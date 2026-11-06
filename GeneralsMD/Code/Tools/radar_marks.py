"""The ground marks of a Turkiye radar contact (GameClient/RadarContactMarks.cpp): a ring with a pip in
its centre where the sweep painted a hidden enemy, and a thin ring for the ping ripples and the last
sweep's echo.  The game lays them on the terrain as decals tinted the contact red, so they are white
where the colour goes; the contact mark has a soft dark edge so it reads on sand and in shadow.

    python Tools/radar_marks.py            # writes Data/Art/Textures/ReforgedRadar*.tga

Drawn eight times too big and brought down, which is the antialiasing.  Shipped small and with strokes
four texels wide or more: the decal manager turns mipmapping off, so a finer sheet or a thinner line
shimmers as the camera moves."""
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

DRAWN = 512
SHIPPED = 64
CENTRE = DRAWN // 2
RING_OUTER = 232
RING_INNER = 196	# the contact ring's stroke, 2.2 of 15 in the approved still
PIP = 46
KEYLINE = 17	# a MaxFilter size, odd: the dark edge round every white shape, 8 drawn pixels, 1 texel
KEYLINE_BLUR = 8	# drawn pixels the dark edge is softened over, a shadow rather than an outline
KEYLINE_ALPHA = 150	# out of 255: enough to lift the red off bright desert, not a black ring
THIN_INNER = 200	# the ripple and echo ring's stroke, four shipped texels
OUT_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures")


def blank():
    return Image.new("L", (DRAWN, DRAWN), 0)


def disc(mask, radius, fill):
    ImageDraw.Draw(mask).ellipse((CENTRE - radius, CENTRE - radius, CENTRE + radius, CENTRE + radius), fill=fill)


def ring(outer, inner):
    mask = blank()
    disc(mask, outer, 255)
    disc(mask, inner, 0)
    return mask


def shipped(white, alpha):
    return Image.merge("RGBA", (white, white, white, alpha)).resize((SHIPPED, SHIPPED), Image.LANCZOS)


def contact():
    white = ring(RING_OUTER, RING_INNER)
    disc(white, PIP, 255)
    edge = white.filter(ImageFilter.MaxFilter(KEYLINE)).filter(ImageFilter.GaussianBlur(KEYLINE_BLUR))
    return shipped(white, ImageChops.lighter(white, edge.point(lambda v: v * KEYLINE_ALPHA // 255)))


def thin():
    white = ring(RING_OUTER, THIN_INNER)
    return shipped(white, white)


def main():
    out_folder = sys.argv[1] if len(sys.argv) > 1 else OUT_FOLDER
    for name, mark in (("Contact", contact()), ("Ring", thin())):
        path = os.path.join(out_folder, "ReforgedRadar%s.tga" % name)
        mark.save(path)
        print(path)


if __name__ == "__main__":
    main()
