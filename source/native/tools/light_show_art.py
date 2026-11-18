#!/usr/bin/env python3
"""Draw the front and rear light diagrams for each preview frame."""
import argparse
from io import BytesIO
from zipfile import ZIP_STORED, ZipFile, ZipInfo

from PIL import Image, ImageDraw, ImageFont

TILE_WIDTH, TILE_HEIGHT = 200, 91
TILE_CAPTIONS = ("FRONT", "REAR", "BOTH", "INSANE", "STOP")


def button_tile(caption, font):
    scale = 2
    w, h = TILE_WIDTH * scale, TILE_HEIGHT * scale
    image = Image.new("RGBA", (w, h))
    draw = ImageDraw.Draw(image)
    for y in range(h):
        shade = int(42 - 20 * y / (h - 1))
        draw.line((0, y, w - 1, y), fill=(shade, shade, shade + 6, 255))
    mask = Image.new("L", image.size)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), radius=20, fill=255)
    image.putalpha(mask)
    draw.rounded_rectangle((1, 1, w - 2, h - 2), radius=20, outline="#484951", width=2)
    draw.text((w // 2, 40 * scale), caption, anchor="mm", font=font, fill="#e7e8ec")
    draw.rectangle((64 * scale, 74 * scale, 136 * scale, 75 * scale), fill="#d01a37")
    return image.resize((TILE_WIDTH, TILE_HEIGHT), Image.Resampling.LANCZOS)


def idle_prompt():
    image = Image.new("RGBA", (640, 48))
    ImageDraw.Draw(image).text((0, 24), "Select a light pattern",
                              font=ImageFont.load_default(size=32),
                              anchor="lm", fill="#aebbc5")
    return image.resize((320, 24), Image.Resampling.LANCZOS)


def frame(mask):
    scale = 2
    image = Image.new("RGB", (1056 * scale, 252 * scale), "#11141a")
    draw = ImageDraw.Draw(image)

    def coords(points):
        return [(int(x * scale), int(y * scale)) for x, y in points]

    def box(x0, y0, x1, y1):
        return tuple(value * scale for value in (x0, y0, x1, y1))

    def line(points, color, width=1):
        draw.line(coords(points), fill=color, width=width * scale, joint="curve")

    def shape(points, fill, outline):
        draw.polygon(coords(points), fill=fill)
        line(points + points[:1], outline, 2)

    font = ImageFont.load_default(size=34)
    for index, caption in enumerate(("FRONT", "REAR")):
        rear = index == 1
        x0 = 12 + index * 524
        center = x0 + 254
        draw.rounded_rectangle(box(x0, 6, x0 + 508, 246), radius=26,
                               fill="#191e26", outline="#34404b", width=2)
        draw.text((center * scale, 226 * scale), caption, anchor="ms",
                  fill="#aebbc5", font=font)
        line([(center - 187, 188), (center + 187, 188)], "#374049")
        body = [(-164,165),(-170,140),(-162,116),(-139,105),(-107,67),(-88,60),
                (88,60),(107,67),(139,105),(162,116),(170,140),(164,165),
                (139,178),(-139,178)]
        shape([(center+x,y) for x,y in body], "#252e39", "#586573")
        glass = [(-127,107),(-99,75),(-82,68),(82,68),(99,75),(127,107)]
        shape([(center+x,y) for x,y in glass], "#18242f", "#475767")
        line([(center - 151,156),(center - 114,163),(center + 114,163),(center + 151,156)],
             "#485363")
        if rear:
            line([(center-136,118),(center+136,118)], "#616b78", 2)
            draw.rounded_rectangle(box(center-32,130,center+32,149), radius=7,
                                   outline="#526172", width=2)
        else:
            for offset in (-35, 4):
                draw.rounded_rectangle(box(center+offset,119,center+offset+31,143),
                                       radius=11, outline="#667381", width=3)
                for slat in (8, 15, 22):
                    line([(center+offset+slat,122),(center+offset+slat,139)], "#465462")

        for side in (-1, 1):
            bit = 2 * index + (side == 1)
            active = bool(mask & (1 << bit))
            stroke = ("#d69288" if rear else "#b5d7e0") if active else "#596575"
            thick = 5 if active else 2
            points = [(93,136),(122,136),(149,126)] if rear else [(88,132),(114,140),(145,132)]
            line([(center + side * x, y) for x, y in points], stroke, thick)
    return image.resize((1056, 252), Image.Resampling.LANCZOS)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image_database")
    parser.add_argument("--font", default="DejaVuSans-Bold.ttf")
    args = parser.parse_args()
    font = ImageFont.truetype(args.font, 56)
    images = [(57600 + mask, frame(mask)) for mask in range(16)]
    images.extend((57620 + i, button_tile(caption, font))
                  for i, caption in enumerate(TILE_CAPTIONS))
    images.append((57625, Image.new("RGBA", (1, 1))))
    images.append((57626, idle_prompt()))
    with ZipFile(args.image_database, "w") as output:
        for identifier, image in images:
            png = BytesIO()
            image.save(png, "PNG", optimize=True)
            output.writestr(ZipInfo(f"{identifier}.png"), png.getvalue(), ZIP_STORED)
