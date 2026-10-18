#!/usr/bin/env python3
"""Rewrites a pk3 so that its maps/*.wad and music/ entries are stored, not compressed.

GZDoom opens a map WAD inside a zip through FileSystem::ReopenFileReader: a stored entry is read
straight from the pk3 file, a compressed one is first inflated whole into the heap (FillCache).
MAP01.wad is 2.3 MB: on the Old 3DS (80 MB mode) that one block failed to find room on a
fragmented heap (2DS, 2026-10-14). The music goes stored too, for the same reason: the game
streams it from the reader, so a compressed song sat inflated whole in the heap while it played
(ZOVRWRLD.mp3, 0.7 MB, in the MAP01 census of 2026-10-18); deflate barely shrinks an MP3 or Ogg
anyway. The other entries are recompressed with deflate.

    store-map-wads.py PK3
"""
import os
import sys
import zipfile


def main(path):
    temp = path + ".tmp"
    with zipfile.ZipFile(path) as source, \
            zipfile.ZipFile(temp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as target:
        for info in source.infolist():
            data = source.read(info)
            name = info.filename.lower()
            stored = (name.startswith("maps/") and name.endswith(".wad")) or \
                name.startswith("music/")
            entry = zipfile.ZipInfo(info.filename, info.date_time)
            entry.external_attr = info.external_attr
            entry.compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
            target.writestr(entry, data)
    os.replace(temp, path)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
