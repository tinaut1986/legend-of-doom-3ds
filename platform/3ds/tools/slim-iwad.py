#!/usr/bin/env python3
"""Write a slim copy of freedoom2.wad holding only what Legend of Doom uses (PLAN P4.9).

The engine makes a texture object for every patch, texture, flat and sprite frame of the IWAD,
and the Old 3DS spends most of its startup in "Loading textures" walking them. Legend of Doom
brings its own textures, flats, sprites and music; from Freedoom it needs the engine's base
lumps (palette, colormap, fonts, menu and status bar graphics, sounds), a few sprites of the
stock Doom actors it still uses, and MAP01 (the IWAD check asks for it; the pk3's MAP01 wins).

    slim-iwad.py IN.wad OUT.wad [--mod LegendOfDoom.pk3]

With --mod, it fails when a map of the pk3 names a texture that neither the pk3 nor the slim
WAD provides, or when the mod's scripts name a Freedoom sprite that was dropped.
"""

import argparse
import re
import struct
import sys
import zipfile

# Sprites of stock actors the mod still uses: DoomPlayer (ZeldaPlayer's states), its Fist
# (weapon slot 1), BulletPuff (the swords), the fogs and blood the engine can spawn, and the
# armour icons Doom's gameinfo names (mapinfo/doomcommon.txt; a level load warns without them).
KEEP_SPRITES = {"PLAY", "PUNG", "PUFF", "TFOG", "IFOG", "BLUD", "ARM1", "ARM2"}
# Flats: the sky marker the maps use for their ceilings.
KEEP_FLATS = {"F_SKY1"}
# Textures: the first one (texture 0 is the engine's "no texture") and the default sky.
KEEP_TEXTURES = {"SKY1"}
# Map lumps: MAP01 stays, the rest of Freedoom's levels go.
MAP_LUMPS = {"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES",
             "SECTORS", "REJECT", "BLOCKMAP", "BEHAVIOR", "SCRIPTS", "TEXTMAP", "ZNODES",
             "ENDMAP", "DIALOGUE"}
NAMESPACES = {"S": "sprite", "SS": "sprite", "F": "flat", "FF": "flat", "F1": "flat",
              "F2": "flat", "F3": "flat", "P": "patch", "PP": "patch", "P1": "patch",
              "P2": "patch", "P3": "patch"}


def read_wad(path):
    data = open(path, "rb").read()
    ident, count, offset = struct.unpack_from("<4sii", data, 0)
    lumps = []
    for i in range(count):
        pos, size, name = struct.unpack_from("<ii8s", data, offset + 16 * i)
        lumps.append([name.rstrip(b"\0").decode("latin1").upper(), data[pos:pos + size]])
    return ident, lumps


def write_wad(path, ident, lumps):
    body = bytearray()
    directory = bytearray()
    for name, data in lumps:
        directory += struct.pack("<ii8s", 12 + len(body), len(data), name.encode("latin1"))
        body += data
    with open(path, "wb") as out:
        out.write(struct.pack("<4sii", ident, len(lumps), 12 + len(body)))
        out.write(body)
        out.write(directory)


def parse_textures(texture1, pnames):
    count = struct.unpack_from("<i", pnames, 0)[0]
    patch_names = [pnames[4 + 8 * i:12 + 8 * i].rstrip(b"\0").decode("latin1").upper()
                   for i in range(count)]
    textures = []
    count = struct.unpack_from("<i", texture1, 0)[0]
    for i in range(count):
        start = struct.unpack_from("<i", texture1, 4 + 4 * i)[0]
        name = texture1[start:start + 8].rstrip(b"\0").decode("latin1").upper()
        patch_count = struct.unpack_from("<h", texture1, start + 20)[0]
        end = start + 22 + 10 * patch_count
        patches = [struct.unpack_from("<h", texture1, start + 22 + 10 * p + 4)[0]
                   for p in range(patch_count)]
        textures.append((name, texture1[start:end], patches))
    return patch_names, textures


def build_textures(patch_names, textures, keep):
    kept = [t for i, t in enumerate(textures) if i == 0 or t[0] in keep]
    used = []
    for _, _, patches in kept:
        for p in patches:
            if patch_names[p] not in used:
                used.append(patch_names[p])
    texture1 = bytearray(struct.pack("<i", len(kept)))
    offsets_end = 4 + 4 * len(kept)
    records = bytearray()
    for name, record, patches in kept:
        texture1 += struct.pack("<i", offsets_end + len(records))
        record = bytearray(record)
        for n, p in enumerate(patches):
            struct.pack_into("<h", record, 22 + 10 * n + 4, used.index(patch_names[p]))
        records += record
    texture1 += records
    pnames = struct.pack("<i", len(used)) + b"".join(
        n.encode("latin1").ljust(8, b"\0") for n in used)
    return bytes(texture1), pnames, [t[0] for t in kept], set(used)


def slim(lumps):
    by_name = {name: data for name, data in lumps}
    patch_names, textures = parse_textures(by_name["TEXTURE1"], by_name["PNAMES"])
    texture1, pnames, kept_textures, kept_patches = build_textures(
        patch_names, textures, KEEP_TEXTURES)

    out = []
    namespace = None
    in_map = False
    dropped = {}
    for name, data in lumps:
        if name.endswith("_START") and name[:-6] in NAMESPACES:
            namespace = NAMESPACES[name[:-6]]
            out.append([name, data])
            continue
        if name.endswith("_END") and name[:-4] in NAMESPACES:
            namespace = None
            out.append([name, data])
            continue
        if re.fullmatch(r"MAP\d\d", name):
            in_map = name != "MAP01"
            kind = None if not in_map else "map"
        elif in_map and name in MAP_LUMPS:
            kind = "map"
        else:
            in_map = False
            if namespace == "sprite":
                kind = None if name[:4] in KEEP_SPRITES else "sprite"
            elif namespace == "flat":
                kind = None if name in KEEP_FLATS else "flat"
            elif namespace == "patch":
                kind = None if name in kept_patches else "patch"
            elif re.fullmatch(r"DEMO\d", name):
                kind = "demo"
            else:
                kind = None
        if kind:
            dropped[kind] = dropped.get(kind, 0) + 1
            continue
        if name == "TEXTURE1":
            data = texture1
        elif name == "PNAMES":
            data = pnames
        out.append([name, data])
    dropped["texture"] = len(textures) - len(kept_textures)
    return out, dropped


def iwad_texture_names(lumps):
    """Every name a map can use as a texture or flat in this WAD."""
    by_name = {name: data for name, data in lumps}
    _, textures = parse_textures(by_name["TEXTURE1"], by_name["PNAMES"])
    names = {t[0] for t in textures}
    namespace = None
    for name, _ in lumps:
        if name.endswith("_START"):
            namespace = name
        elif name.endswith("_END"):
            namespace = None
        elif namespace and namespace.startswith("F"):
            names.add(name)
    return names


def check_mod(mod_path, full_lumps, slim_lumps):
    """Fail when the mod needs something the full WAD has and the slim one dropped."""
    mod = zipfile.ZipFile(mod_path)
    provided = set()
    text = []
    for entry in mod.infolist():
        name = entry.filename
        base = name.rsplit("/", 1)[-1].rsplit(".", 1)[0].upper()
        if name.lower().startswith(("textures/", "flats/", "patches/", "graphics/",
                                    "sprites/")):
            provided.add(base)
        elif name.lower().startswith("maps/") and name.lower().endswith(".wad"):
            data = mod.read(entry)
            count, offset = struct.unpack_from("<ii", data, 4)
            for i in range(count):
                pos, size, lump = struct.unpack_from("<ii8s", data, offset + 16 * i)
                if lump.rstrip(b"\0") == b"TEXTMAP":
                    text.append(("map", name, data[pos:pos + size].decode("latin1")))
        elif not name.lower().endswith((".png", ".ogg", ".wav", ".mp3", ".ico", ".o")):
            text.append(("script", name, mod.read(entry).decode("latin1")))

    dropped = iwad_texture_names(full_lumps) - iwad_texture_names(slim_lumps) - provided

    problems = []
    for kind, name, body in text:
        if kind == "map":
            for texture in set(re.findall(r'texture\w+\s*=\s*"([^"]*)"', body)):
                texture = texture.upper()
                if texture in dropped:
                    problems.append(f"{name}: texture {texture} was dropped from the IWAD")
        else:
            # A state line: SPRT FRAMES TICS. Only Freedoom's sprite names can be missing.
            for sprite in set(re.findall(r"^\s*\"?([A-Z0-9_]{4})\"?\s+[A-Z\[\]\\#\"]+\s+-?\d",
                                         body, re.M)):
                if sprite not in KEEP_SPRITES and sprite in FREEDOOM_SPRITES and \
                        sprite not in {p[:4] for p in provided}:
                    problems.append(f"{name}: sprite {sprite} was dropped from the IWAD")
    return problems


FREEDOOM_SPRITES = set()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--mod", help="LegendOfDoom.pk3, to check that nothing it needs is gone")
    args = parser.parse_args()

    ident, lumps = read_wad(args.input)
    namespace = None
    for name, _ in lumps:
        if name.endswith("_START"):
            namespace = name
        elif name.endswith("_END"):
            namespace = None
        elif namespace in ("S_START", "SS_START"):
            FREEDOOM_SPRITES.add(name[:4])

    out, dropped = slim(lumps)
    if args.mod:
        problems = check_mod(args.mod, lumps, out)
        if problems:
            for problem in sorted(set(problems)):
                print(f"slim-iwad: {problem}", file=sys.stderr)
            return 1
    write_wad(args.output, ident, out)
    summary = ", ".join(f"{n} {k}s" for k, n in sorted(dropped.items()))
    size_in = sum(len(d) for _, d in lumps)
    size_out = sum(len(d) for _, d in out)
    print(f"slim-iwad: {len(lumps)} -> {len(out)} lumps, {size_in / 1e6:.1f} -> "
          f"{size_out / 1e6:.1f} MB; dropped {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
