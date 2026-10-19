#!/usr/bin/env python3
"""The UPDATE screen always has notes for the top screen (PLAN U9)."""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class UpdateChangelogContractTests(unittest.TestCase):
    def test_build_bundles_the_installed_versions_notes(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        self.assertIn("bundle_update_changelog()", build)
        # Both packages: the 3DSX romfs and the CIA romfs.
        self.assertIn('bundle_update_changelog "${PACKAGE_TMP}/update-romfs"', build)
        self.assertIn('bundle_update_changelog "${CIA_ROMFS}"', build)

    def test_view_falls_back_to_saved_then_bundled_notes(self):
        view = (ROOT / "src/common/platform/3ds/update/changelog_view.inc").read_text(encoding="utf-8")
        for needle in ("update-notes-new.txt", "update-notes-all.txt", "romfs:/update-changelog.txt",
                       "saved[0] ? saved : bundled"):
            self.assertIn(needle, view)


if __name__ == "__main__":
    unittest.main()
