#!/usr/bin/env python3
"""Generate Road Jet's RHMI image database."""
import argparse
from io import BytesIO
from zipfile import ZIP_STORED, ZipFile, ZipInfo

from PIL import Image, ImageDraw

WIDTH, HEIGHT = 1080, 368
PERIOD = 72
FRAMES = 12
LEVELS = 17
OBSTACLE_FRAMES = 26


def road_frame(offset):
    road = Image.new("RGBA", (WIDTH, HEIGHT), "#0e1920")
    draw = ImageDraw.Draw(road)
    for lane in range(5):
        x = 120 + lane * 168
        draw.rectangle((x, 0, x + 167, HEIGHT), fill=("#1b2933", "#202e39")[lane % 2])
    for edge in (120, 960):
        draw.rectangle((edge - 9, 0, edge + 9, HEIGHT), fill="#4a635e")
        draw.line((edge, 0, edge, HEIGHT), fill="#b7c4ab", width=2)
    for y in range(offset - PERIOD, HEIGHT, PERIOD):
        for lane in range(1, 5):
            x = 120 + lane * 168
            draw.rectangle((x - 1, y, x + 1, y + 30), fill="#8398a8")
        for edge in (120, 960):
            draw.rectangle((edge - 5, y, edge + 5, y + 17), fill="#d6b584")
        for x in (58, WIDTH - 58):
            draw.line((x - 5, y, x + 4, y), fill="#355360", width=2)
    shade = Image.new("RGBA", road.size)
    draw = ImageDraw.Draw(shade)
    for y in range(HEIGHT):
        alpha = int(max((53 - y) * 2.7, (y - (HEIGHT - 62)) * 1.45, 0))
        draw.line((0, y, WIDTH, y), fill=(7, 19, 29, alpha))
    return Image.alpha_composite(road, shade).convert("RGB")


def jet_frame(level):
    width = 72
    sprite = Image.new("RGBA", (width * 2, HEIGHT * 2))
    draw = ImageDraw.Draw(sprite)
    x = width // 2
    y = HEIGHT - 45 - level * (HEIGHT - 88) / (LEVELS - 1)

    def poly(points, color, outline=None):
        draw.polygon([((x + dx) * 2, (y + dy) * 2) for dx, dy in points],
                     fill=color, outline=outline)

    draw.ellipse(((x - 28) * 2, (y - 13) * 2, (x + 31) * 2, (y + 23) * 2),
                 fill="#03101969")
    poly([(-7, 14), (7, 14), (0, 24 + level * .75)], "#2a9f93")
    poly([(-4, 14), (4, 14), (0, 20 + level * .62)], "#9dffee")
    poly([(0, -22), (5, -14), (7, -4), (24, 8), (24, 12), (10, 9), (11, 17),
          (0, 14), (-11, 17), (-10, 9), (-24, 12), (-24, 8), (-7, -4), (-5, -14)],
         "#dcecf4", "#142c37")
    poly([(0, -21), (5, -13), (7, -3), (4, 11), (0, 15), (-4, 11), (-7, -3),
          (-5, -13)], "#7fc8ca")
    poly([(0, -13), (3, -8), (2, 0), (-2, 0), (-3, -8)], "#123745")
    poly([(-22, 8), (-17, 8), (-17, 10), (-22, 10)], "#ff997d")
    poly([(17, 8), (22, 8), (22, 10), (17, 10)], "#79eac8")
    return sprite.resize((width, HEIGHT), Image.Resampling.LANCZOS)


def obstacle_frame(police, frame):
    sprite = Image.new("RGBA", (144, HEIGHT * 2))
    draw = ImageDraw.Draw(sprite)
    y = -24 + frame * 16

    def box(left, top, right, bottom):
        return tuple(2 * value for value in (36 + left, y + top, 36 + right, y + bottom))

    draw.ellipse(box(-19, -8, 21, 24), fill="#02081077")
    if police:
        draw.rounded_rectangle(box(-17, -23, 17, 23), radius=12,
                               fill="#d2dce6", outline="#111923", width=3)
        draw.rounded_rectangle(box(-16, -19, 16, -9), radius=6, fill="#252e3a")
        draw.rectangle(box(-12, -7, 12, 8), fill="#394f61")
        draw.rounded_rectangle(box(-16, 12, 16, 20), radius=5, fill="#252e3a")
        draw.rectangle(box(-13, -2, 0, 2), fill="#cf334d")
        draw.rectangle(box(1, -2, 13, 2), fill="#419ad3")
        for x in (-12, 6):
            draw.rectangle(box(x, 18, x + 6, 20), fill="#ef815a")
    else:
        draw.rounded_rectangle(box(-15, -5, 15, 16), radius=4, fill="#20262d")
        draw.ellipse(box(-11, -10, 11, 12), fill="#ff822c", outline="#ffc67c", width=2)
        draw.ellipse(box(-7, -6, 7, 8), outline="#fff1d8", width=3)
        draw.ellipse(box(-3, -2, 3, 4), fill="#e35912")
    return sprite.resize((72, HEIGHT), Image.Resampling.LANCZOS)


def write_image(archive, identifier, image):
    data = BytesIO()
    image.save(data, format="PNG", optimize=True)
    archive.writestr(ZipInfo(f"{identifier}.png"), data.getvalue(), ZIP_STORED)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image_database")
    args = parser.parse_args()
    with ZipFile(args.image_database, "w") as archive:
        for index in range(FRAMES):
            write_image(archive, 57300 + index, road_frame(index * PERIOD // FRAMES))
        for index in range(LEVELS):
            write_image(archive, 57320 + index, jet_frame(index))
        write_image(archive, 57337, Image.new("RGBA", (72, HEIGHT)))
        for kind in range(2):
            for index in range(OBSTACLE_FRAMES):
                write_image(archive, 57350 + kind * OBSTACLE_FRAMES + index,
                            obstacle_frame(kind == 1, index))
