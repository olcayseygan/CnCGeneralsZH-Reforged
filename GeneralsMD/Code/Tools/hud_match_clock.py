"""The match clock's pieces (GameClient/MatchClock.cpp): a steel tray at the top of the screen and the
three marks that ride its cycle bar.

ReforgedMatchClock.tga, drawn at 1920x1080 pixel sizes:
    the tray, the control bar's steel: a rim, a lit top edge, a dark face, and along its foot an empty
        trough the code fills with the day's phases and slides a mark along
    the sun, a disc with short rays
    the moon, a crescent
    the blood moon, the moon's disc full and red
ReforgedMatchClock.ini maps every piece.  No word anywhere: the clock's digits are the game's own font.

    python Tools/hud_match_clock.py     # writes Data/Art/Textures/ReforgedMatchClock.tga and the INI

Drawn eight times too big and brought down, which is the antialiasing (recon_marks.py does the same)."""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
TEXTURES = os.path.join(HERE, "..", "Data", "Art", "Textures")
MAPPED = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated")

UP = 8
STEEL_DK = (20, 28, 36)
STEEL = (112, 132, 150)
STEEL_HI = (200, 214, 226)
SUN = (255, 214, 110)
MOON = (214, 224, 240)
BLOOD = (220, 40, 30)

SHEET_W, SHEET_H = 256, 128
TRAY = (0, 0, 240, 52)          # x, y, w, h; MatchClock.cpp's TRAY_W and TRAY_H
TROUGH = (12, 34, 216, 10)      # inside the tray: MatchClock.cpp's BAR_* lengths
MARK = 28                       # a mark's cell
MARKS = ("Sun", "Moon", "BloodMoon")
MARK_Y = 64


def keylined(rgb, white, blur, alpha):
    """a coloured shape with a soft dark edge round it"""
    edge = white.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: min(255, v * 3) * alpha // 255)
    a = ImageChops.lighter(white, edge)
    r, g, b = (white.point(lambda v, c=c: v * c // 255) for c in rgb)
    return Image.merge("RGBA", (r, g, b, a))


def tray():
    w, h = TRAY[2], TRAY[3]
    img = Image.new("RGBA", (w * UP, h * UP), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rim = 2 * UP
    d.rounded_rectangle([0, 0, w * UP - 1, h * UP - 1], radius=4 * UP, fill=STEEL + (235,))
    d.rounded_rectangle([rim, rim, w * UP - 1 - rim, h * UP - 1 - rim], radius=3 * UP, fill=STEEL_DK + (225,))
    d.rectangle([rim + 3 * UP, rim, w * UP - 1 - rim - 3 * UP, rim + 2 * UP - 1], fill=STEEL_HI + (190,))
    x, y, tw, th = (v * UP for v in TROUGH)
    d.rectangle([x, y, x + tw - 1, y + th - 1], fill=STEEL + (210,))
    d.rectangle([x + UP, y + UP, x + tw - 1 - UP, y + th - 1 - UP], fill=(0, 0, 0, 220))
    return img.resize((w, h), Image.LANCZOS)


def mark(kind):
    n = MARK * UP
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    c = n / 2
    if kind == "Sun":
        r = n * 0.22
        d.ellipse([c - r, c - r, c + r, c + r], fill=255)
        for i in range(8):
            t = 2 * math.pi * i / 8
            d.line([(c + r * 1.35 * math.cos(t), c + r * 1.35 * math.sin(t)),
                    (c + r * 1.85 * math.cos(t), c + r * 1.85 * math.sin(t))], fill=255, width=round(2.2 * UP))
        colour = SUN
    elif kind == "Moon":
        r = n * 0.34
        d.ellipse([c - r, c - r, c + r, c + r], fill=255)
        d.ellipse([c - r + r * 0.55, c - r - r * 0.25, c + r + r * 0.55, c + r - r * 0.25], fill=0)
        colour = MOON
    else:
        r = n * 0.34
        d.ellipse([c - r, c - r, c + r, c + r], fill=255)
        colour = BLOOD
    return keylined(colour, m, 1.4 * UP, 190).resize((MARK, MARK), Image.LANCZOS)


def sheet():
    img = Image.new("RGBA", (SHEET_W, SHEET_H), (0, 0, 0, 0))
    img.paste(tray(), TRAY[:2])
    for index, kind in enumerate(MARKS):
        img.paste(mark(kind), (index * MARK, MARK_Y))
    return img


def mapped(name, box):
    x, y, w, h = box
    return ("MappedImage %s\n  Texture = ReforgedMatchClock.tga\n  TextureWidth = %d\n  TextureHeight = %d\n"
            "  Coords = Left:%d Top:%d Right:%d Bottom:%d\n  Status = NONE\nEnd\n"
            % (name, SHEET_W, SHEET_H, x, y, x + w, y + h))


def ini():
    blocks = [mapped("ReforgedMatchClockTray", TRAY)]
    blocks += [mapped("ReforgedMatchClock%s" % kind, (i * MARK, MARK_Y, MARK, MARK)) for i, kind in enumerate(MARKS)]
    head = ("; The match clock's pieces (GameClient/MatchClock.cpp).  Written by Tools/hud_match_clock.py\n"
            "; with Art/Textures/ReforgedMatchClock.tga; do not edit by hand.  Mapped images are not in the\n"
            "; multiplayer INI checksum.\n\n")
    return head + "\n".join(blocks)


def main():
    path = os.path.join(TEXTURES, "ReforgedMatchClock.tga")
    sheet().save(path)
    print(path)
    path = os.path.join(MAPPED, "ReforgedMatchClock.ini")
    with open(path, "w", newline="\r\n") as f:
        f.write(ini())
    print(path)


if __name__ == "__main__":
    main()
