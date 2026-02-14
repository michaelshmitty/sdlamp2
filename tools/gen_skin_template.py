#!/usr/bin/env python3
"""
gen_skin_template.py — Generate a skin template PNG for sdlamp2.

Creates a 642x420 PNG showing the sprite grid layout with labeled gutters.
Each 200x200 cell is transparent (ready to draw on). The 20px gutters between
cells are a bright magenta so they're clearly distinguishable from content areas.

Grid layout:
  Col 0 (0-199)    Col 1 (220-419)   Col 2 (440-639)
  Row 0: REWIND     PLAY              FF
         ----gutter y=200-219 with labels----
  Row 1: STOP       PREV              NEXT

Two extra pixels on the right (640-641) are gutter fill to reach 642px width,
matching the spritesheet dimensions.

Usage: python3 tools/gen_skin_template.py [output.png]

Requires Pillow.
"""
import sys

from PIL import Image, ImageDraw, ImageFont

CELL = 200
GAP = 20
COLS = 3
ROWS = 2
WIDTH = COLS * CELL + (COLS - 1) * GAP + 2   # 642
HEIGHT = ROWS * CELL + (ROWS - 1) * GAP       # 420

CELL_COLOR = (0, 0, 0, 0)
GUTTER_COLOR = (255, 0, 255, 255)
TEXT_COLOR = (255, 255, 255, 255)

LABELS = [
    ["REWIND", "PLAY", "FF"],
    ["STOP", "PREV", "NEXT"],
]


def cell_x(col):
    return col * (CELL + GAP)


def cell_y(row):
    return row * (CELL + GAP)


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else "skin_template.png"

    img = Image.new("RGBA", (WIDTH, HEIGHT), GUTTER_COLOR)
    draw = ImageDraw.Draw(img)

    # Try to load a small font for labels
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
    except OSError:
        try:
            font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 11)
        except OSError:
            font = ImageFont.load_default()

    # Draw white cells
    for row in range(ROWS):
        for col in range(COLS):
            x = cell_x(col)
            y = cell_y(row)
            draw.rectangle([x, y, x + CELL - 1, y + CELL - 1], fill=CELL_COLOR)

    # Label the horizontal gutter (y = 200..219)
    gutter_y = CELL  # 200
    for col in range(COLS):
        cx = cell_x(col) + CELL // 2

        # Row 0 label above center of gutter
        label_0 = LABELS[0][col]
        bbox = draw.textbbox((0, 0), label_0, font=font)
        tw = bbox[2] - bbox[0]
        draw.text((cx - tw // 2, gutter_y + 1), label_0, fill=TEXT_COLOR, font=font)

        # Row 1 label below center of gutter
        label_1 = LABELS[1][col]
        bbox = draw.textbbox((0, 0), label_1, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        draw.text((cx - tw // 2, gutter_y + GAP - th - 2), label_1, fill=TEXT_COLOR, font=font)

    img.save(output_path)
    print(f"Skin template saved to {output_path} ({WIDTH}x{HEIGHT})")


if __name__ == "__main__":
    main()
