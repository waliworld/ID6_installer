"""Place the existing Exhaust Flap artwork and actions under MHD+ Options."""

import argparse
from io import BytesIO
from pathlib import Path
import tempfile
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile

from PIL import Image

from make_cooling_tiles import add_position

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("xml", type=Path)
parser.add_argument("mhd_images", type=Path)
parser.add_argument("flap_images", type=Path)
args = parser.parse_args()

xml = ET.parse(args.xml)
app = xml.getroot().find("pluginApp")
models = app.find("models")
for model, image in ((66, 56200), (67, 56202), (68, 56204)):
    ET.SubElement(models, "imageIdModel", id=str(model), imageId=str(image))

states = app.find("hmiStates")
states.find("hmiState[@id='100']/optionComponents/button[@id='312']").set("text", "Exhaust Flap")
flap = states.find("hmiState[@id='102']")
flap.set("text", "Exhaust Flap")
flap.remove(flap.find("components"))
group = ET.SubElement(flap, "components")
for index, (x, action) in enumerate(((52, 26), (384, 25), (716, 27))):
    add_position(ET.SubElement(group, "image", id=str(230 + index), model=str(66 + index)),
                 x, 130, 320, 168, artwork=True)
    add_position(ET.SubElement(group, "button", id=str(215 + index), action=str(action), imageModel="65"),
                 x, 130, 320, 168)
add_position(ET.SubElement(group, "label", id="321", model="51"), 35, 360, 1020, 50)
ET.indent(xml, space="  ")
xml.write(args.xml, encoding="utf-8", xml_declaration=True)

with tempfile.NamedTemporaryFile(dir=args.mhd_images.parent) as updated:
    with ZipFile(args.mhd_images) as mhd, ZipFile(args.flap_images) as source, ZipFile(updated, "w", ZIP_DEFLATED) as merged:
        for entry in mhd.infolist():
            merged.writestr(entry, mhd.read(entry))
        for image_id in range(56200, 56206):
            name = f"{image_id}.png"
            assert name not in mhd.namelist()
            with Image.open(BytesIO(source.read(name))) as original:
                buffer = BytesIO()
                original.convert("RGBA").resize((320, 168), Image.Resampling.LANCZOS).save(buffer, format="PNG")
                merged.writestr(name, buffer.getvalue())
    updated.seek(0)
    args.mhd_images.write_bytes(updated.read())
