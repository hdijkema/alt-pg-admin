#!/usr/bin/env python3
"""Check complete catalogs and protect placeholders used by QString::arg."""
from collections import Counter
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
expected_keys = None
for language in ("en", "de"):
    catalog = ET.parse(root / "translations" / f"pgadmin_{language}.ts")
    keys = set()
    assert catalog.getroot().get("language") == language
    for context in catalog.findall("context"):
        for message in context.findall("message"):
            source = message.findtext("source") or ""
            translation = message.find("translation")
            text = translation.text or "" if translation is not None else ""
            label = f"{language}: {context.findtext('name')}: {source}"
            assert translation is not None and not translation.get("type"), label
            assert text, label
            assert Counter(re.findall(r"%L?\d+|%n", source)) == Counter(re.findall(r"%L?\d+|%n", text)), label
            assert source.count("\n") == text.count("\n"), label
            keys.add((context.findtext("name"), source))
    if expected_keys is not None:
        assert keys == expected_keys, "Catalogs have different source messages"
    expected_keys = keys
    print(f"{language}: {len(keys)} complete translations; placeholders and newlines preserved")
