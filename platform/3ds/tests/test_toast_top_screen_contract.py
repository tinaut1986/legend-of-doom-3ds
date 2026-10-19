#!/usr/bin/env python3
"""Toasts are on the top screen, beside the game's own messages (PLAN U7)."""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DIAG = ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
NOTIFY = ROOT / "src/console/c_notifybuffer.cpp"


class ToastTopScreenContractTests(unittest.TestCase):
    def test_bottom_screen_draws_no_toast(self):
        source = DIAG.read_text(encoding="utf-8")
        body = source[source.index("void DrawBottomToast("):]
        body = body[:body.index("\n}\n")]
        self.assertNotIn("OverlayNotification", body)

    def test_top_screen_paths_compose_the_toast(self):
        source = DIAG.read_text(encoding="utf-8")
        # Gameplay (GPU, automap, canvas) and both menu paths.
        self.assertGreaterEqual(source.count("ComposeTopToast(pixels"), 5)

    def test_toast_follows_messages_and_stacks_beside_them(self):
        source = DIAG.read_text(encoding="utf-8")
        body = source[source.index("void ComposeTopToast("):]
        body = body[:body.index("\n}\n")]
        self.assertIn("lod3ds_messages_bottom", body)
        self.assertIn("C_3DSNotifyOccupied", body)
        self.assertIn("float C_3DSNotifyOccupied()", NOTIFY.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
