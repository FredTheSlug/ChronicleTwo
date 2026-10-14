#!/usr/bin/env python3
"""Draws a starter glyphs/ folder: one atlas per controller style and glyphs.json.

The game pastes these symbols over the PS2 ones in its own textures (docs/GLYPHS.md). The art is
deliberately plain; replace any atlas with your own and keep or edit the rectangles in glyphs.json.

    python3 tools/glyphs/make_glyphs.py <output folder> [--font <ttf>]
"""

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL = 64  # Kazarov's glyphs::kReference: a round face button.
OUTLINE = (24, 20, 28, 255)
WHITE = (240, 240, 240, 255)
DARK = (52, 52, 60, 255)


def font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default(size)


class Atlas:
    def __init__(self, columns, font_path):
        self.columns = columns
        self.cells = {}
        self.items = []
        self.font_path = font_path

    def add(self, name, width, draw):
        self.items.append((name, width, draw))

    def render(self):
        x = y = 0
        row_width = self.columns * CELL
        placed = []
        for name, width, draw in self.items:
            if x + width > row_width:
                x, y = 0, y + CELL
            placed.append((name, x, y, width, draw))
            x += width
        image = Image.new("RGBA", (row_width, y + CELL), (0, 0, 0, 0))
        for name, x, y, width, draw in placed:
            cell = Image.new("RGBA", (width, CELL), (0, 0, 0, 0))
            draw(cell, self)
            image.alpha_composite(cell, (x, y))
            # Tight rectangles: the game fits each symbol to the cell it replaces.
            left, top, right, bottom = cell.getbbox() or (0, 0, width, CELL)
            self.cells[name] = [x + left, y + top, right - left, bottom - top]
        return image


def centred_text(draw, box, text, size, fill, atlas, stroke=0):
    face = font(atlas.font_path, size)
    left, top, right, bottom = draw.textbbox((0, 0), text, font=face, stroke_width=stroke)
    x = box[0] + (box[2] - box[0] - (right - left)) / 2 - left
    y = box[1] + (box[3] - box[1] - (bottom - top)) / 2 - top
    draw.text((x, y), text, font=face, fill=fill, stroke_width=stroke, stroke_fill=OUTLINE)


def face_button(letter, colour, letter_colour=WHITE):
    def draw(cell, atlas):
        d = ImageDraw.Draw(cell)
        d.ellipse((2, 2, CELL - 3, CELL - 3), fill=OUTLINE)
        d.ellipse((6, 6, CELL - 7, CELL - 7), fill=colour)
        centred_text(d, (6, 6, CELL - 7, CELL - 7), letter, 36, letter_colour, atlas)
    return draw


def shoulder(text, wide=False):
    def draw(cell, atlas):
        d = ImageDraw.Draw(cell)
        w = cell.width
        radius = 14 if not wide else 22
        d.rounded_rectangle((2, 10, w - 3, CELL - 11), radius=radius, fill=OUTLINE)
        d.rounded_rectangle((6, 14, w - 7, CELL - 15), radius=radius - 4, fill=DARK)
        centred_text(d, (6, 14, w - 7, CELL - 15), text, 28, WHITE, atlas)
    return draw


def pill(symbol):
    def draw(cell, atlas):
        d = ImageDraw.Draw(cell)
        w = cell.width
        d.rounded_rectangle((2, 12, w - 3, CELL - 13), radius=20, fill=OUTLINE)
        d.rounded_rectangle((6, 16, w - 7, CELL - 17), radius=16, fill=DARK)
        symbol(d, (6, 16, w - 7, CELL - 17), atlas)
    return draw


def menu_symbol(d, box, atlas):
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    for dy in (-7, 0, 7):
        d.rectangle((cx - 10, cy + dy - 2, cx + 10, cy + dy + 1), fill=WHITE)


def view_symbol(d, box, atlas):
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    d.rectangle((cx - 10, cy - 8, cx + 4, cy + 3), outline=WHITE, width=3)
    d.rectangle((cx - 4, cy - 3, cx + 10, cy + 8), fill=DARK, outline=WHITE, width=3)


def text_symbol(text, size=30):
    def symbol(d, box, atlas):
        centred_text(d, box, text, size, WHITE, atlas)
    return symbol


def label(text):
    # A word drawn like the gaiji sheet's SELECT and START: white, dark outline.
    def draw(cell, atlas):
        d = ImageDraw.Draw(cell)
        centred_text(d, (0, 0, cell.width, CELL), text, 40, WHITE, atlas, stroke=4)
    return draw


def keycap(text):
    def draw(cell, atlas):
        d = ImageDraw.Draw(cell)
        w = cell.width
        d.rounded_rectangle((2, 4, w - 3, CELL - 5), radius=10, fill=OUTLINE)
        d.rounded_rectangle((5, 7, w - 6, CELL - 10), radius=8, fill=(236, 236, 230, 255))
        size = 32 if len(text) <= 2 else 22
        centred_text(d, (5, 7, w - 6, CELL - 10), text, size, OUTLINE, atlas)
    return draw


def mouse(button):
    def draw(cell, atlas):
        d = ImageDraw.Draw(cell)
        d.rounded_rectangle((14, 4, 50, 60), radius=17, fill=OUTLINE)
        d.rounded_rectangle((17, 7, 47, 57), radius=14, fill=(236, 236, 230, 255))
        d.line((32, 7, 32, 28), fill=OUTLINE, width=2)
        d.line((17, 28, 47, 28), fill=OUTLINE, width=2)
        hot = {1: (18, 8, 31, 27), 2: (33, 8, 46, 27), 3: (29, 10, 35, 24)}.get(button)
        if hot:
            d.rectangle(hot, fill=(214, 64, 52, 255))
    return draw


XBOX_COLOURS = {"A": (96, 172, 64, 255), "B": (210, 60, 52, 255), "X": (58, 120, 214, 255), "Y": (232, 184, 40, 255)}


def xbox_like(atlas, letters=XBOX_COLOURS, coloured=True):
    # By position: the PS2's cross is the bottom button, circle the right, square the left, triangle the top.
    for glyph, letter in (("cross", "A"), ("circle", "B"), ("square", "X"), ("triangle", "Y")):
        atlas.add(glyph, CELL, face_button(letter, letters[letter] if coloured else DARK,
                                           WHITE if coloured else letters[letter]))
    for glyph, text in (("l1", "LB"), ("r1", "RB"), ("l2", "LT"), ("r2", "RT")):
        atlas.add(glyph, 96, shoulder(text))
    atlas.add("start", 96, pill(menu_symbol))
    atlas.add("select", 96, pill(view_symbol))
    atlas.add("start_label", 160, label("MENU"))
    atlas.add("select_label", 160, label("VIEW"))


def switch(atlas):
    grey = {letter: DARK for letter in "ABXY"}
    for glyph, letter in (("cross", "B"), ("circle", "A"), ("square", "Y"), ("triangle", "X")):
        atlas.add(glyph, CELL, face_button(letter, grey[letter]))
    for glyph, text in (("l1", "L"), ("r1", "R"), ("l2", "ZL"), ("r2", "ZR")):
        atlas.add(glyph, 96, shoulder(text))
    atlas.add("start", CELL, face_button("+", DARK))
    atlas.add("select", CELL, face_button("−", DARK))
    atlas.add("start_label", 160, label("PLUS"))
    atlas.add("select_label", 180, label("MINUS"))


def steam_deck(atlas):
    xbox_like(atlas, coloured=False, letters={letter: WHITE for letter in "ABXY"})
    atlas.items = [item for item in atlas.items if item[0] not in ("l1", "r1", "l2", "r2")]
    for glyph, text in (("l1", "L1"), ("r1", "R1"), ("l2", "L2"), ("r2", "R2")):
        atlas.add(glyph, 96, shoulder(text))


KEYS = {
    **{chr(c): chr(c).upper() for c in range(ord("a"), ord("z") + 1)},
    **{str(n): str(n) for n in range(10)},
    "space": "Space", "return": "Enter", "escape": "Esc", "backspace": "Back", "tab": "Tab",
    "leftshift": "Shift", "rightshift": "Shift", "leftctrl": "Ctrl", "rightctrl": "Ctrl",
    "leftalt": "Alt", "rightalt": "Alt", "up": "↑", "down": "↓", "left": "←", "right": "→",
    **{f"f{n}": f"F{n}" for n in range(1, 13)},
}


def keyboard(atlas):
    for name, text in KEYS.items():
        atlas.add(name, CELL if len(text) <= 2 else 112, keycap(text))
    for button in (1, 2, 3):
        atlas.add(f"mouse{button}", CELL, mouse(button))


PS_COLOURS = {"cross": (124, 178, 232, 255), "circle": (230, 92, 92, 255), "square": (214, 130, 196, 255),
              "triangle": (64, 200, 168, 255)}


def ps_shape(shape, colour, disc=DARK):
    # A PlayStation face button: the shape in its colour (or white) on a dark disc.
    def draw(cell, atlas):
        d = ImageDraw.Draw(cell)
        d.ellipse((2, 2, CELL - 3, CELL - 3), fill=OUTLINE)
        d.ellipse((6, 6, CELL - 7, CELL - 7), fill=disc)
        c, w = CELL / 2, 6
        if shape == "cross":
            d.line((c - 12, c - 12, c + 12, c + 12), fill=colour, width=w)
            d.line((c - 12, c + 12, c + 12, c - 12), fill=colour, width=w)
        elif shape == "circle":
            d.ellipse((c - 13, c - 13, c + 13, c + 13), outline=colour, width=w)
        elif shape == "square":
            d.rectangle((c - 11, c - 11, c + 11, c + 11), outline=colour, width=w)
        else:
            d.polygon((c, c - 15, c + 14, c + 10, c - 14, c + 10), outline=colour, width=w)
    return draw


def playstation(start, select, coloured):
    def build(atlas):
        for glyph in ("cross", "circle", "square", "triangle"):
            atlas.add(glyph, CELL, ps_shape(glyph, PS_COLOURS[glyph] if coloured else WHITE))
        for glyph, text in (("l1", "L1"), ("r1", "R1"), ("l2", "L2"), ("r2", "R2")):
            atlas.add(glyph, 96, shoulder(text))
        atlas.add("start", 96, pill(menu_symbol))
        atlas.add("select", 96, pill(view_symbol if select == "CREATE" else text_symbol("\u25cf", 20)))
        atlas.add("start_label", 220, label(start))
        atlas.add("select_label", 220, label(select))
    return build


STYLES = {
    "ps4": playstation("OPTIONS", "SHARE", True),
    "ps5": playstation("OPTIONS", "CREATE", False),
    "ps5color": playstation("OPTIONS", "CREATE", True),
    "xbox": xbox_like,
    "steamcontroller": xbox_like,
    "steamdeck": steam_deck,
    "switch": switch,
    "keyboard": keyboard,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--font", default="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    index = {"styles": {}}
    for name, build in STYLES.items():
        atlas = Atlas(12 if name == "keyboard" else 8, args.font)
        build(atlas)
        image = atlas.render()
        image.save(args.output / f"{name}.png")
        index["styles"][name] = {"atlas": f"{name}.png", "glyphs": atlas.cells}
    (args.output / "glyphs.json").write_text(json.dumps(index, indent=1) + "\n")


if __name__ == "__main__":
    main()
