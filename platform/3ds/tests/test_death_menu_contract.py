#!/usr/bin/env python3
"""After Link dies, a menu opens by itself (PLAN U6)."""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATCH = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text(encoding="utf-8")


class DeathMenuContractTests(unittest.TestCase):
    def test_menu_has_the_choices_that_make_sense_after_dying(self):
        start = PATCH.index('+LISTMENU "LegendDeathMenu"')
        block = PATCH[start:PATCH.index("+}", start)]
        for item in ('"LegendContinue"', '"LoadGameMenu"', '"PlayerclassMenu"', '"QuitMenu"'):
            self.assertIn(item, block)
        self.assertNotIn("SaveGameMenu", block)  # cannot save while dead

    def test_game_ticker_opens_it_once_after_a_delay(self):
        source = (ROOT / "src/g_game.cpp").read_text(encoding="utf-8")
        self.assertIn("openmenu LegendDeathMenu", source)
        self.assertIn("++deadTics == DeathMenuDelayTics", source)

    def test_continue_does_what_use_does(self):
        source = (ROOT / "src/menu/doommenu.cpp").read_text(encoding="utf-8")
        self.assertIn('menu == FName("LegendContinue")', source)
        self.assertIn("PST_REBORN", source)


if __name__ == "__main__":
    unittest.main()
