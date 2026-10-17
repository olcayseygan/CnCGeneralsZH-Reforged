#!/usr/bin/env python3
"""Main/Generals.ico for the platforms that do not read .ico files.

    generals-icon.py png <ico> <out.png> [size]   a PNG, at the .ico's own size or scaled to size
    generals-icon.py header <ico> <out.h>         the pixels as a C array, for SDL_SetWindowIcon

Takes the .ico's largest 24-bit image (48 px) with its AND mask as alpha. Scaling is bilinear over
premultiplied alpha; AppStream, which the Flatpak's metadata goes through, wants 64 px or more.
"""
import struct
import sys
import zlib


def read_ico(path):
    d = open(path, "rb").read()
    count = struct.unpack("<H", d[4:6])[0]
    best = None
    for i in range(count):
        w, h, c, r, planes, bpp, size, off = struct.unpack("<BBBBHHII", d[6 + 16 * i:22 + 16 * i])
        w, h = w or 256, h or 256
        if d[off:off + 4] == b"\x89PNG":
            continue
        header = struct.unpack("<IiiHHIIiiII", d[off:off + 40])
        if header[4] == 24 and (best is None or w > best[0]):
            best = (w, h, off + header[0])
    w, h, pixels = best
    row = (w * 3 + 3) & ~3
    mask_row = ((w + 31) // 32) * 4
    mask = pixels + row * h
    rgba = []  # rows of (r, g, b, a), top first
    for y in range(h):
        src = pixels + row * (h - 1 - y)
        msrc = mask + mask_row * (h - 1 - y)
        line = []
        for x in range(w):
            b, g, r = d[src + 3 * x:src + 3 * x + 3]
            transparent = (d[msrc + x // 8] >> (7 - x % 8)) & 1
            line.append((r, g, b, 0 if transparent else 255))
        rgba.append(line)
    return rgba


def scaled(image, size):
    n = len(image)

    def at(x, y):
        r, g, b, a = image[min(max(y, 0), n - 1)][min(max(x, 0), n - 1)]
        return (r * a / 255.0, g * a / 255.0, b * a / 255.0, float(a))

    out = []
    for y in range(size):
        fy = (y + 0.5) * n / size - 0.5; y0 = int(fy // 1); ty = fy - y0
        line = []
        for x in range(size):
            fx = (x + 0.5) * n / size - 0.5; x0 = int(fx // 1); tx = fx - x0
            p = [at(x0, y0)[i] * (1 - tx) * (1 - ty) + at(x0 + 1, y0)[i] * tx * (1 - ty)
                 + at(x0, y0 + 1)[i] * (1 - tx) * ty + at(x0 + 1, y0 + 1)[i] * tx * ty for i in range(4)]
            a = p[3]
            line.append(tuple(int(round(min(255, c * 255.0 / a))) if a > 0 else 0 for c in p[:3]) + (int(round(a)),))
        out.append(line)
    return out


def write_png(path, image):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    size = len(image)
    raw = b"".join(bytes([0]) + bytes(v for p in line for v in p) for line in image)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_header(path, image):
    size = len(image)
    data = [v for line in image for p in line for v in p]
    lines = [", ".join(str(v) for v in data[i:i + 24]) for i in range(0, len(data), 24)]
    open(path, "w").write(
        "// Generated from Main/Generals.ico by Tools/generals-icon.py; not edited by hand.\n"
        f"static const int ZH_WINDOW_ICON_SIZE = {size};\n"
        "static const unsigned char ZH_WINDOW_ICON_RGBA[] = {\n\t" + ",\n\t".join(lines) + "\n};\n")


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ("png", "header"):
        print(__doc__, file=sys.stderr)
        return 2
    image = read_ico(sys.argv[2])
    if sys.argv[1] == "png":
        if len(sys.argv) > 4:
            image = scaled(image, int(sys.argv[4]))
        write_png(sys.argv[3], image)
    else:
        write_header(sys.argv[3], image)
    return 0


if __name__ == "__main__":
    sys.exit(main())
