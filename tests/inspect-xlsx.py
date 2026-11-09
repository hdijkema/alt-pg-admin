"""Inspect exported OOXML independently, using only Python's standard library."""
import json
import re
import sys
import xml.etree.ElementTree as ET
from zipfile import ZipFile

NS = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}


def text(element):
    """Decode Excel's escaped characters after joining rich/inline string text."""
    value = "".join(element.itertext())
    return re.sub(r"_x([0-9A-Fa-f]{4})_", lambda match: chr(int(match[1], 16)), value)


with ZipFile(sys.argv[1]) as archive:
    assert archive.testzip() is None
    workbook = ET.fromstring(archive.read("xl/workbook.xml"))
    sheet = ET.fromstring(archive.read("xl/worksheets/sheet1.xml"))
    shared = []
    if "xl/sharedStrings.xml" in archive.namelist():
        shared = [text(item) for item in ET.fromstring(archive.read("xl/sharedStrings.xml"))]
    cells, types, formulas = {}, {}, []
    for cell in sheet.findall(".//s:c", NS):
        ref = cell.attrib["r"]
        kind = cell.get("t", "n")
        types[ref] = kind
        if cell.find("s:f", NS) is not None:
            formulas.append(ref)
        if kind == "inlineStr":
            cells[ref] = text(cell.find("s:is", NS))
        elif kind == "s":
            cells[ref] = shared[int(cell.findtext("s:v", namespaces=NS))]
        else:
            cells[ref] = cell.findtext("s:v", "", NS)
    styles = ET.fromstring(archive.read("xl/styles.xml"))
    formats = styles.find("s:cellXfs", NS)
    fonts = styles.find("s:fonts", NS)
    bold = [cell.attrib["r"] for cell in sheet.findall(".//s:c", NS)
            if fonts[int(formats[int(cell.get("s", 0))].get("fontId", 0))].find("s:b", NS) is not None]
    print(json.dumps({
        "sheets": [item.attrib["name"] for item in workbook.findall("s:sheets/s:sheet", NS)],
        "cells": cells, "types": types, "formulas": formulas, "bold": bold,
        "widths": [float(item.attrib["width"]) for item in sheet.findall("s:cols/s:col", NS)],
    }, ensure_ascii=True))
