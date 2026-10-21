#!/usr/bin/env python3
# Draws the controller button glyphs the SwitchControls add-on shows (Tools/Switch/romfs/AddOns/SwitchControls/Media):
# 64x64 32-bit TGA files, which WoW 3.3.5 loads from add-on folders and shows inline in text with |T...|t.
# Run it again after changing a glyph; the TGA files are committed.

import math
import os
import struct
import sys

SIZE = 64
SAMPLES = 4

# 5x7 pixel font for the few characters the glyphs use.
FONT = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
}

BACK = (48, 48, 52, 235)
EDGE = (200, 200, 205, 255)
INK = (255, 255, 255, 255)
DIM = (110, 110, 118, 255)


def blend(dst, src, coverage):
    a = src[3] / 255.0 * coverage
    out = tuple(int(round(src[i] * a + dst[i] * (1 - a))) for i in range(3))
    alpha = int(round(255 * (a + dst[3] / 255.0 * (1 - a))))
    return out + (alpha,)


class Image:
    def __init__(self):
        self.pixels = [[(0, 0, 0, 0)] * SIZE for _ in range(SIZE)]

    def shape(self, inside, color):
        # Supersampled coverage, so edges are smooth.
        for y in range(SIZE):
            for x in range(SIZE):
                hits = 0
                for sy in range(SAMPLES):
                    for sx in range(SAMPLES):
                        if inside(x + (sx + 0.5) / SAMPLES, y + (sy + 0.5) / SAMPLES):
                            hits += 1
                if hits:
                    self.pixels[y][x] = blend(self.pixels[y][x], color, hits / float(SAMPLES * SAMPLES))

    def circle(self, cx, cy, r, color):
        self.shape(lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r, color)

    def rect(self, x0, y0, x1, y1, color):
        self.shape(lambda x, y: x0 <= x <= x1 and y0 <= y <= y1, color)

    def round_rect(self, x0, y0, x1, y1, r, color):
        def inside(x, y):
            cx = min(max(x, x0 + r), x1 - r)
            cy = min(max(y, y0 + r), y1 - r)
            return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
        self.shape(inside, color)

    def triangle(self, points, color):
        (ax, ay), (bx, by), (cx, cy) = points

        def side(px, py, qx, qy, x, y):
            return (qx - px) * (y - py) - (qy - py) * (x - px)

        def inside(x, y):
            d1 = side(ax, ay, bx, by, x, y)
            d2 = side(bx, by, cx, cy, x, y)
            d3 = side(cx, cy, ax, ay, x, y)
            return (d1 >= 0 and d2 >= 0 and d3 >= 0) or (d1 <= 0 and d2 <= 0 and d3 <= 0)

        self.shape(inside, color)

    def text(self, chars, pixel, color, cy=SIZE / 2):
        width = len(chars) * 5 * pixel + (len(chars) - 1) * pixel
        left = (SIZE - width) / 2.0
        top = cy - 7 * pixel / 2.0

        # One shape for the whole label, so neighboring font pixels join without seams.
        def inside(x, y):
            row = int((y - top) // pixel)
            column = int((x - left) // pixel)

            if row < 0 or row >= 7 or column < 0:
                return False

            index, col = divmod(column, 6)

            if index >= len(chars) or col >= 5:
                return False

            return FONT[chars[index]][row][col] == "1"

        self.shape(inside, color)

    def save(self, path):
        with open(path, "wb") as f:
            # Uncompressed true color, top-left origin, 8 alpha bits.
            f.write(struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, SIZE, SIZE, 32, 0x28))
            for row in self.pixels:
                for r, g, b, a in row:
                    f.write(struct.pack("BBBB", b, g, r, a))


def face(label):
    image = Image()
    image.circle(32, 32, 30, EDGE)
    image.circle(32, 32, 27, BACK)
    image.text(label, 5 if len(label) == 1 else 4, INK)
    return image


def shoulder(label):
    image = Image()
    image.round_rect(2, 12, 61, 51, 12, EDGE)
    image.round_rect(5, 15, 58, 48, 9, BACK)
    image.text(label, 4, INK)
    return image


def dpad(direction):
    image = Image()
    image.circle(32, 32, 30, EDGE)
    image.circle(32, 32, 27, BACK)
    arms = {
        "Up": [(32, 9), (22, 24), (42, 24)],
        "Down": [(32, 55), (22, 40), (42, 40)],
        "Left": [(9, 32), (24, 22), (24, 42)],
        "Right": [(55, 32), (40, 22), (40, 42)],
    }
    for name, points in arms.items():
        image.triangle(points, INK if name == direction or direction == "All" else DIM)
    return image


def stick(label, click):
    image = Image()
    image.circle(32, 32, 30, EDGE)
    image.circle(32, 32, 27, BACK)
    image.circle(32, 32, 19, EDGE if click else DIM)
    image.circle(32, 32, 16, BACK)
    image.text(label, 3, INK)
    return image


def main(folder):
    glyphs = {
        "A": face("A"),
        "B": face("B"),
        "X": face("X"),
        "Y": face("Y"),
        "Plus": face("+"),
        "Minus": face("-"),
        "L": shoulder("L"),
        "R": shoulder("R"),
        "ZL": shoulder("ZL"),
        "ZR": shoulder("ZR"),
        "DpadUp": dpad("Up"),
        "DpadDown": dpad("Down"),
        "DpadLeft": dpad("Left"),
        "DpadRight": dpad("Right"),
        "Dpad": dpad("All"),
        "LStick": stick("L", False),
        "RStick": stick("R", False),
        "LStickClick": stick("L", True),
        "RStickClick": stick("R", True),
    }

    for name, image in glyphs.items():
        image.save(os.path.join(folder, name + ".tga"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "romfs/AddOns/SwitchControls/Media"))
