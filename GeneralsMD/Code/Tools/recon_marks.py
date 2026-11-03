"""The SIHA recon marks of GameClient/RadarContactMarks.cpp, as the approved still recon_v1 has them.

ReforgedReconArea.tga is the ground decal of a recon area: a dashed ring with range ticks pointing in,
a long one every quarter.  The game tints it green and stretches it to the area's diameter, so it is
white where the colour goes, with a soft dark edge that lifts it off bright sand.

ReforgedReconHud.tga is the 2D sheet, drawn at 1920x1080 pixel sizes times two:
    the six class glyphs (RadarGlyph order), white on a dark edge, tinted amber over a recon chip
        and red beside a classified radar ring
    the chip, a small steel plate the glyph sits on where a health bar would be
    the tray under a recon area, steel with an empty trough for the countdown bar
    the bar, flat green, stretched across the trough as the time runs out
ReforgedReconHud.ini maps every piece.  No national symbol anywhere.

    python Tools/recon_marks.py      # writes Data/Art/Textures/ReforgedRecon*.tga and the INI

Drawn eight times too big and brought down, which is the antialiasing (radar_marks.py does the same)."""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
TEXTURES = os.path.join(HERE, "..", "Data", "Art", "Textures")
MAPPED = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated")

UP = 8                      # drawn this many times the shipped size
STEEL_DK = (20, 28, 36)
STEEL = (112, 132, 150)
STEEL_HI = (200, 214, 226)
GREEN = (90, 210, 120)

# the sheet, in shipped texels
SHEET_W, SHEET_H = 512, 128
GLYPH = 32                  # a glyph cell, the six side by side from the top left
CHIP = (320, 0, 64, 48)     # x, y, w, h: 32x24 on a 1080 screen; every piece a few texels clear of the next
TRAY = (0, 48, 264, 64)     # 132x32 on a 1080 screen
TROUGH = (14, 42, 236, 12)  # inside the tray: the bar's dark slot
BAR_BLOCK = (272, 48, 32, 16)   # the bar's green, padded so a stretch never samples its neighbours
BAR = (280, 52, 16, 8)          # the part the INI maps
GLYPHS = ("Infantry", "Vehicle", "AntiAir", "Artillery", "Aircraft", "Defence")

# the area decal
AREA = 512
AREA_UP = 4                 # a 512 decal at eight times is more pixels than its edge needs
AREA_RADIUS = 240           # of 256: the dashed ring's centre line, its keyline clear of the edge.
                            # RadarContactLook::AreaDecalSpan (256/240) lays it on the area's radius
AREA_STROKE = 3.2
DASHES = 48                 # twice the ticks
TICKS = 24
TICK_SHORT, TICK_LONG = 7, 14


def keylined(white, blur, alpha):
    """white shapes with a soft dark edge: RGBA whose colour is white on the shape and black round it.
    The edge is the shape blurred and pushed up, a dilation that stays fast on a sheet drawn at UP."""
    edge = white.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: min(255, v * 3) * alpha // 255)
    a = ImageChops.lighter(white, edge)
    return Image.merge("RGBA", (white, white, white, a))


def glyph(kind):
    """one class pictogram on a 32 texel cell, drawn at UP"""
    n = GLYPH * UP
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    s = n / 24.0            # the pictogram on a 24 unit grid, centred at 12,12

    def P(x, y):
        return (x * s, y * s)

    def rect(x0, y0, x1, y1):
        d.rectangle([P(x0, y0), P(x1, y1)], fill=255)

    def line(pts, w):
        d.line([P(x, y) for x, y in pts], fill=255, width=round(w * s), joint="curve")

    def disc(x, y, r):
        d.ellipse([P(x - r, y - r), P(x + r, y + r)], fill=255)

    if kind == "Infantry":
        disc(12, 5.2, 2.6)
        d.polygon([P(8.4, 8.6), P(15.6, 8.6), P(14.4, 15), P(9.6, 15)], fill=255)
        line([(10.6, 15), (9.4, 20.5)], 2.0)
        line([(13.4, 15), (14.6, 20.5)], 2.0)
    elif kind == "Vehicle":           # a tank from the side
        d.rounded_rectangle([P(3, 12), P(21, 18)], radius=3 * s, fill=255)
        rect(7.5, 8, 15, 12)
        line([(15, 9.8), (22.5, 9.8)], 1.8)
    elif kind == "AntiAir":           # a hull with two barrels raised
        rect(3, 14, 21, 18.5)
        rect(8.5, 10.5, 15.5, 14)
        line([(10.5, 11), (5.5, 4)], 1.8)
        line([(13.5, 11), (18.5, 4)], 1.8)
    elif kind == "Artillery":         # a long barrel high over a low hull
        rect(3, 14.5, 21, 18.5)
        rect(4.5, 10.5, 11.5, 14.5)
        line([(9, 12), (21.5, 4.5)], 2.0)
    elif kind == "Aircraft":          # top down, swept wings
        line([(12, 2.5), (12, 21)], 2.6)
        d.polygon([P(12, 7.5), P(22, 14), P(22, 15.5), P(12, 12.5), P(2, 15.5), P(2, 14)], fill=255)
        d.polygon([P(12, 17.5), P(16.5, 21), P(16.5, 22), P(12, 20.5), P(7.5, 22), P(7.5, 21)], fill=255)
    elif kind == "Defence":           # a bunker with its gun out
        d.chord([P(4, 7), P(18, 21)], 180, 360, fill=255)
        rect(2.5, 14, 19.5, 18.5)
        line([(16, 11), (22.5, 11)], 2.0)
    return keylined(m, 1.2 * UP, 170).resize((GLYPH, GLYPH), Image.LANCZOS)


def plate(w, h, trough=None):
    """a steel plate at UP: dark fill, steel rim, light top edge, dark bottom edge, as the control bar's"""
    img = Image.new("RGBA", (w * UP, h * UP), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rim = 2 * UP
    d.rectangle([0, 0, w * UP - 1, h * UP - 1], fill=STEEL + (235,))
    d.rectangle([rim, rim, w * UP - 1 - rim, h * UP - 1 - rim], fill=STEEL_DK + (222,))
    d.rectangle([rim, rim, w * UP - 1 - rim, rim + 2 * UP - 1], fill=STEEL_HI + (200,))
    d.rectangle([rim, h * UP - 1 - rim - 2 * UP + 1, w * UP - 1 - rim, h * UP - 1 - rim], fill=(0, 0, 0, 160))
    if trough is not None:
        x, y, tw, th = (v * UP for v in trough)
        d.rectangle([x, y, x + tw - 1, y + th - 1], fill=STEEL + (200,))
        d.rectangle([x + rim, y + rim, x + tw - 1 - rim, y + th - 1 - rim], fill=(0, 0, 0, 200))
    return img.resize((w, h), Image.LANCZOS)


def bar_block():
    _, _, w, h = BAR_BLOCK
    img = Image.new("RGBA", (w, h), GREEN + (240,))
    # a lighter top row on the mapped part, so the bar reads as lit like the trough's rim
    top = BAR[1] - BAR_BLOCK[1]
    ImageDraw.Draw(img).rectangle([0, 0, w - 1, top + 1], fill=(170, 245, 185, 240))
    return img


def hud_sheet():
    sheet = Image.new("RGBA", (SHEET_W, SHEET_H), (0, 0, 0, 0))
    for index, kind in enumerate(GLYPHS):
        sheet.paste(glyph(kind), (index * GLYPH, 0))
    sheet.paste(plate(CHIP[2], CHIP[3]), CHIP[:2])
    sheet.paste(plate(TRAY[2], TRAY[3], TROUGH), TRAY[:2])
    sheet.paste(bar_block(), BAR_BLOCK[:2])
    return sheet


def area_decal():
    n = AREA * AREA_UP
    c = n / 2
    r = AREA_RADIUS * AREA_UP
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    w = round(AREA_STROKE * AREA_UP)
    step = 360.0 / DASHES
    for i in range(DASHES):     # a dash over 55% of each step, centred on it, so every tick sits on a dash
        start = i * step - step * 0.275
        d.arc([c - r - w / 2, c - r - w / 2, c + r + w / 2, c + r + w / 2], start, start + step * 0.55, fill=255, width=w)
    for i in range(TICKS):
        t = 2 * math.pi * i / TICKS
        length = (TICK_LONG if i % 6 == 0 else TICK_SHORT) * AREA_UP
        o = (c + r * math.cos(t), c + r * math.sin(t))
        e = (c + (r - length) * math.cos(t), c + (r - length) * math.sin(t))
        d.line([o, e], fill=255, width=round(2.4 * AREA_UP))
    decal = keylined(m, 2 * AREA_UP, 140).resize((AREA, AREA), Image.LANCZOS)
    # a decal clamps its texture, so any alpha left on the border is drawn out past it as a faint
    # square: two texels of nothing round the edge
    clear = Image.new("L", decal.size, 0)
    clear.paste(decal.getchannel("A").crop((2, 2, AREA - 2, AREA - 2)), (2, 2))
    decal.putalpha(clear)
    return decal


def mapped(name, box):
    x, y, w, h = box
    return ("MappedImage %s\n  Texture = ReforgedReconHud.tga\n  TextureWidth = %d\n  TextureHeight = %d\n"
            "  Coords = Left:%d Top:%d Right:%d Bottom:%d\n  Status = NONE\nEnd\n"
            % (name, SHEET_W, SHEET_H, x, y, x + w, y + h))


def ini():
    blocks = [mapped("ReforgedReconGlyph%s" % kind, (i * GLYPH, 0, GLYPH, GLYPH)) for i, kind in enumerate(GLYPHS)]
    blocks += [mapped("ReforgedReconChip", CHIP), mapped("ReforgedReconTray", TRAY), mapped("ReforgedReconBar", BAR)]
    head = ("; The SIHA recon marks' 2D pieces (GameClient/RadarContactMarks.cpp).  Written by\n"
            "; Tools/recon_marks.py with Art/Textures/ReforgedReconHud.tga; do not edit by hand.  Mapped\n"
            "; images are not in the multiplayer INI checksum.\n\n")
    return head + "\n".join(blocks)


def main():
    for name, img in (("ReforgedReconHud.tga", hud_sheet()), ("ReforgedReconArea.tga", area_decal())):
        path = os.path.join(TEXTURES, name)
        img.save(path)
        print(path)
    path = os.path.join(MAPPED, "ReforgedReconHud.ini")
    with open(path, "w", newline="\r\n") as f:
        f.write(ini())
    print(path)


if __name__ == "__main__":
    main()
