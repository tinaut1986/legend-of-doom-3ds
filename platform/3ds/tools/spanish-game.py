#!/usr/bin/env python3
"""Legend of Doom in Spanish: the fonts' letters and the pages drawn with English text.

A build step over the game's package directory (in place, safe to run twice).

Fonts. 
The game's fonts (SMALLFNT, BIGFONT: 8x8; ZeldFNT2, the NPC dialogue: the same letters
with an outline, 10x10) hold characters 32-95 only, so GZDoom drew Spanish text with the
accents stripped (MONTAÑA as MONTANA) and no ¿ or ¡. This extends each one to 255:

- Á É Í Ó Ú Ñ Ü: the letter squeezed to 6 rows with a one-row mark on top, inside the
  cell (FON2 has one height and no offsets, and the line spacing must not change).
- ¿ ¡: the font's own ? and ! turned upside down.
- a-z and the other Latin-1 letters: their capital (the fonts have capitals only), so
  every character keeps its width and lower case still draws as before.
- anything else: blank.

The palette, height and widths stay as they were, so colours and layout do not change.
The native menus read SMALLFNT too and ignore characters above 127.

Pages. ZSTORY (the story, our menu-story.png) and ENDR (the ending) have their text in
the picture. Their Spanish variants, localized/textures/<NAME>.es.png, are the same
picture with the text area cleared and the Spanish drawn with SMALLFNT at the original's
size, place and colours; GZDoom shows them while the language is Spanish.

Usage: spanish-game.py PACKAGE_DIR
"""

import struct
import sys
import unicodedata
import zlib
from pathlib import Path

# The 8x8 shapes: the mark in row 0, then the capital squeezed into rows 1-6 by keeping
# six of its seven rows. Row 7 stays blank (the line gap).
MARKS = {
    "acute": "....11..",
    "tilde": ".11111..",
    "dieresis": ".11.11..",
}
ACCENTED = {
    0xC1: ("A", "acute", (0, 1, 2, 4, 5, 6)),
    0xC9: ("E", "acute", (0, 1, 3, 4, 5, 6)),
    0xCD: ("I", "acute", (0, 1, 2, 4, 5, 6)),
    0xD3: ("O", "acute", (0, 1, 2, 4, 5, 6)),
    0xDA: ("U", "acute", (0, 1, 2, 4, 5, 6)),
    0xD1: ("N", "tilde", (0, 1, 2, 4, 5, 6)),
    0xDC: ("U", "dieresis", (0, 1, 2, 4, 5, 6)),
}
INVERTED = {0xBF: "?", 0xA1: "!"}


def load(path):
    data = path.read_bytes()
    if data[:4] != b"FON2":
        raise SystemExit(f"{path}: not a FON2 font")
    font = {
        "height": data[4] | data[5] << 8,
        "first": data[6],
        "last": data[7],
        "mono": data[8],
        "shading": data[9],
        "colors": data[10],
        "flags": data[11],
    }
    at = 12
    font["kerning"] = data[at:at + 2] if font["flags"] & 1 else b""
    at += len(font["kerning"])
    count = font["last"] - font["first"] + 1
    if font["mono"]:
        widths = [struct.unpack_from("<H", data, at)[0]] * count
        at += 2
    else:
        widths = list(struct.unpack_from(f"<{count}H", data, at))
        at += 2 * count
    font["palette"] = data[at:at + (font["colors"] + 1) * 3]
    at += len(font["palette"])
    glyphs = {}
    for index, width in enumerate(widths):
        total = width * font["height"]
        pixels = bytearray()
        while len(pixels) < total:
            code = struct.unpack_from("b", data, at)[0]
            at += 1
            if code >= 0:
                pixels += data[at:at + code + 1]
                at += code + 1
            elif code != -128:
                pixels += bytes([data[at]]) * (1 - code)
                at += 1
        glyphs[font["first"] + index] = (width, bytes(pixels))
    font["glyphs"] = glyphs
    return font


def pack(pixels):
    """ByteRun1, as GZDoom's FON2 reader expects."""
    out = bytearray()
    at = 0
    while at < len(pixels):
        run = 1
        while at + run < len(pixels) and run < 128 and pixels[at + run] == pixels[at]:
            run += 1
        if run > 1:
            out += struct.pack("b", 1 - run) + bytes([pixels[at]])
            at += run
            continue
        start = at
        while at < len(pixels) and at - start < 128:
            if at + 1 < len(pixels) and pixels[at + 1] == pixels[at]:
                break
            at += 1
        out += struct.pack("b", at - start - 1) + pixels[start:at]
    return bytes(out)


def save(path, font):
    first, last = font["first"], font["last"]
    widths = [font["glyphs"][c][0] for c in range(first, last + 1)]
    mono = font["mono"] and len(set(widths)) == 1
    out = bytearray(b"FON2")
    out += struct.pack("<HBBBBBB", font["height"], first, last, mono, font["shading"],
                       font["colors"], font["flags"])
    out += font["kerning"]
    out += struct.pack("<H", widths[0]) if mono else struct.pack(f"<{len(widths)}H", *widths)
    out += font["palette"]
    for character in range(first, last + 1):
        width, pixels = font["glyphs"][character]
        if width:
            out += pack(pixels)
    path.write_bytes(bytes(out))


def rows_of(font, character):
    """The 8x8 shape of a SMALLFNT-style character, as strings of '1' and '.'."""
    width, pixels = font["glyphs"][ord(character)]
    return ["".join("1" if pixels[y * width + x] else "." for x in range(8)) for y in range(8)]


def accented_shape(font8, base, mark, keep):
    letter = rows_of(font8, base)
    shape = [MARKS[mark]] + [letter[row] for row in keep]
    return shape + ["........"]


def render(shape, width, height, outline, ink):
    """A glyph from an 8x8 shape: as is, or shifted by one with a 4-neighbour outline."""
    grid = [[0] * width for _ in range(height)]
    shift = 1 if outline else 0
    for y, row in enumerate(shape):
        for x, cell in enumerate(row):
            if cell == "1":
                grid[y + shift][x + shift] = ink
    if outline:
        filled = [row[:] for row in grid]
        for y in range(height):
            for x in range(width):
                if filled[y][x] == 0 and any(
                    0 <= y + dy < height and 0 <= x + dx < width and filled[y + dy][x + dx]
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1))
                ):
                    grid[y][x] = 1
    return bytes(value for row in grid for value in row)


def upside_down(glyph, height):
    """The glyph turned 180 degrees inside the box its pixels use."""
    width, pixels = glyph
    lit = [(x, y) for y in range(height) for x in range(width) if pixels[y * width + x]]
    left = min(x for x, _ in lit)
    right = max(x for x, _ in lit)
    top = min(y for _, y in lit)
    bottom = max(y for _, y in lit)
    out = bytearray(width * height)
    for x, y in lit:
        out[(bottom + top - y) * width + (right + left - x)] = pixels[y * width + x]
    return width, bytes(out)


def capital_of(character):
    """The capital a character falls back to: its upper case, then without the accent."""
    text = chr(character).upper()
    if len(text) == 1 and ord(text) in ACCENTED:
        return ord(text)
    base = unicodedata.normalize("NFD", text)[0]
    return ord(base) if len(base) == 1 else None


def extend(font, font8, outline):
    if font["last"] >= 255:
        return False
    if font["first"] > 32 or font["last"] < 95:
        raise SystemExit("unexpected font range")
    glyphs = font["glyphs"]
    width = glyphs[ord("A")][0]
    height = font["height"]
    ink = max(glyphs[ord("A")][1])
    for character, (base, mark, keep) in ACCENTED.items():
        glyphs[character] = (width, render(accented_shape(font8, base, mark, keep), width,
                                           height, outline, ink))
    for character, mark in INVERTED.items():
        glyphs[character] = upside_down(glyphs[ord(mark)], height)
    for character in range(font["last"] + 1, 256):
        if character in glyphs:
            continue
        capital = capital_of(character)
        if capital is not None and capital != character and capital in glyphs:
            glyphs[character] = glyphs[capital]
        else:
            glyphs[character] = (width, bytes(width * height))
    font["last"] = 255
    return True


def read_png(path):
    """RGBA rows of an 8-bit, non-interlaced PNG."""
    data = path.read_bytes()
    at, idat, palette, alpha = 8, b"", b"", b""
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        chunk = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"PLTE":
            palette = chunk
        elif kind == b"tRNS":
            alpha = chunk
        elif kind == b"IDAT":
            idat += chunk
    if depth != 8 or interlace:
        raise SystemExit(f"{path}: unsupported PNG")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    raw = zlib.decompress(idat)
    stride = width * channels
    previous = bytearray(stride)
    pixels = []
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                guess = a + b - c
                pa, pb, pc = abs(guess - a), abs(guess - b), abs(guess - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        row = []
        for x in range(width):
            value = line[x * channels:(x + 1) * channels]
            if color == 3:
                index = value[0]
                row.append(tuple(palette[index * 3:index * 3 + 3]) +
                           (alpha[index] if index < len(alpha) else 255,))
            elif color == 2:
                row.append(tuple(value) + (255,))
            elif color == 6:
                row.append(tuple(value))
            elif color == 0:
                row.append((value[0],) * 3 + (255,))
            else:
                row.append((value[0],) * 3 + (value[1],))
        pixels.append(row)
        previous = line
    return pixels


def write_png(path, pixels):
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
    height, width = len(pixels), len(pixels[0])
    raw = b"".join(b"\0" + bytes(v for p in row for v in p) for row in pixels)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


WHITE = (255, 255, 255)
RED = (184, 51, 31)
GREEN = (75, 145, 24)
BLUE = (75, 129, 195)


def draw_text(pixels, font, x, y, text, scale_x, scale_y, colors=None):
    """Draws `text` (upper case, Latin-1) at x, y; each font pixel a scale_x by scale_y box,
    blended by how much of each picture pixel it covers, as the original was scaled.
    `colors` gives one colour per character (default white)."""
    height = font["height"]
    advance = font["glyphs"][ord("A")][0] * scale_x
    cover = {}
    for index, character in enumerate(text):
        width, glyph = font["glyphs"].get(ord(character), font["glyphs"][ord(" ")])
        color = colors[index] if colors else WHITE
        left = x + index * advance
        for gy in range(height):
            for gx in range(width):
                if not glyph[gy * width + gx]:
                    continue
                x0, x1 = left + gx * scale_x, left + (gx + 1) * scale_x
                y0, y1 = y + gy * scale_y, y + (gy + 1) * scale_y
                for py in range(int(y0), int(y1 - 1e-9) + 1):
                    for px in range(int(x0), int(x1 - 1e-9) + 1):
                        area = ((min(x1, px + 1) - max(x0, px)) *
                                (min(y1, py + 1) - max(y0, py)))
                        if area > 0:
                            amount, _ = cover.get((px, py), (0.0, color))
                            cover[(px, py)] = (min(1.0, amount + area), color)
    for (px, py), (amount, color) in cover.items():
        if 0 <= py < len(pixels) and 0 <= px < len(pixels[0]):
            pixels[py][px] = tuple(round(c * amount) for c in color) + (255,)


def colored(text, words):
    """One colour per character: white, and `words` (word -> colour) wherever they appear."""
    colors = [WHITE] * len(text)
    for word, color in words.items():
        start = text.find(word)
        while start >= 0:
            colors[start:start + len(word)] = [color] * len(word)
            start = text.find(word, start + 1)
    return colors


def wrap(text, columns):
    lines, line = [], ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > columns:
            lines.append(line)
            line = word
        else:
            line = f"{line} {word}" if line else word
    return lines + [line]


STORY_TITLE = "LA LEYENDA DE ZELDA"
STORY = ("HACE MUCHO TIEMPO, GANON, PRÍNCIPE DE LAS TINIEBLAS, ROBÓ LA TRIFUERZA DEL "
         "PODER. LA PRINCESA ZELDA DE HYRULE DIVIDIÓ LA TRIFUERZA DE LA SABIDURÍA EN OCHO "
         "FRAGMENTOS Y LOS ESCONDIÓ DE GANON ANTES DE SER SECUESTRADA POR SUS ESBIRROS. "
         "LINK, DEBES ENCONTRAR LOS FRAGMENTOS Y SALVAR A ZELDA.")
STORY_WORDS = {"GANON": RED, "ZELDA": BLUE, "LINK": GREEN}


def story_page(source, font):
    """ZSTORY, 400x240: the vine frame kept; the title and the lines of the original at
    font pixels of 1.32 x 1.43 (8x7 letters as about 10.5 x 10), body from x 33, y 56."""
    pixels = read_png(source)
    for y in range(24, 212):
        for x in range(30, 368):
            r, g, b, _ = pixels[y][x]
            vine = g > r + 30 and g > b + 30
            if not vine or (36 <= x < 362 and 24 <= y < 208):
                pixels[y][x] = (0, 0, 0, 255)
    scale_x, scale_y = 1.32, 1.43
    advance = 8 * scale_x
    title_x = 200 - len(STORY_TITLE) * advance / 2
    draw_text(pixels, font, title_x, 26, STORY_TITLE, scale_x, scale_y,
              [RED] * len(STORY_TITLE))
    lines = wrap(STORY, int((366 - 33) // advance))
    pitch = min(17.4, (212 - 56) / len(lines))
    for index, line in enumerate(lines):
        draw_text(pixels, font, 33, round(56 + index * pitch), line, scale_x, scale_y,
                  colored(line, STORY_WORDS))
    return pixels


# ENDR, 256x166, the font at 1x: each line's top, and whether it starts at x 33 or is
# centred on the picture's middle text column (x 125.5), as in the original.
ENDING = [
    (30, None, "GRACIAS, LINK. ERES"),
    (46, None, "EL HÉROE DE HYRULE."),
    (94, 125.5, "POR FIN,"),
    (110, 125.5, "LA PAZ VUELVE A HYRULE."),
    (134, 125.5, "AQUÍ TERMINA LA HISTORIA."),
]


def ending_page(source, font):
    pixels = read_png(source)
    width = len(pixels[0])
    for top, _, _ in ENDING:
        for y in range(top, top + 7):
            for x in range(width):
                pixels[y][x] = (0, 0, 0, pixels[y][x][3])
    for top, centre, text in ENDING:
        x = 33 if centre is None else round(centre - len(text) * 4)
        draw_text(pixels, font, x, top, text, 1, 1)
    return pixels


def make_pages(package, font):
    pages = (("ZSTORY", story_page), ("ENDR", ending_page))
    for name, make in pages:
        source = package / "graphics" / f"{name}.png"
        if not source.exists():
            continue
        target = package / "localized" / "textures" / f"{name}.es.png"
        write_png(target, make(source, font))
        print(f"spanish-game: {target.relative_to(package)}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    package = Path(sys.argv[1])
    font8 = load(package / "SMALLFNT")
    for name, outline in (("SMALLFNT", False), ("BIGFONT", False), ("ZeldFNT2", True)):
        path = package / name
        if not path.exists():
            continue
        font = load(path)
        if extend(font, font8, outline):
            save(path, font)
            if load(path)["glyphs"] != font["glyphs"]:
                raise SystemExit(f"{name}: round trip failed")
        print(f"spanish-game: {name} {font['first']}-{font['last']}")
    make_pages(package, load(package / "SMALLFNT"))


if __name__ == "__main__":
    main()
