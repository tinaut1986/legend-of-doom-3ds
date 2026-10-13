#!/usr/bin/env python3
"""The interface's languages (PLAN U3): every text the port translates has a row.

Tr("ENGLISH TEXT") looks up LOD_ENGLISH_TEXT in language.csv, which the game patch adds to
LegendOfDoom.pk3; a missing row would leave that text in English when Spanish is chosen.
"""

import csv
import io
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch"
SOURCES = [
    ROOT / "src/common/platform/3ds/native_menu_3ds.inc",
    ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp",
    ROOT / "src/common/platform/3ds/update/changelog_view.inc",
]


def key(english):
    return "LOD_" + "_".join(re.findall(r"[A-Za-z0-9]+", english)).upper()


def language_rows():
    patch = PATCH.read_text(encoding="utf-8")
    start = patch.index("+++ b/language.csv")
    hunk = patch[start:].split("\n")[2:]
    lines = []
    for line in hunk:
        if not line.startswith("+"):
            break
        lines.append(line[1:])
    return list(csv.reader(io.StringIO("\n".join(lines))))


class LanguageContractTests(unittest.TestCase):
    def test_every_translated_literal_has_a_spanish_row(self):
        rows = language_rows()
        header = rows[0]
        self.assertEqual(header[:4], ["default", "Identifier", "Remarks", "Filter"])
        spanish = {row[1]: row[4] for row in rows[1:] if len(row) > 4 and row[1]}
        literals = set()
        for source in SOURCES:
            literals.update(re.findall(r'Tr\("([^"]+)"\)', source.read_text(encoding="utf-8")))
        missing = sorted(text for text in literals if not spanish.get(key(text)))
        self.assertEqual(missing, [])

    def test_keys_follow_tr(self):
        for row in language_rows()[1:]:
            if row and row[1]:
                self.assertEqual(row[1], key(row[0]), row)


if __name__ == "__main__":
    unittest.main()
