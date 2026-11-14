"""The tank gun's muzzle flash cards: sixteen distinct flame lobes in a 4 by 4 DDS atlas, one per
particle (FlipbookGrid = 4, FlipbookRandomStart, a FlipbookRate so slow the frame never changes).

A gun blast lasts a few frames on screen, so a burn played over it would smear; what reads is the
shape.  The INI builds the blast's direction out of these cards: a short chain of them thrown along
the barrel that stops dead and shrinks (the jet), one held on the muzzle (the hot core), and a soot
puff from the smoke flipbook.  A camera-facing card cannot point along a barrel, so every lobe is
compact and ragged, and the chain gives the direction.

Each lobe is a volume rendered here, not painted: a cluster of hot gas balls whose surface is torn by
3D fractal noise, a temperature that falls from a small core to the torn edges, soot that absorbs
where the gas is cool, ray marched front to back (emission and absorption) with numpy alone.  The
colour comes from the temperature the ray saw, through a fire ramp that stops at a pale yellow: the
card is an ALPHA blend drawn Emissive, so stacking cards converges on that colour instead of adding
to white the way EA's twelve ADDITIVE EXexplo03 cards did.

    python Tools/muzzle_flipbook.py build [preview folder]
        writes Data/Art/Textures/ReforgedMuzzleFlash.dds, and with a folder the contact sheets and
        a mock shot strip.
    python Tools/muzzle_flipbook.py selfcheck"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEXTURES = os.path.join(HERE, "..", "Data", "Art", "Textures")
NAME = "ReforgedMuzzleFlash.dds"

GRID = 4				# 4 by 4 variants
CELL = 256				# pixels a variant side: 1024 atlas
DEPTH = 96				# ray march steps through the volume
SPAN = 0.78				# half the cell's width in volume units: the gas fills the cell to its window
SEED = 1914

# The fire ramp by temperature seen along the ray: soot, deep red, orange, yellow, the pale core.
# The core stops short of white on purpose: blue at 0.62 keeps even a stack of cores under the white
# the bisect counted (every channel over 230).
RAMP = ((0.0, (0.10, 0.04, 0.02)), (0.28, (0.52, 0.10, 0.02)), (0.5, (0.96, 0.38, 0.06)),
        (0.72, (1.0, 0.64, 0.20)), (0.88, (1.0, 0.80, 0.40)), (1.0, (1.0, 0.90, 0.62)))


def upsample(lattice, shape):
    """A small random lattice to shape by separable smoothstep interpolation: value noise."""
    import numpy as np
    out = lattice
    for axis, n in enumerate(shape):
        size = out.shape[axis]
        x = np.linspace(0.0, size - 1.001, n)
        i = np.floor(x).astype(int)
        f = x - i
        f = f * f * (3.0 - 2.0 * f)
        a = np.take(out, i, axis=axis)
        b = np.take(out, i + 1, axis=axis)
        bshape = [1] * out.ndim
        bshape[axis] = n
        f = f.reshape(bshape)
        out = a * (1.0 - f) + b * f
    return out


def noise(lattice, q):
    """Value noise of a periodic lattice (n, n, n) at points q (..., 3) in lattice cells."""
    import numpy as np
    n = lattice.shape[0]
    i = np.floor(q).astype(np.int64)
    f = q - i
    f = f * f * (3.0 - 2.0 * f)
    out = 0.0
    for corner in range(8):
        o = [(corner >> k) & 1 for k in range(3)]
        w = 1.0
        for k in range(3):
            w = w * (f[..., k] if o[k] else 1.0 - f[..., k])
        out = out + w * lattice[(i[..., 0] + o[0]) % n, (i[..., 1] + o[1]) % n, (i[..., 2] + o[2]) % n]
    return out


def fbm_at(rng, q, octaves):
    """Fractal value noise in about -1..1 at points q (..., 3), the coarsest octave one cell a unit."""
    import numpy as np
    total, amplitude, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        lattice = rng.uniform(-1.0, 1.0, (32, 32, 32)).astype(np.float32)
        total = total + amplitude * noise(lattice, q * np.float32(2 ** o) + rng.uniform(0, 32, 3).astype(np.float32))
        norm += amplitude
        amplitude *= 0.5
    return (total / norm).astype(np.float32)


def lobe(rng, size=CELL, depth=DEPTH):
    """One flame lobe as premultiplied RGBA (size, size, 4) in 0..1."""
    import numpy as np
    axis = np.linspace(-1.0, 1.0, size, dtype=np.float32)
    zaxis = np.linspace(-1.0, 1.0, depth, dtype=np.float32)
    y, x, z = np.meshgrid(axis * SPAN, axis * SPAN, zaxis, indexing="ij")
    p = np.stack((x, y, z), -1)

    # The gas: a main ball and three to six smaller ones blown out of it, as a sum of Gaussians.
    shape = np.zeros(x.shape, np.float32)
    balls = [((0.0, 0.0, 0.0), rng.uniform(0.36, 0.44))]
    for _ in range(rng.integers(3, 7)):
        d = rng.normal(size=3)
        d[2] *= 0.6
        d /= np.linalg.norm(d)
        balls.append((tuple(d * rng.uniform(0.25, 0.5)), rng.uniform(0.17, 0.28)))
    for (cx, cy, cz), r in balls:
        shape += np.exp(-((x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2) / (r * r))

    # Tongues: noise of the direction from the centre, drifting slowly with the radius, so the gas
    # tears into streaks that radiate out like the spikes of a real blast; then fine turbulence
    # through a warped domain, more at the rim than in the core.
    radius = np.linalg.norm(p, axis=-1, keepdims=True)
    direction = p / np.maximum(radius, 1e-4)
    drift = rng.normal(size=3).astype(np.float32)
    streak = fbm_at(rng, direction * rng.uniform(2.2, 3.4) + radius * drift * 0.6, 4)
    streak *= np.clip(radius[..., 0] / 0.3, 0.0, 1.0)		# the core stays whole, no pinch where the tongues meet
    warp = np.stack([fbm_at(rng, p * 1.5, 2) for _ in range(3)], -1)
    turbulence = fbm_at(rng, (p + 0.25 * warp) * 3.0, 4)
    rim = np.clip(1.3 - shape, 0.0, 1.0)
    field = shape * (0.75 + 0.75 * streak) + 0.5 * turbulence * rim
    density = np.clip((field - 0.32) / 0.25, 0.0, 1.0)

    # Hottest in a small core, cooling to the torn edges; a second noise breaks the isotherms.
    heat = fbm_at(rng, p * 2.5, 3)
    temperature = np.clip((field - 0.32) / 1.1 + 0.15 * heat, 0.0, 1.0) * density
    radiance = temperature ** 2.6 * 9.0						# black body: brightness climbs fast with heat
    absorb = density * (4.0 + 5.0 * (1.0 - temperature))	# soot is where the gas is cool

    # Front to back along z: what a pixel shows is emitted in a slab and dimmed by the soot before it.
    dz = 2.0 / depth
    step = 1.0 - np.exp(-absorb * dz)
    before = np.exp(-np.cumsum(absorb * dz, axis=-1) + absorb * dz)
    weight = step * before
    light = (radiance * weight).sum(-1)
    seen = (temperature * weight).sum(-1) / np.maximum(weight.sum(-1), 1e-5)
    alpha = 1.0 - before[..., -1] * (1.0 - step[..., -1])

    glow = 1.0 - np.exp(-light)
    t = np.clip(0.2 + 0.85 * glow + 0.1 * seen, 0.0, 1.0)
    stops = np.array([s for s, _ in RAMP])
    colour = np.stack([np.interp(t, stops, [c[k] for _, c in RAMP]) for k in range(3)], -1)
    colour *= (0.7 + 0.3 * glow)[..., None]
    alpha = np.clip(np.maximum(alpha, glow * 1.3), 0.0, 1.0)

    # A round window, so no lobe touches its cell's edge however the noise fell.
    r = np.hypot(*np.meshgrid(axis, axis, indexing="ij"))
    fade = np.clip((0.96 - r) / 0.2, 0.0, 1.0)
    alpha *= fade * fade * (3.0 - 2.0 * fade)
    alpha = np.where(alpha < 0.02, 0.0, alpha)
    return np.concatenate((colour * alpha[..., None], alpha[..., None]), -1).astype(np.float32)


def variants(cell=CELL, depth=DEPTH, seed=SEED):
    import numpy as np
    rng = np.random.default_rng(seed)
    return np.stack([lobe(rng, cell, depth) for _ in range(GRID * GRID)])


def sheet(frames):
    import numpy as np
    cell = frames.shape[1]
    image = np.zeros((GRID * cell, GRID * cell, frames.shape[-1]), np.float32)
    for index, frame in enumerate(frames):
        row, column = divmod(index, GRID)
        image[row * cell:(row + 1) * cell, column * cell:(column + 1) * cell] = frame
    return image


def straight(image):
    import numpy as np
    alpha = image[..., 3:]
    rgb = np.where(alpha > 1e-3, image[..., :3] / np.maximum(alpha, 1e-3), 0.0)
    return np.clip(np.concatenate((rgb, alpha), -1), 0.0, 1.0)


def over(premultiplied, background):
    return premultiplied[..., :3] + background * (1.0 - premultiplied[..., 3:])


def save_png(path, rgb):
    import numpy as np
    from PIL import Image
    Image.fromarray(np.clip(rgb * 255.0 + 0.5, 0, 255).astype(np.uint8)).save(path)


def ground(h, w):
    """A dusty desert floor in the game's late afternoon, for judging the cards against."""
    import numpy as np
    rng = np.random.default_rng(5)
    n = upsample(rng.uniform(-1, 1, (12, 20, 2)).astype(np.float32), (h, w, 1))[..., 0]
    base = np.array([0.42, 0.33, 0.20], np.float32)
    return np.clip(base * (1.0 + 0.25 * n)[..., None], 0.0, 1.0)


def mock_shot(frames, width=1200, height=300, steps=6, seed=3):
    """What the INI throws, drawn flat: a barrel pointing right, the jet's chain of cards along it
    and the hot core on the muzzle, each card a random variant and turn, over six frames."""
    import numpy as np
    from PIL import Image
    rng = np.random.default_rng(seed)
    unit = 9.0		# pixels a game unit is drawn at here (the default camera is about 4)
    cards = []
    for _ in range(6):		# jet
        cards.append(dict(v=(rng.uniform(1.0, 4.0), rng.uniform(-0.35, 0.35)), damp=rng.uniform(0.25, 0.3),
                          size=rng.uniform(5.0, 6.5), rate=rng.uniform(-0.45, -0.3), life=rng.uniform(6, 7)))
    cards.append(dict(v=(0.0, 0.0), damp=0.0, size=4.5, rate=-0.5, life=4))		# core
    for c in cards:
        c["variant"] = rng.integers(0, len(frames))
        c["turn"] = rng.uniform(0, 360)
    panels = []
    for t in range(steps):
        image = ground(height, width // steps)
        cx, cy = 40, height // 2
        image[cy - 3:cy + 3, :cx] = 0.12		# the barrel
        for c in cards:
            if t >= c["life"]:
                continue
            travel = (1.0 - c["damp"] ** (t + 1)) / (1.0 - c["damp"])
            px = cx + c["v"][0] * travel * unit
            py = cy + c["v"][1] * travel * unit
            size = c["size"] + c["rate"] * (t + 1)
            fade = max(0.0, 1.0 - t / c["life"])
            s = max(4, int(size * unit))
            sprite = np.stack([np.asarray(Image.fromarray(frames[c["variant"]][..., k]).rotate(c["turn"]).resize((s, s), Image.LANCZOS))
                               for k in range(4)], -1) * fade
            top, left = int(py - s / 2), int(px - s / 2)
            y0, x0 = max(top, 0), max(left, 0)
            y1, x1 = min(top + s, image.shape[0]), min(left + s, image.shape[1])
            if y1 <= y0 or x1 <= x0:
                continue
            part = sprite[y0 - top:y1 - top, x0 - left:x1 - left]
            image[y0:y1, x0:x1] = part[..., :3] + image[y0:y1, x0:x1] * (1.0 - part[..., 3:])
        panels.append(image)
    return np.concatenate(panels, axis=1)


def build(folder=None):
    sys.path.insert(0, HERE)
    from smoke_flipbook import dds
    frames = variants()
    path = os.path.normpath(os.path.join(TEXTURES, NAME))
    with open(path, "wb") as out:
        out.write(dds(straight(sheet(frames)), CELL))
    print(path, os.path.getsize(path))
    if folder:
        from PIL import Image
        import numpy as np
        os.makedirs(folder, exist_ok=True)
        whole = sheet(frames)
        save_png(os.path.join(folder, "sheet-dark.png"), over(whole, np.full(whole.shape[:2] + (3,), 0.08, np.float32)))
        save_png(os.path.join(folder, "sheet-ground.png"), over(whole, ground(*whole.shape[:2])))
        small = np.stack([np.stack([np.asarray(Image.fromarray(f[..., k]).resize((48, 48), Image.LANCZOS)) for k in range(4)], -1)
                          for f in frames])
        game = sheet(small)
        save_png(os.path.join(folder, "sheet-game-scale.png"), over(game, ground(*game.shape[:2])))
        save_png(os.path.join(folder, "mock-shot.png"), mock_shot(frames))


def selfcheck():
    import numpy as np
    # Noise: in range, and smooth (neighbours close at the finest octave's scale).
    rng = np.random.default_rng(0)
    q = np.stack(np.meshgrid(*(np.linspace(0, 4, 32),) * 3, indexing="ij"), -1)
    n = fbm_at(rng, q, 4)
    assert n.shape == (32, 32, 32) and -1.0 <= n.min() and n.max() <= 1.0 and n.std() > 0.05
    assert np.abs(np.diff(n, axis=0)).max() < 0.5
    # A lobe: something in the middle, nothing at the cell's edge, premultiplied colour within its
    # alpha, and the core short of white (blue under 0.7 straight).
    frames = variants(cell=48, depth=24, seed=2)
    assert frames.shape == (GRID * GRID, 48, 48, 4)
    for f in frames:
        assert f[24, 24, 3] > 0.5 and not f[0, :, 3].any() and not f[:, -1, 3].any()
        assert (f[..., :3] <= f[..., 3:] + 1e-5).all()
        s = straight(f[None])[0]
        assert s[..., 2][f[..., 3] > 0.05].max() < 0.7
    # Sixteen different shapes, not one repeated.
    masks = frames[..., 3] > 0.3
    assert len({m.tobytes() for m in masks}) == GRID * GRID
    print("selfcheck passed")


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command == "build":
        build(sys.argv[2] if len(sys.argv) > 2 else None)
    elif command == "selfcheck":
        selfcheck()
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
