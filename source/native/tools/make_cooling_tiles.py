"""Update the Max Cooling page and its tiles in the existing MHD image archive."""

import argparse
from io import BytesIO
from pathlib import Path
import tempfile
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile

from PIL import Image, ImageDraw, ImageFont


WIDTH, HEIGHT = 380, 200
ASSETS = {
    56010: ("OFF", False),
    56011: ("OFF", True),
    56012: ("ON", False),
    56013: ("ON", True),
}
ASSET_NAMES = {f"{identifier}.png" for identifier in (*ASSETS, 56014)}


def tile(text, active, font):
    gradient = Image.new("RGBA", (WIDTH, HEIGHT))
    draw = ImageDraw.Draw(gradient)
    top, bottom = ((242, 8, 49), (155, 4, 37)) if active else ((44, 46, 55), (29, 30, 37))
    for y in range(HEIGHT):
        rgb = tuple(round(a + (b - a) * y / (HEIGHT - 1)) for a, b in zip(top, bottom))
        draw.line((0, y, WIDTH - 1, y), fill=(*rgb, 255))
    mask = Image.new("L", gradient.size)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, WIDTH - 1, HEIGHT - 1), radius=13, fill=255)
    gradient.putalpha(mask)
    draw.rounded_rectangle((0, 0, WIDTH - 1, HEIGHT - 1), radius=13,
                           outline="#ff4361" if active else "#4b4d56", width=2)
    draw.text((WIDTH // 2, 92), text, font=font, anchor="mm", fill="#f7eff0")
    draw.rounded_rectangle((126, 162, 254, 167), radius=3,
                           fill="#fff2f4" if active else "#e10a35")
    return gradient


def add_position(node, x, y, width, height, artwork=False):
    properties = ET.SubElement(node, "properties")
    for prop, value in ((20, x), (21, y), (9, width), (10, height)):
        ET.SubElement(properties, "property", id=str(prop), value=str(value))
    if artwork:
        ET.SubElement(properties, "property", id="2", value="false")
        ET.SubElement(properties, "property", id="3", value="true")


def update_page(path):
    xml = ET.parse(path)
    app = xml.getroot().find("pluginApp")
    models = app.find("models")
    for model, asset in ((63, 56010), (64, 56012), (65, 56014)):
        old = models.find(f"*[@id='{model}']")
        if old is not None:
            models.remove(old)
        ET.SubElement(models, "imageIdModel", id=str(model), imageId=str(asset))
    page = app.find("hmiStates/hmiState[@id='103']")
    page.remove(page.find("components"))
    group = ET.SubElement(page, "components")
    for index, x in enumerate((146, 561)):
        add_position(ET.SubElement(group, "image", id=str(223 + index),
                                   model=str(63 + index)), x, 130, WIDTH, HEIGHT, artwork=True)
        add_position(ET.SubElement(group, "button", id=str(218 + index),
                                   action=str(28 + index), imageModel="65"), x, 130, WIDTH, HEIGHT)
    add_position(ET.SubElement(group, "label", id="322", model="52"), 35, 360, 1020, 50)
    ET.indent(xml, space="  ")
    xml.write(path, encoding="utf-8", xml_declaration=True)


def update_archive(path, font):
    with tempfile.NamedTemporaryFile(dir=path.parent) as updated:
        with ZipFile(path) as base, ZipFile(updated, "w", ZIP_DEFLATED) as archive:
            for entry in base.infolist():
                if entry.filename not in ASSET_NAMES:
                    archive.writestr(entry, base.read(entry))
            images = {identifier: tile(text, active, font)
                      for identifier, (text, active) in ASSETS.items()}
            images[56014] = Image.new("RGBA", (1, 1))
            for identifier, image in images.items():
                buffer = BytesIO()
                image.save(buffer, format="PNG", optimize=True)
                archive.writestr(f"{identifier}.png", buffer.getvalue())
        updated.seek(0)
        path.write_bytes(updated.read())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xml", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--font", default="DejaVuSans-Bold.ttf")
    args = parser.parse_args()
    update_archive(args.archive, ImageFont.truetype(args.font, 55))
    update_page(args.xml)
