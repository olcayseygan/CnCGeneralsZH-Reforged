"""Smoke, dust and fire flipbooks for ParticleSystem FlipbookGrid = 8, simulated, not drawn: a
Mantaflow sim in Blender, rendered as a volume in Cycles, packed eight by eight into a DDS atlas.

Looks (LOOKS below):
    puff, puffb   a burst of smoke that billows out and rolls over: deaths, burning buildings.  Two
                  sims from different emitters, so a column built of both never repeats one shape.
                  Played once over a particle's life (FlipbookRate 0).
    wisp          a churning plume that sheds puffs upwards: trails and rising smoke.  Its 64 frames
                  loop (the last 16 crossfade into the first), so FlipbookRate and
                  FlipbookRandomStart never pop.
    dust          a ground burst: earth kicked out low and wide that rolls and settles, for shell
                  hits and the dirt of a vehicle's death.
    fire          a fuel burst burning out: a hot core, ragged flame tongues cooling through orange to
                  deep red, eaten by the soot they make, which is all that is left at the end.  An
                  ALPHA card drawn Emissive: the flame covers what is behind it, the soot darkens it.

Smoke is neutral grey, low albedo and dense so it shades itself hard, lit from the side and above;
the INI's Color keys tint it, the alpha carries the shape.  What matters is what survives at game
scale, where a 256 pixel cell is drawn 60 to 200 pixels wide: a few big lobes, each lit on one side
and in its own shadow on the other.  Fire's colour is a black body whose temperature follows the
sim's flame field, rendered linear and tone mapped here so the hottest parts bleach to white.

Three steps; Blender runs the first, plain Python (numpy, Pillow, ffmpeg on PATH) the others:
    blender -b --factory-startup -P Tools/smoke_flipbook.py -- render <look> <work>
        bakes the sim into <work>/<look>/cache and renders <work>/<look>/frames/####.png.
        SMOKE_RES and SMOKE_SAMPLES in the environment override the sim and sample counts,
        SMOKE_REUSE=1 keeps a baked cache, SMOKE_PICK=0,20,40 renders only those frames.
    python Tools/smoke_flipbook.py pack <work> [preview folder] [look ...]
        writes the atlases into Data/Art/Textures, and with a folder the contact sheets and the
        looping clips the owner judges.
    python Tools/smoke_flipbook.py selfcheck"""
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEXTURES = os.path.join(HERE, "..", "Data", "Art", "Textures")

GRID = 8
FRAMES = GRID * GRID
CELL = 256				# pixels a frame side in the DDS: 2048 atlas, 5.6 MB with mips; a look's "cell" overrides
RENDER = 1024			# pixels a side Blender renders; cropped and filtered down to CELL
MARGIN = 0.07			# share of a cell left empty round the union extent
SMALLEST_CELL = 8		# the smallest mip the game keeps has frames this many pixels a side
THROWAWAY_LEVELS = 2	# DDSFileClass drops a file's two smallest levels (ddsfile.cpp)
LOOP_BLEND = 16			# wisp: frames crossfaded across the loop seam
THRESHOLD = 0.15		# alpha that counts as smoke when framing; fainter haze is left to the window
						# (at 0.04 the ground haze under a puff framed it, and it filled 40% of its cell)
ALPHA_FLOOR = 0.025		# haze fainter than this goes, or a stack of particles shows each square
GAME_CELL = 80			# preview: pixels a cell is drawn at from the default camera

# The smoke's emitters: x, y, z, radius, first and last frame it blows.  Big separate lobes, so what
# is left at 80 pixels is a cluster of lit balls, not one soft blob.
PUFF_A = ((0.0, 0.0, 1.0, 0.5, 1, 6), (0.55, 0.1, 1.3, 0.34, 2, 7), (-0.5, -0.15, 1.2, 0.38, 2, 6),
          (0.1, 0.3, 1.75, 0.33, 4, 9), (-0.25, -0.3, 1.55, 0.3, 3, 8))
PUFF_B = ((0.1, 0.0, 0.9, 0.45, 1, 5), (-0.6, 0.2, 1.1, 0.36, 1, 6), (0.62, -0.2, 1.0, 0.32, 3, 8),
          (0.0, -0.1, 1.65, 0.38, 4, 10), (0.32, 0.25, 1.95, 0.27, 6, 11), (-0.32, 0.1, 1.9, 0.25, 5, 9))
SMOKE = dict(
    kind="smoke", res=320, samples=96, step=1, loop=False, crop=None, floor=False,
    density=1.0, temperature=1.5, density_buoyancy=0.0, heat_buoyancy=0.8, vorticity=0.25,
    velocity_normal=1.4, velocity_up=1.0, velocity_random=1.6, dissolve=0, pulse=0,
    lump=0.5, noise_scale=0.45, tumble=(2.0, 1.3, 3.1),
    density_scale=110.0, density_power=1.2, albedo=0.55, ortho=7.5, aim_z=3.0,
    growth=1.0, hold=0.85, gamma=1.25, base=None)		# growth 1: every frame fills its cell; the INI grows it

# v3: a 200 grid doubled by Mantaflow's wavelet noise (upres) tears the flame front into tongues
# where a plain 256 or 320 grid kept the emitters' round lobes; harder burst and flame vorticity;
# the flame itself opaque (flame_density) and torn by noise in the shader; the first three sim
# frames, the emitters' own lumpy shapes, skipped.
FIRE = dict(
    kind="fire", res=200, samples=128, step=1, loop=False, crop=None, floor=False,
    upres=2, upres_strength=2.0, upres_size=2.0,
    frame_end=70, first=5, pulse=0, time_scale=0.6,
    density=0.0, temperature=2.0, fuel=3.0, density_buoyancy=0.0, heat_buoyancy=1.6,
    vorticity=0.9, velocity_normal=5.5, velocity_up=0.6, velocity_random=4.5,
    dissolve=0, lump=0.55, noise_scale=0.3,
    burning_rate=0.5, flame_smoke=1.8, flame_vorticity=5.0, flame_ignition=1.25, flame_max_temp=2.5,
    soot_scale=1500.0, flame_strength=300.0, flame_power=4.5,
    flame_noise=0.9, flame_noise_scale=6.0, flame_noise_speed=0.06, flame_edge=0.15, flame_gate=0.04,
    flame_density=1500.0,
    kelvin_low=800.0, kelvin_span=3400.0, kelvin_shape=1.4,
    ortho=4.0, aim_z=1.7, growth=0.55,
    retime=1.35)		# frame i shows sim frame (i / n) ** 1.35 of the run: the soot turn comes a quarter later

# A steady fire.  The first 45 sim frames, the fire still finding its height, are skipped; the 80
# after loop, the last 16 crossfaded into the first.  Fuel flows from a thin skin round its lumps
# (surface): from the default 0.6 the base was one wide white slab.  The soot dissolves, so it
# stays a thin wisp and the domain small.  Exposed to one steady key, not the fireball's cooling
# arc, and cropped to one square for the whole loop with the fire's foot near the card's bottom,
# so the card stands on what burns.
FLAME = dict(FIRE, loop=True, first=45, frame_end=126, floor=True, surface=0.2, dissolve=25,
             time_scale=0.8, fuel=1.6, temperature=1.6, heat_buoyancy=3.0, vorticity=0.6,
             velocity_normal=0.4, velocity_up=1.5, velocity_random=1.3, lump=0.35, noise_scale=0.25,
             burning_rate=0.85, flame_smoke=0.4, flame_vorticity=4.0, soot_scale=250.0,
             key=1.0, ortho=4.5, aim_z=1.8, base=0.92, headroom=0.12)		# headroom: the soot's wisps

LOOKS = {
    # The first 29 frames are skipped: straight out of the emitters the lobes are smooth, dense balls
    # (the "clay" look); from there they roll and curl.  One sim frame a flipbook frame: past sim
    # frame 95 the vortices have smeared into haze, so the flipbook ends while it still has structure.
    "puff": dict(SMOKE, name="ReforgedSmokePuff.dds", frame_end=102, first=30, emitters=PUFF_A),
    "puffb": dict(SMOKE, name="ReforgedSmokePuffB.dds", frame_end=102, first=30, emitters=PUFF_B,
                  tumble=(0.7, 2.6, 1.2), noise_scale=0.38, velocity_random=1.9),
    # A plume from a small hot source that blows in pulses; the square cut from it is its dense,
    # rolling lower part just above the source's neck, and a round window softens the cut.  Higher
    # up it has thinned to the pale tube the first version was cut from.
    # At res 200 its fast rise smeared into vertical streaks; 288 and more vorticity curl it instead.
    "wisp": dict(SMOKE, name="ReforgedSmokeWisp.dds", res=288, frame_end=220, first=120, loop=True,
                 emitters=((0.0, 0.0, 0.5, 0.32, 1, 10000),), pulse=7, samples=128,
                 velocity_normal=0.3, velocity_up=1.6, velocity_random=0.6, heat_buoyancy=1.0,
                 vorticity=0.3, dissolve=40, lump=0.35, density_scale=70.0, density_power=1.0,
                 ortho=6.0, growth=1.0, crop=(0.5, 0.70, 0.24)),
    # Earth thrown out low and wide from a hit: no heat, a floor, the dust sinking back as it spreads.
    # Seen small, a shell's hit at most a tank across, so 128 pixel frames: a 1.4 MB atlas.
    "dust": dict(SMOKE, name="ReforgedDust.dds", cell=128, frame_end=80, first=6, floor=True,
                 emitters=((0.0, 0.0, 0.1, 0.45, 1, 3), (0.5, 0.2, 0.15, 0.3, 1, 4),
                           (-0.45, -0.1, 0.12, 0.32, 2, 4), (0.05, -0.35, 0.2, 0.28, 2, 5)),
                 temperature=0.0, heat_buoyancy=0.0, density_buoyancy=-0.15,
                 velocity_normal=4.0, velocity_up=1.0, velocity_random=1.6, vorticity=0.1,
                 samples=160,		# at 0.3 vorticity and 96 samples its settled half went hairy
                 tumble=(1.1, 0.4, 2.2), density_scale=70.0, albedo=0.6, aim_z=1.6, ortho=7.0,
                 growth=0.35, hold=0.45, base=0.62),		# its foot just under the card's middle
    # A fuel burst: lumps of fuel blown hard outwards for a few frames, burning out while the heat
    # lifts them.  The fuel carries no smoke of its own: the first version's did, which made the young
    # fireball an opaque skin with no hot core showing through.  Strong flame vorticity and random
    # velocity tear the front into tongues; the soot the flame makes cuts lanes into it.
    "fire": dict(FIRE, name="ReforgedFireball.dds", tumble=(1.7, 0.9, 2.4),
                 emitters=((0.0, 0.0, 1.0, 0.34, 1, 4), (0.45, 0.15, 1.15, 0.24, 1, 5),
                           (-0.42, -0.1, 1.1, 0.26, 2, 5), (0.1, 0.32, 1.45, 0.22, 2, 6),
                           (-0.18, -0.3, 1.4, 0.2, 3, 7), (0.32, -0.28, 0.85, 0.2, 1, 3),
                           (-0.35, 0.25, 0.8, 0.18, 2, 4), (0.15, -0.05, 1.7, 0.2, 4, 7))),
    # The same burst from other lumps, so a blast built of both never shows one silhouette twice.
    "fireb": dict(FIRE, name="ReforgedFireballB.dds", tumble=(0.4, 2.2, 1.1), noise_scale=0.26,
                  emitters=((0.05, 0.05, 0.95, 0.3, 1, 4), (-0.5, 0.1, 1.05, 0.26, 1, 5),
                            (0.48, -0.15, 1.2, 0.25, 2, 5), (-0.1, -0.35, 1.3, 0.22, 2, 6),
                            (0.25, 0.3, 1.55, 0.22, 3, 7), (-0.3, 0.2, 1.6, 0.18, 4, 7),
                            (0.55, 0.25, 0.85, 0.17, 1, 3))),
    # A fire that keeps burning in place, a wreck's or a building's: fuel welling up for good from a
    # few lumps on the ground, licking up in tongues that tear off and go out, a thin soot above.
    "flame": dict(FLAME, name="ReforgedFlame.dds", tumble=(0.9, 1.6, 0.7),
                  emitters=((-0.42, 0.0, 0.22, 0.2, 1, 10000), (0.0, 0.08, 0.18, 0.24, 1, 10000),
                            (0.4, -0.06, 0.22, 0.19, 1, 10000), (0.18, -0.2, 0.3, 0.15, 1, 10000))),
    # A gun's flame jet: a lump that flies 2.6 m along +x in two frames while it blows lays a thin
    # column of fuel, and a bigger lump at its end the ball of flame at the tip; the column burns,
    # tears into ragged tongues and goes out in sixteen frames.  4 by 4, 1024 square; turned so the
    # root is at the top edge's middle, the side a ground-aligned card turned up towards its
    # emitter puts on the muzzle.
    "jet": dict(FIRE, name="ReforgedMuzzleJet.dds", grid=4, root_up=True, base=0.93, headroom=0.04,
                res=256, frame_end=24, first=4, time_scale=0.5,
                emitters=((-1.9, 0.0, 3.0, 0.11, 1, 3), (0.75, 0.0, 3.0, 0.12, 2, 3)), sweep=(2.6, 0.0, 0.0),
                subframes=12, surface=0.1, lump=0.3, noise_scale=0.2,
                tumble=(0.6, 1.2, 0.4),
                velocity_coord=(14.0, 0.0, 0.0), velocity_normal=0.3, velocity_random=0.8,
                fuel=4.0, temperature=2.0, heat_buoyancy=0.2, vorticity=0.8, burning_rate=0.5,
                flame_smoke=0.3, flame_vorticity=5.0, soot_scale=300.0,
                ortho=4.6, aim_z=3.0),
    # One narrow tongue of it, for a small fire: a burning tree, a crate, a wreck's last flame.
    "flameb": dict(FLAME, name="ReforgedFlameB.dds", tumble=(1.4, 0.5, 1.1), ortho=3.0, aim_z=1.3,
                   emitters=((0.0, 0.0, 0.2, 0.22, 1, 10000), (0.12, 0.1, 0.3, 0.14, 1, 10000))),
    # The nuke's mushroom, in three cards the INI stacks, all seen at the RTS camera's 37 degrees and
    # played once.  The cap: a ball of hot fuel and soot let go above the ground, so its buoyancy
    # rolls it into a ring that turns over itself while it rises, hot and glowing underneath at
    # first, cooling along the fireball's arc to grey-brown soot with the glow going out under it.
    # Cropped round its centre like the fireball, so it sits in the middle of its card and grows.
    "nukecap": dict(FIRE, name="ReforgedNukeCap.dds", nuke=True, tilt=37.0, floor=True,
                    frame_end=70, first=6, time_scale=0.8, growth=0.5,
                    emitters=((0.0, 0.0, 0.6, 0.4, 1, 4), (0.12, -0.1, 0.75, 0.3, 1, 3)),
                    surface=0.2, fuel=3.0, temperature=2.0, heat_buoyancy=1.5, vorticity=0.15,
                    velocity_normal=0.3, velocity_up=5.0, velocity_random=0.4, lump=0.3,
                    burning_rate=0.25, flame_smoke=3.0, flame_vorticity=0.6, soot_scale=900.0,
                    tumble=(0.5, 1.3, 0.9), ortho=6.5, aim_z=3.0),
    # The stem: a dirty updraft fed for good from the ground, fire at its foot and soot above, torn at
    # its edges; framed by its smoke, not its glow, in one square for the whole run with its foot at
    # STEM_BASE, so it stands on the blast and its top runs up into the cap.
    "nukestem": dict(FIRE, name="ReforgedNukeStem.dds", nuke=True, tilt=37.0, floor=True, fixed=True,
                     by_light=False, base=0.95, headroom=0.0, frame_end=88, first=24, time_scale=1.0,
                     res=150,		# the column fills the domain's height: at 200 a frame took 90 s to bake
                     emitters=((0.0, 0.0, 0.25, 0.35, 1, 10000), (0.22, 0.15, 0.3, 0.22, 1, 10000),
                               (-0.22, -0.1, 0.3, 0.25, 1, 10000)),
                     surface=0.2, density=0.5, fuel=1.8, temperature=2.5, heat_buoyancy=3.0,
                     velocity_normal=0.3, velocity_up=2.0, velocity_random=0.8, vorticity=0.5,
                     burning_rate=0.6, flame_smoke=1.0, flame_vorticity=3.0, soot_scale=600.0,
                     lump=0.35, noise_scale=0.25, tumble=(0.8, 1.4, 0.6), ortho=7.0, aim_z=3.0),
    # A flame spike bursting up from the blast and tearing: the muzzle jet stood on end, its root at
    # the card's foot.  4 by 4.
    "nukejet": dict(FIRE, name="ReforgedNukeJet.dds", nuke=True, grid=4, fixed=True, base=0.93,
                    headroom=0.04, tilt=37.0, res=256, frame_end=24, first=5, time_scale=0.5,
                    emitters=((0.0, 0.0, 0.3, 0.11, 1, 3), (0.0, 0.0, 2.95, 0.13, 2, 3)), sweep=(0.0, 0.0, 2.6),
                    subframes=12, surface=0.1, lump=0.3, noise_scale=0.2, tumble=(0.6, 1.2, 0.4),
                    velocity_normal=0.3, velocity_random=0.8, fuel=4.0, temperature=2.0,
                    heat_buoyancy=0.6, vorticity=0.8, burning_rate=0.5, flame_smoke=0.3,
                    flame_vorticity=5.0, soot_scale=300.0, ortho=4.6, aim_z=1.8),
}
STEM_BASE = LOOKS["nukestem"]["base"]

# Variant atlases, for FlipbookVariants = 4: four runs of VARIANT_FRAMES back to back, each its own
# sim, so a field of particles that each pick one at birth never shows one silhouette twice.  A run
# is every fourth sim frame of the look's own span.  The sims differ by their emitters, the parent's
# lumps turned about the vertical and some mirrored, and by the tumble of the lump texture, so each
# is a different shape and not a re-timing of one.
VARIANTS = 4
VARIANT_FRAMES = FRAMES // VARIANTS


def turned(emitters, degrees, mirror=False):
    """An emitter set turned about the vertical axis, mirrored first if asked."""
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    return tuple((x * c - (-y if mirror else y) * s, x * s + (-y if mirror else y) * c, z, r, a, b)
                 for x, y, z, r, a, b in emitters)


def variant_looks(parent, name, plan):
    """The parts and the whole of one variant atlas: plan is (emitters, tumble) per run."""
    parts = {}
    for k, (emitters, tumble) in enumerate(plan):
        parts["%s%d" % (parent, k)] = dict(LOOKS[parent], name=None, frames=VARIANT_FRAMES, step=4,
                                           emitters=emitters, tumble=tumble)
    parts[parent + "v"] = dict(LOOKS[parent], name=name, parts=tuple(sorted(parts)))
    return parts


LOOKS.update(variant_looks("puff", "ReforgedSmokePuffV.dds", (
    (PUFF_A, (1.1, 2.3, 0.7)), (PUFF_B, (2.4, 0.6, 1.8)),
    (turned(PUFF_A, 130, True), (0.4, 1.7, 2.9)), (turned(PUFF_B, 240), (3.0, 1.0, 0.3)))))
LOOKS.update(variant_looks("fire", "ReforgedFireballV.dds", (
    (LOOKS["fire"]["emitters"], (2.1, 0.5, 1.4)), (LOOKS["fireb"]["emitters"], (0.9, 2.6, 0.4)),
    (turned(LOOKS["fire"]["emitters"], 150, True), (1.6, 1.9, 2.8)),
    (turned(LOOKS["fireb"]["emitters"], 260), (0.3, 1.1, 2.2)))))
LOOKS.update(variant_looks("dust", "ReforgedDustV.dds", (
    (LOOKS["dust"]["emitters"], (0.6, 1.8, 1.3)), (turned(LOOKS["dust"]["emitters"], 95), (2.0, 0.3, 0.9)),
    (turned(LOOKS["dust"]["emitters"], 190, True), (1.3, 2.5, 0.2)),
    (turned(LOOKS["dust"]["emitters"], 280, True), (0.2, 0.8, 2.7)))))


# ---------------------------------------------------------------------------------------------
# Blender: sim and render.

def blender_render(look, work):
    import bpy
    from mathutils import Vector

    p = dict(LOOKS[look])
    p["res"] = int(os.environ.get("SMOKE_RES", p["res"]))
    p["samples"] = int(os.environ.get("SMOKE_SAMPLES", p["samples"]))
    for pair in filter(None, os.environ.get("SMOKE_SET", "").split(",")):		# key=number, for probes
        key, value = pair.split("=")
        p[key] = float(value)
    fire = p["kind"] == "fire"
    root = os.path.abspath(os.path.join(work, look))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = 1, p["frame_end"]

    # Domain: 6 x 6 x 6 standing on z = 0, open on every side so nothing piles against a wall; the
    # dust has a floor to spread along.
    bpy.ops.mesh.primitive_cube_add(size=6.0, location=(0.0, 0.0, 3.0))
    domain = bpy.context.object
    fluid = domain.modifiers.new("Fluid", "FLUID")
    fluid.fluid_type = "DOMAIN"
    ds = fluid.domain_settings
    ds.domain_type = "GAS"
    ds.resolution_max = p["res"]
    ds.use_collision_border_top = False
    ds.use_collision_border_bottom = p["floor"]
    ds.use_collision_border_front = ds.use_collision_border_back = False
    ds.use_collision_border_left = ds.use_collision_border_right = False
    ds.vorticity = p["vorticity"]
    ds.time_scale = p.get("time_scale", 1.0)		# below 1 spreads a fast event over more frames
    ds.alpha = p["density_buoyancy"]
    ds.beta = p["heat_buoyancy"]
    ds.use_dissolve_smoke = p["dissolve"] > 0		# 0: the smoke thins only by spreading
    ds.use_dissolve_smoke_log = True
    ds.dissolve_speed = max(1, p["dissolve"])
    ds.use_adaptive_domain = True		# solves only round the smoke, so the base grid can be fine
    ds.adapt_margin = 12		# a fast burst outran 6 cells and was cut flat on the grid's faces
    # Mantaflow's wavelet noise crinkles smoke's edges like paper, so smoke's base res is better.  The
    # fire's flame field is near 1 inside and drops to 0 within two voxels, round lobes as smooth as
    # the emitters; there the crinkle is what tears the front into tongues.
    ds.use_noise = p.get("upres", 0) > 0
    if ds.use_noise:
        ds.noise_scale = int(p["upres"])
        ds.noise_strength = p["upres_strength"]
        ds.noise_pos_scale = p["upres_size"]
        ds.noise_time_anim = 0.3
    if fire:
        ds.burning_rate = p["burning_rate"]
        ds.flame_smoke = p["flame_smoke"]
        ds.flame_vorticity = p["flame_vorticity"]
        ds.flame_ignition = p["flame_ignition"]
        ds.flame_max_temp = p["flame_max_temp"]
        ds.flame_smoke_color = (0.12, 0.12, 0.12)
    ds.cache_type = "ALL"
    ds.cache_data_format = "OPENVDB"
    ds.cache_directory = os.path.join(root, "cache")
    ds.cache_frame_start, ds.cache_frame_end = 1, p["frame_end"]

    # Emitters: lumpy icospheres, each blowing for its own frames, so a burst comes out in lobes
    # rather than as one ball.  The lumps tumble, so the inflow never repeats one pattern.
    clouds = bpy.data.textures.new("Lumps", "CLOUDS")
    clouds.noise_scale = p["noise_scale"]
    clouds.noise_depth = 3
    tumbler = bpy.data.objects.new("Tumbler", None)
    scene.collection.objects.link(tumbler)
    tumbler.keyframe_insert("rotation_euler", frame=1)
    tumbler.rotation_euler = p["tumble"]
    tumbler.keyframe_insert("rotation_euler", frame=p["frame_end"])
    emitters = []
    for x, y, z, radius, first, last in p["emitters"]:
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=radius, location=(x, y, z))
        emitter = bpy.context.object
        if p.get("sweep") and not emitters:
            # a jet: the first lump flies sweep metres while it blows, laying a column of fuel
            # (initial velocity alone left the gas sitting on the emitter)
            bpy.context.preferences.edit.keyframe_new_interpolation_type = "LINEAR"
            emitter.keyframe_insert("location", frame=first)
            emitter.location = (x + p["sweep"][0], y + p["sweep"][1], z + p["sweep"][2])
            emitter.keyframe_insert("location", frame=last)
        emitters.append(emitter)
        displace = emitter.modifiers.new("Lumps", "DISPLACE")
        displace.texture = clouds
        displace.strength = p["lump"]
        displace.mid_level = 0.5
        displace.texture_coords = "OBJECT"
        displace.texture_coords_object = tumbler
        flow = emitter.modifiers.new("Fluid", "FLUID")
        flow.fluid_type = "FLOW"
        fs = flow.flow_settings
        fs.flow_type = "BOTH" if fire else "SMOKE"
        fs.flow_behavior = "INFLOW"
        fs.flow_source = "MESH"
        fs.volume_density = 1.0
        fs.surface_distance = p.get("surface", 0.6)
        fs.density = p["density"]
        fs.temperature = p["temperature"]
        if fire:
            fs.fuel_amount = p["fuel"]
            fs.smoke_color = (0.15, 0.15, 0.15)
        else:
            fs.smoke_color = (1.0, 1.0, 1.0)
        fs.use_initial_velocity = True
        fs.velocity_normal = p["velocity_normal"]
        fs.velocity_random = p["velocity_random"]
        fs.velocity_coord = p.get("velocity_coord", (0.0, 0.0, p["velocity_up"]))
        fs.use_texture = True
        fs.noise_texture = clouds
        fs.texture_size = 1.4
        fs.subframes = p.get("subframes", 2)
        keys = [(first - 1, False), (first, True), (last, True), (last + 1, False)]
        if p["pulse"]:
            # on for a pulse, off for half one: the plume rises as separate puffs
            keys = [(1, True)]
            frame, on = 1, True
            while frame < p["frame_end"]:
                frame += p["pulse"] if on else max(2, p["pulse"] // 2)
                on = not on
                keys.append((frame - 1, not on))
                keys.append((frame, on))
        for frame, on in keys:
            if 1 <= frame <= p["frame_end"]:
                fs.use_inflow = on
                fs.keyframe_insert("use_inflow", frame=frame)

    if not (os.environ.get("SMOKE_REUSE") and os.path.isdir(os.path.join(root, "cache", "data"))):
        with bpy.context.temp_override(object=domain, active_object=domain, selected_objects=[domain]):
            bpy.ops.fluid.bake_all()
    for emitter in emitters:
        emitter.hide_render = True

    material = bpy.data.materials.new("Volume")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    volume = nodes.new("ShaderNodeVolumePrincipled")
    links.new(volume.outputs["Volume"], out.inputs["Volume"])

    def attribute(name):
        node = nodes.new("ShaderNodeAttribute")
        node.attribute_name = name
        return node.outputs["Fac"]

    def math(operation, a, b, clamp=False):
        node = nodes.new("ShaderNodeMath")
        node.operation = operation
        node.use_clamp = clamp
        for socket, value in zip(node.inputs, (a, b)):
            if isinstance(value, (int, float)):
                socket.default_value = value
            else:
                links.new(value, socket)
        return node.outputs[0]

    # The sim's near-zero residue would fill the cell with haze, so a floor comes off first.
    density = math("SUBTRACT", attribute("density"), 0.004, clamp=True)
    if fire:
        # Soot: dark and dense, so only the outer skin of the flame shows, broken by soot lanes, and
        # it is lit orange by it.  A thin dense soot is what turned the fireball into one evenly
        # glowing ball: emission summed through the whole volume flattens every fold.  Flame: a
        # black body from 800 K where the flame field is faint to 4200 K at its hottest, the field
        # shaped so most of it sits low (orange, red) and only the core runs hot, its strength
        # rising steeply with the field, so the core, past the tone curve, bleaches to white.
        volume.inputs["Color"].default_value = (0.14, 0.13, 0.12, 1.0)
        volume.inputs["Absorption Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        # The sim's flame field sits near 1 through the whole young fireball, so it drew as one flat
        # glowing lump.  A 4D noise shifts it, flame only, the soot untouched: the skin breaks into
        # hot licks and darker folds, and at the rim the field's isosurfaces are torn.  W moves
        # with the frame, so the licks flicker instead of sliding through a fixed pattern.
        coords = nodes.new("ShaderNodeTexCoord")
        stretch = nodes.new("ShaderNodeMapping")
        stretch.inputs["Scale"].default_value = (1.0, 1.0, 0.6)		# taller than wide: licks, not grain
        links.new(coords.outputs["Object"], stretch.inputs["Vector"])
        noise = nodes.new("ShaderNodeTexNoise")
        noise.noise_dimensions = "4D"
        noise.inputs["Scale"].default_value = p["flame_noise_scale"]
        noise.inputs["Detail"].default_value = 8.0
        noise.inputs["Roughness"].default_value = 0.62
        noise.inputs["Distortion"].default_value = 0.6
        links.new(stretch.outputs["Vector"], noise.inputs["Vector"])
        # Added, not multiplied: a fixed shift tears the thin rim and leaves the deep field whole.
        # It fades out where the field falls under flame_gate, or it lit flame in empty air; above
        # that, in the sim's faint halo, it raises tongues out past the field's soft edge.
        raw = attribute("flame")
        shift = nodes.new("ShaderNodeMath")
        shift.operation = "MULTIPLY_ADD"
        links.new(noise.outputs["Fac"], shift.inputs[0])
        shift.inputs[1].default_value = 2.0 * p["flame_noise"]
        shift.inputs[2].default_value = -p["flame_noise"]
        gate = math("MULTIPLY", raw, 1.0 / p["flame_gate"], clamp=True)
        torn = math("ADD", raw, math("MULTIPLY", shift.outputs[0], gate))
        flame = math("SUBTRACT", torn, 0.01, clamp=True)
        # A hard floor on the torn field, so the flame's body ends in a cut, not a faint fringe.
        edge = nodes.new("ShaderNodeMapRange")
        edge.interpolation_type = "SMOOTHSTEP"
        links.new(flame, edge.inputs["Value"])
        edge.inputs["From Min"].default_value = p["flame_edge"]
        edge.inputs["From Max"].default_value = p["flame_edge"] * 1.6
        shaped = math("POWER", flame, p["kelvin_shape"])
        kelvin = nodes.new("ShaderNodeMath")
        kelvin.operation = "MULTIPLY_ADD"
        links.new(shaped, kelvin.inputs[0])
        kelvin.inputs[1].default_value = p["kelvin_span"]
        kelvin.inputs[2].default_value = p["kelvin_low"]
        body = nodes.new("ShaderNodeBlackbody")
        links.new(kelvin.outputs[0], body.inputs["Temperature"])
        links.new(body.outputs["Color"], volume.inputs["Emission Color"])
        strength = math("MULTIPLY", math("POWER", flame, p["flame_power"]), p["flame_strength"])
        links.new(math("MULTIPLY", strength, edge.outputs["Result"]), volume.inputs["Emission Strength"])
        # The flame is opaque too, as a sooty flame is: light summed through a transparent glow
        # fades at the rim along a shrinking chord, a soft limb however hard the field's edge; an
        # opaque flame shows its outer skin, cut sharp, and hides its hottest core behind it.
        body_density = math("MULTIPLY", math("MULTIPLY", flame, edge.outputs["Result"]), p["flame_density"])
        links.new(math("ADD", math("MULTIPLY", density, p["soot_scale"]), body_density), volume.inputs["Density"])
    else:
        # Dense and dark: low albedo, so the side away from the sun falls into its own shadow
        # instead of glowing with scattered light, which is what made v1 cotton wool.
        curve = math("POWER", density, p["density_power"])
        links.new(math("MULTIPLY", curve, p["density_scale"]), volume.inputs["Density"])
        volume.inputs["Color"].default_value = (p["albedo"],) * 3 + (1.0,)
        volume.inputs["Absorption Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        volume.inputs["Anisotropy"].default_value = 0.3
    # Linear interpolation of the grid drew its voxels as vertical streaks and stair steps through
    # a steep emission curve; cubic smooths the field, the noise above puts the detail back.
    if fire:
        material.cycles.volume_interpolation = "CUBIC"
    domain.data.materials.append(material)

    world = bpy.data.worlds.new("Sky")
    world.use_nodes = True
    background = world.node_tree.nodes["Background"]
    background.inputs["Color"].default_value = (0.04, 0.04, 0.045, 1.0)
    background.inputs["Strength"].default_value = 1.0
    scene.world = world
    sun_data = bpy.data.lights.new("Sun", "SUN")
    sun_data.energy = 3.0 if fire else 10.0		# fire: the soot's lit side, under the flame's light
    sun_data.angle = 0.1
    sun = bpy.data.objects.new("Sun", sun_data)
    # low and from the camera's left, a little in front: the lobes get a lit flank and a dark one
    travel = Vector((0.8, 0.3, -0.52)).normalized()
    sun.rotation_euler = travel.to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(sun)

    # Camera: orthographic, 20 degrees above the horizon, like the game's billboards are seen.
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = p["ortho"]
    cam = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam)
    tilt = math_radians(p.get("tilt", 20.0))		# the nuke looks: the RTS camera's own 37
    cam.location = (0.0, -12.0 * math_cos(tilt), p["aim_z"] + 12.0 * math_sin(tilt))
    cam.rotation_euler = (math_pi / 2 - tilt, 0.0, 0.0)
    cam_data.clip_end = 40.0
    scene.camera = cam

    scene.render.engine = "CYCLES"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "OPTIX"
    prefs.get_devices()
    for device in prefs.devices:
        device.use = device.type != "CPU"
    scene.cycles.device = "GPU"
    scene.cycles.samples = p["samples"]
    scene.cycles.use_denoising = not fire		# the denoiser smeared fire's fine tongues into streaks
    scene.cycles.volume_bounces = 1
    scene.cycles.volume_step_rate = 0.5
    scene.cycles.volume_max_steps = 2048
    scene.render.film_transparent = True
    scene.render.resolution_x = scene.render.resolution_y = RENDER
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    # Fire goes out linear and premultiplied: a PNG's straight alpha cannot hold light where nothing
    # is opaque, and the flame's brightness is tone mapped in pack (tone_map).
    scene.render.image_settings.file_format = "OPEN_EXR" if fire else "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "16"
    os.makedirs(os.path.join(root, "frames"), exist_ok=True)
    count = frame_count(p) + (LOOP_BLEND if p["loop"] else 0)
    pick = os.environ.get("SMOKE_PICK")
    for index in ([int(v) for v in pick.split(",")] if pick else range(count)):
        frame = p["first"] + index * p["step"]
        scene.frame_set(frame)
        if fire:
            noise.inputs["W"].default_value = frame * p["flame_noise_speed"]
        scene.render.filepath = os.path.join(root, "frames", "%04d.%s" % (index, "exr" if fire else "png"))
        bpy.ops.render.render(write_still=True)


math_radians, math_cos, math_sin, math_pi = math.radians, math.cos, math.sin, math.pi


# ---------------------------------------------------------------------------------------------
# Packing: crop, filter, loop, atlas, DXT.

def read_png16(path):
    """A 16-bit PNG as float (h, w, 4); Pillow cannot, so ffmpeg decodes it."""
    import numpy as np
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                            "stream=width,height", "-of", "csv=p=0", path], capture_output=True, text=True, check=True)
    w, h = (int(v) for v in probe.stdout.strip().split(","))
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgba64le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, "<u2").reshape(h, w, 4).astype(np.float32) / 65535.0


def read_exr(path):
    """A linear, premultiplied RGBA EXR as float (h, w, 4), through ffmpeg."""
    import numpy as np
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                            "stream=width,height", "-of", "csv=p=0", path], capture_output=True, text=True, check=True)
    w, h = (int(v) for v in probe.stdout.strip().split(","))
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "gbrapf32le", "-"],
                         capture_output=True, check=True).stdout
    g, b, r, a = np.frombuffer(raw, "<f4").reshape(4, h, w)
    return np.stack((r, g, b, a), axis=-1).astype(np.float32)


FIRE_EXPOSURE = 1.0		# tone curve: 1 / the brightness at 1 - 1/e
FIRE_SATURATION = 1.35	# linear sRGB greys out a black body's orange
FIRE_FILM = 0.6			# share of a flame's colour taken from FIRE_RAMP
FIRE_EDGE = 1.6			# the flame's coverage is its brightness to this power: a dim rim covers less,
						# so a tongue ends in a cut, not a red halo
# What film makes of a fire by brightness, linear: soot red, deep red, orange, yellow, white.  A bare
# black body of 4000 K comes out peach, which read as the pale clay of v3.3.0.
FIRE_RAMP = ((0.0, (0.02, 0.002, 0.0)), (0.25, (0.35, 0.03, 0.005)), (0.5, (0.85, 0.22, 0.02)),
             (0.75, (1.0, 0.55, 0.1)), (0.9, (1.0, 0.8, 0.35)), (1.0, (1.0, 0.95, 0.8)))


def tone_map(frames):
    """Linear premultiplied fire to display premultiplied: each pixel's brightest channel through
    1 - exp(-x), the hue saturated and, by how coloured the pixel is (flame, not grey soot), pulled
    towards FIRE_RAMP at its brightness, then sRGB.  Its alpha is at least its brightness, since an
    alpha-blended card can only show light where it covers: the flame covers what is behind it, the
    soot does by its own opacity."""
    import numpy as np
    rgb = np.maximum(frames[..., :3], 0.0) * FIRE_EXPOSURE
    peak = rgb.max(axis=-1, keepdims=True)
    chroma = np.where(peak > 1e-4, 1.0 - rgb.min(axis=-1, keepdims=True) / np.maximum(peak, 1e-4), 0.0)		# 0 grey soot, 1 pure hue
    mapped = 1.0 - np.exp(-peak)
    rgb = rgb * (mapped / np.maximum(peak, 1e-6))
    grey = rgb.mean(axis=-1, keepdims=True)
    rgb = np.clip(grey + (rgb - grey) * FIRE_SATURATION, 0.0, mapped)		# film's saturated orange
    stops = np.array([s for s, _ in FIRE_RAMP])
    film = np.stack([np.interp(mapped[..., 0], stops, [c[k] for _, c in FIRE_RAMP]) for k in range(3)], axis=-1)
    weight = np.clip(chroma * 2.0, 0.0, 1.0) * FIRE_FILM
    rgb = rgb * (1.0 - weight) + film * weight
    rgb = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * np.power(np.maximum(rgb, 0.0031308), 1.0 / 2.4) - 0.055)
    alpha = np.clip(frames[..., 3:], 0.0, 1.0)
    alpha = np.maximum(alpha, np.clip(rgb.max(axis=-1, keepdims=True) * 1.4, 0.0, 1.0) ** FIRE_EDGE)
    return np.concatenate((np.minimum(rgb, alpha), alpha), axis=-1)


# The stylize grade: the owner found the filmed fire out of the game's spirit ("fazla gerçekçi"), so
# every fire card goes through EA's own look after the tone curve.  EA's fireball cards
# (EXFireball01-04 in the retail bigs) are a flat fill: their texels' 10th, 50th and 90th
# percentile colours are an orange (0.6, 0.35, 0.0), a gold (0.8, 0.6, 0.22; hue 38, saturation 0.73,
# value 0.8) and a cream (0.84, 0.72, 0.55), 70% of them in the gold, little fine detail inside, a
# hard ragged edge.  So the flame (not the grey soot) is painted in those three: its value, smoothed
# by a 3x3 median and a wide blur so no fine detail inside survives, and set against the frame's own
# flame (its 60th percentile), picks the band, mostly the gold.  Set against the frame, a dimming
# flame stays gold until it is gone, so the soot takes over late, as EA's do.  A first grade of five
# bands up a ramp to pale yellow was too bright, dark red inside and blotchy up close.
STYLE_LOW = (0.76, 0.40, 0.05)		# the shadow band: EA's 10th percentile read brown on a flame's
									# tongues, so a brighter orange of the same hue
STYLE_MID = (0.82, 0.58, 0.18)		# the fill, EA's median a touch richer: most of every flame
STYLE_HIGH = (0.85, 0.71, 0.50)		# the highlight, EA's 90th; blue 0.5, never white
									# (EA's own three exactly came out a shade paler than EA: 0.71 to 0.81)
STYLE_BANDS = (0.55, 1.1, 0.07)		# value / the frame's flame (its 60th percentile) for the low and
									# the high band, blend half width
STYLE_SHADE = (0.86, 0.14)			# the fill times a + b * the smoothed value: one band, not one flat card
STYLE_BLUR = 3						# box blur radius, twice, on the value that picks the band (256 cells)
STYLE_EDGE = (0.15, 0.45)			# flame coverage smoothstep: a hard ragged rim
STYLE_SOOT_EDGE = (0.08, 0.5)		# soot coverage: its thin fringe trimmed, less soot area


def median3(image):
    """A 3x3 median of an (h, w, c) image, its edge repeated."""
    import numpy as np
    h, w = image.shape[:2]
    padded = np.pad(image, ((1, 1), (1, 1), (0, 0)), mode="edge")
    return np.median(np.stack([padded[y:y + h, x:x + w] for y in range(3) for x in range(3)]), axis=0)


def box_blur(image, radius):
    """An (h, w, c) image blurred by a box of 2 radius + 1, twice (close to a Gaussian), edge repeated."""
    import numpy as np
    for _ in range(2):
        for axis in (0, 1):
            pad = [(0, 0)] * image.ndim
            pad[axis] = (radius + 1, radius)
            total = np.cumsum(np.pad(image, pad, mode="edge"), axis=axis)
            n = image.shape[axis]
            image = (np.take(total, np.arange(2 * radius + 1, 2 * radius + 1 + n), axis=axis)
                     - np.take(total, np.arange(n), axis=axis)) / (2 * radius + 1)
    return image


def smoothstep(e0, e1, x):
    import numpy as np
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def stylize(frames):
    """Display-space premultiplied fire (n, h, w, 4) painted in EA's flat fireball colours (STYLE_*)."""
    import numpy as np
    out = np.empty_like(frames)
    low, mid, high = (np.array(c, np.float32) for c in (STYLE_LOW, STYLE_MID, STYLE_HIGH))
    t1, t2, soft = STYLE_BANDS
    for i, frame in enumerate(frames):
        alpha = frame[..., 3:]
        straight = np.where(alpha > 1e-3, frame[..., :3] / np.maximum(alpha, 1e-3), 0.0)
        smooth_ = median3(np.concatenate((straight, alpha), axis=-1))
        straight, alpha = smooth_[..., :3], smooth_[..., 3:]
        value = straight.max(axis=-1, keepdims=True)
        chroma = 1.0 - straight.min(axis=-1, keepdims=True) / np.maximum(value, 1e-3)
        # 0 soot, 1 flame: coloured and bright.  By colour alone the soot the flame lights orange went
        # gold too, and a cooled fireball turned into a muddy brown cloud.
        flame = smoothstep(0.35, 0.65, chroma) * smoothstep(0.12, 0.3, value)		# a deep red still burns
        # the value that picks the band: alpha-weighted and blurred, so the rim does not darken it
        blurred = box_blur(np.concatenate((value * alpha, alpha), axis=-1), STYLE_BLUR)
        shade = blurred[..., :1] / np.maximum(blurred[..., 1:], 1e-3)
        lit = (flame[..., 0] > 0.5) & (alpha[..., 0] > 0.3)
        reference = np.clip(np.percentile(shade[..., 0][lit], 60) if lit.any() else 1.0, 0.5, 1.0)
        shade = shade / reference
        w_high = smoothstep(t2 - soft, t2 + soft, shade)
        w_low = 1.0 - smoothstep(t1 - soft, t1 + soft, shade)
        paint = mid * (1.0 - w_high - w_low) + high * w_high + low * w_low
        paint = paint * (STYLE_SHADE[0] + STYLE_SHADE[1] * np.clip(shade, 0.0, 1.2))		# broad brush shading
        colour = np.clip(straight * (1.0 - flame) + paint * flame, 0.0, 1.0)		# the soot keeps its own grey
        cover = (alpha * smoothstep(*STYLE_SOOT_EDGE, alpha) * (1.0 - flame)
                 + smoothstep(*STYLE_EDGE, alpha) * flame)
        out[i] = np.concatenate((colour * cover, cover), axis=-1)
    return out


# The flame's brightness, the 90th percentile of its lit texels, a frame should show at each share of
# the burn, linear.  The sim's own runs from about 120 at ignition to under 1 as the fuel goes, and
# no single exposure holds both: at ignition every texel was past white, a flat cream blob with no
# hot core.  Exposing each frame to this key keeps a core-to-edge gradient inside every frame while
# the whole still cools from white-yellow to red.
# At 2.6 for the ignition the first rows came out cream to the rim, and in the game, under the
# additive cores and the bloom they feed, a flat white flash; at 1.5 only the core bleaches.
FIRE_KEY = ((0.0, 1.5), (0.15, 1.35), (0.35, 1.05), (0.6, 0.62), (1.0, 0.32))


def expose_burn(frames):
    """frames (n, h, w, 4) linear premultiplied fire, each scaled to FIRE_KEY at its place in the run,
    the scale smoothed over frames so the exposure never flickers, and never raised more than
    twice, so a dying ember is not lit up."""
    import numpy as np
    level = np.array([np.percentile(f[..., :3].max(axis=-1)[f[..., :3].max(axis=-1) > 0.02], 90)
                      if (f[..., :3].max(axis=-1) > 0.02).any() else 0.0 for f in frames])
    at = np.linspace(0.0, 1.0, len(frames))
    key = np.interp(at, [k for k, _ in FIRE_KEY], [v for _, v in FIRE_KEY])
    scale = np.clip(key / np.maximum(level, 1e-4), 0.0, 2.0)
    scale = np.exp(smooth(np.log(np.maximum(scale, 1e-4)), 5))
    frames = frames.copy()
    frames[..., :3] *= scale[:, None, None, None]
    return frames


def expose_steady(frames, key):
    """frames (n, h, w, 4) linear premultiplied fire that burns steadily, all scaled by one factor so
    their median flame brightness is key: the flicker stays the sim's own."""
    import numpy as np
    lit = frames[..., :3].max(axis=-1)
    level = np.median([np.percentile(l[l > 0.02], 90) for l in lit if (l > 0.02).any()])
    frames = frames.copy()
    frames[..., :3] *= key / max(level, 1e-4)
    return frames


def load_sequence(folder, fire=False, key=None):
    """Premultiplied frames: smoke from straight-alpha PNGs, fire from linear EXRs, exposed along the
    burn (or to one steady key) and tone mapped."""
    import numpy as np
    names = sorted(n for n in os.listdir(folder) if n.endswith(".exr" if fire else ".png"))
    if fire:
        frames = np.stack([read_exr(os.path.join(folder, n)) for n in names])
        return tone_map(expose_steady(frames, key) if key else expose_burn(frames))
    frames = np.stack([read_png16(os.path.join(folder, n)) for n in names])
    frames[..., :3] *= frames[..., 3:]
    return frames


def crossfade_loop(frames):
    """frames FRAMES + LOOP_BLEND long: FRAMES that loop, the tail faded into the head, so the frame
    after the last is the one the sim had next."""
    out = frames[:FRAMES].copy()
    for i in range(LOOP_BLEND):
        w = (i + 0.5) / LOOP_BLEND
        out[i] = frames[i] * w + frames[FRAMES + i] * (1.0 - w)
    return out


def smooth(values, width=9):
    import numpy as np
    kernel = np.ones(width) / width
    padded = np.pad(values, width // 2, mode="edge")
    return np.convolve(padded, kernel, mode="valid")


def crop_frames(frames, cell, growth, loop, base=None, headroom=MARGIN, by_light=False):
    """Each frame cropped to a square round its content and filtered to cell.  The square follows
    the content's smoothed centre; its size is the largest extent times (extent / largest) ** growth,
    so growth 0 keeps the sim's own expansion in one square and 1 fits every frame to its cell,
    leaving growth to the INI's Size keys.  A looping look takes one square for all frames, or the seam would jump.  base,
    a share of the cell from its top, puts the content's foot there instead of centring it: a card
    stands on its particle's place, and ground dust centred on it sank half into the ground.  A
    looping look with a base leaves headroom, a share of the cell, above its tallest content.
    by_light frames what glows, not what covers: a steady fire's soot column is metres tall, and
    framed by it the flame filled a third of its cell."""
    import numpy as np
    solid = frames[..., :3].max(axis=-1) > 0.2 if by_light else frames[..., 3] > THRESHOLD
    boxes = []
    for mask in solid:
        ys, xs = np.nonzero(mask)
        boxes.append((ys.min(), ys.max(), xs.min(), xs.max()) if len(ys) else boxes[-1])
    boxes = np.array(boxes, np.float32)
    cy = (boxes[:, 0] + boxes[:, 1]) / 2.0
    cx = (boxes[:, 2] + boxes[:, 3]) / 2.0
    extent = np.maximum(boxes[:, 1] - boxes[:, 0], boxes[:, 3] - boxes[:, 2])
    if loop:
        cy[:] = (boxes[:, 0].min() + boxes[:, 1].max()) / 2.0
        cx[:] = (boxes[:, 2].min() + boxes[:, 3].max()) / 2.0
        extent[:] = max(boxes[:, 1].max() - boxes[:, 0].min(), boxes[:, 3].max() - boxes[:, 2].min())
    else:
        cy, cx = smooth(cy), smooth(cx)
        extent = np.maximum.accumulate(smooth(extent))		# never shrinks: it thins, it does not pull in
    largest = extent.max()
    sides = largest * (extent / largest) ** growth / (1.0 - 2.0 * MARGIN)
    if base is not None and loop:
        # one foot for the whole loop, the deepest, and room above the tallest tongue
        height = boxes[:, 1].max() - boxes[:, 0].min()
        sides = np.maximum(sides, height / (base - headroom))
        cy = np.full(len(frames), boxes[:, 1].max()) - sides * (base - 0.5)
    elif base is not None:
        # room above the foot for the whole cloud: two of the dust variants rose past the square
        # their width gave them and were cut flat along the top
        foot = smooth(boxes[:, 1])
        sides = np.maximum(sides, np.maximum.accumulate(foot - boxes[:, 0]) / (base - MARGIN))
        cy = foot - sides * (base - 0.5)
    out = np.zeros((len(frames), cell, cell, 4), np.float32)
    pad = int(sides.max())
    padded = np.pad(frames, ((0, 0), (pad, pad), (pad, pad), (0, 0)))
    for i in range(len(frames)):
        half = sides[i] / 2.0
        top, left = int(round(cy[i] - half)) + pad, int(round(cx[i] - half)) + pad
        side = int(round(sides[i]))
        out[i] = resize(padded[i:i + 1, top:top + side, left:left + side], cell)[0]
    return out


def resize(frames, cell):
    import numpy as np
    from PIL import Image
    out = np.zeros((len(frames), cell, cell, frames.shape[-1]), np.float32)
    for i, frame in enumerate(frames):
        for c in range(frames.shape[-1]):
            out[i, ..., c] = np.asarray(Image.fromarray(frame[..., c]).resize((cell, cell), Image.LANCZOS))
    return np.clip(out, 0.0, 1.0)


def window(cell, round_=False):
    """1 inside, falling smoothly to 0 over the outer MARGIN of a cell; round_ fades from the
    inscribed circle's inner half instead, for a look cut out of a larger plume."""
    import numpy as np
    axis = np.arange(cell) / (cell - 1.0)
    if round_:
        y, x = np.meshgrid(axis - 0.5, axis - 0.5, indexing="ij")
        ramp = np.clip((0.5 - MARGIN - np.hypot(x, y)) / 0.22, 0.0, 1.0)
        return ramp * ramp * (3.0 - 2.0 * ramp)
    edge = np.minimum(axis, 1.0 - axis) / MARGIN
    ramp = np.clip(edge, 0.0, 1.0)
    ramp = ramp * ramp * (3.0 - 2.0 * ramp)
    return np.outer(ramp, ramp)


def crop_fixed(frames, cell, crop):
    """The same square, given as shares of the render, from every frame."""
    size = frames.shape[1]
    cx, cy, side = (v * size for v in crop)
    top, left = int(round(cy - side / 2)), int(round(cx - side / 2))
    return resize(frames[:, top:top + int(side), left:left + int(side)], cell)


def frame_count(p):
    """A look's frames: GRID by GRID, its own grid's (the muzzle jet is 4 by 4), or a variant run's
    VARIANT_FRAMES."""
    return p.get("frames") or p.get("grid", GRID) ** 2


def sheet(frames):
    """(n * n, cell, cell, c) to one (n cell, n cell, c) image, row by row from the top left."""
    import numpy as np
    cell = frames.shape[1]
    grid = int(round(len(frames) ** 0.5))
    image = np.zeros((grid * cell, grid * cell, frames.shape[-1]), np.float32)
    for index, frame in enumerate(frames):
        row, column = divmod(index, grid)
        image[row * cell:(row + 1) * cell, column * cell:(column + 1) * cell] = frame
    return image


def atlas(frames):
    """Premultiplied frames (64, cell, cell, 4) to a straight-alpha atlas (8 cell, 8 cell, 4)."""
    import numpy as np
    image = sheet(frames)
    alpha = image[..., 3:]
    grey = image[..., :3].sum() / max(alpha.sum() * 3.0, 1e-6)		# the mean colour under the smoke
    image[..., :3] = np.where(alpha > 1e-3, image[..., :3] / np.maximum(alpha, 1e-3), grey)
    return np.clip(image, 0.0, 1.0)


def to565(rgb):
    import numpy as np
    r = np.round(rgb[..., 0] * 31.0 / 255.0).astype(np.uint32)
    g = np.round(rgb[..., 1] * 63.0 / 255.0).astype(np.uint32)
    b = np.round(rgb[..., 2] * 31.0 / 255.0).astype(np.uint32)
    return (r << 11) | (g << 5) | b


def from565(c):
    import numpy as np
    r = (c >> 11) & 31
    g = (c >> 5) & 63
    b = c & 31
    return np.stack(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)), axis=-1).astype(np.float32)


def blocks(image):
    h, w, c = image.shape
    return image.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)


def colour_blocks(rgb):
    """rgb (n, 16, 3) in 0..255: n DXT colour blocks in four colour mode, endpoints on the principal
    axis inset by a sixteenth."""
    import numpy as np
    mean = rgb.mean(axis=1, keepdims=True)
    centred = rgb - mean
    axis = np.linalg.eigh(np.einsum("nki,nkj->nij", centred, centred))[1][:, :, 2]
    along = np.einsum("nki,ni->nk", centred, axis)
    low = np.clip(mean[:, 0] + axis * along.min(axis=1, keepdims=True), 0.0, 255.0)
    high = np.clip(mean[:, 0] + axis * along.max(axis=1, keepdims=True), 0.0, 255.0)
    inset = (high - low) / 16.0
    c0 = to565(high - inset)
    c1 = to565(low + inset)
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0 = from565(c0)
    p1 = from565(c1)
    palette = np.stack((p0, p1, (2.0 * p0 + p1) / 3.0, (p0 + 2.0 * p1) / 3.0), axis=1)
    distance = ((rgb[:, :, None, :] - palette[:, None, :, :]) ** 2).sum(axis=-1)
    index = distance.argmin(axis=-1).astype(np.uint32)
    index[c0 == c1] = 0
    bits = (index << (2 * np.arange(16, dtype=np.uint32))).sum(axis=1, dtype=np.uint64).astype(np.uint32)
    out = np.zeros((rgb.shape[0], 8), np.uint8)
    out[:, 0:2] = c0.astype("<u2").view(np.uint8).reshape(-1, 2)
    out[:, 2:4] = c1.astype("<u2").view(np.uint8).reshape(-1, 2)
    out[:, 4:8] = bits.astype("<u4").view(np.uint8).reshape(-1, 4)
    return out


def alpha_blocks(alpha):
    """alpha (n, 16) in 0..255: n DXT5 alpha blocks, eight value mode."""
    import numpy as np
    a0 = alpha.max(axis=1).round().astype(np.uint32)
    a1 = alpha.min(axis=1).round().astype(np.uint32)
    weights = np.array([0, 7, 1, 2, 3, 4, 5, 6], np.float32)
    palette = np.floor(a0[:, None] + (a1[:, None].astype(np.float32) - a0[:, None]) * weights / 7.0 + 0.5)
    index = np.abs(alpha[:, :, None] - palette[:, None, :]).argmin(axis=-1).astype(np.uint64)
    index[a0 == a1] = 0
    bits = (index << (3 * np.arange(16, dtype=np.uint64))).sum(axis=1, dtype=np.uint64)
    out = np.zeros((alpha.shape[0], 8), np.uint8)
    out[:, 0] = a0
    out[:, 1] = a1
    out[:, 2:8] = bits.astype("<u8").view(np.uint8).reshape(-1, 8)[:, :6]
    return out


def clear_cell_rings(level, cell):
    alpha = level[..., 3]
    for edge in range(0, level.shape[0], cell):
        alpha[edge, :] = alpha[edge + cell - 1, :] = 0.0
        alpha[:, edge] = alpha[:, edge + cell - 1] = 0.0
    return level


def mips(image, cell):
    """Down to SMALLEST_CELL frames plus THROWAWAY_LEVELS, alpha-weighted box filter, every level's
    frame rings cleared so no mip pulls a neighbour onto a quad's edge."""
    import numpy as np
    levels = [clear_cell_rings(image.copy(), cell)]
    while cell > SMALLEST_CELL >> THROWAWAY_LEVELS:
        top = levels[-1]
        weighted = np.concatenate((top[..., :3] * top[..., 3:], top[..., 3:]), axis=-1)
        h, w, c = weighted.shape
        level = weighted.reshape(h // 2, 2, w // 2, 2, c).mean(axis=(1, 3))
        alpha = level[..., 3:]
        level = np.concatenate((np.where(alpha > 1e-4, level[..., :3] / np.maximum(alpha, 1e-4), top[::2, ::2, :3]), alpha), axis=-1)
        cell //= 2
        levels.append(clear_cell_rings(level, cell))
    return levels


def dds(image, cell):
    """Straight-alpha image (h, w, 4) in 0..1 as DXT5 DDS bytes with mips."""
    import numpy as np
    levels = mips(image, cell)
    payload = []
    for level in levels:
        if level.shape[0] < 4:
            level = np.pad(level, ((0, 4 - level.shape[0]), (0, 4 - level.shape[1]), (0, 0)), mode="edge")
        texels = blocks(np.clip(np.round(level * 255.0), 0.0, 255.0))
        payload.append(np.concatenate((alpha_blocks(texels[:, :, 3]), colour_blocks(texels[:, :, :3])), axis=1).tobytes())
    h, w = image.shape[:2]
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    header = b"DDS " + struct.pack("<7I", 124, flags, h, w, len(payload[0]), 0, len(levels)) + bytes(44)
    header += struct.pack("<2I4s5I", 32, 0x4, b"DXT5", 0, 0, 0, 0, 0)
    header += struct.pack("<5I", 0x8 | 0x1000 | 0x400000, 0, 0, 0, 0)
    assert len(header) == 128
    return header + b"".join(payload)


def build(frames, p):
    """Rendered premultiplied frames to the 64 (or the look's grid's) premultiplied frames that go
    in the atlas."""
    import numpy as np
    cell = p.get("cell", CELL)
    count = frame_count(p)
    if p["loop"]:
        frames = crossfade_loop(frames)
    elif p.get("retime", 1.0) != 1.0:
        # more of the run on the burning, less on the soot: EA's fireballs turn to smoke late
        at = np.linspace(0.0, 1.0, count) ** p["retime"]
        frames = frames[np.round(at * (min(len(frames), count) - 1)).astype(int)]
    # A jet is simulated along +x, its root on the left: turned so the root is the foot, framed like
    # a standing fire (one square, foot near the bottom), then flipped so the root is at the top.
    if p.get("root_up"):
        frames = np.ascontiguousarray(np.rot90(frames, 1, axes=(1, 2)))
    fixed = p["loop"] or p.get("root_up", False) or p.get("fixed", False)
    if p["crop"]:
        frames = crop_fixed(frames[:count], cell, p["crop"])
    else:
        frames = crop_frames(frames[:count], cell, p["growth"], fixed, p.get("base"),
                             p.get("headroom", MARGIN), p.get("by_light", p["kind"] == "fire" and fixed))
    if p.get("root_up"):
        frames = np.ascontiguousarray(frames[:, ::-1])
    if p["kind"] == "fire" and p.get("stylize", True):
        frames = stylize(frames)
    frames *= window(cell, p["crop"] is not None)[None, :, :, None]
    if p["kind"] == "fire":
        return frames		# tone mapped on load, then stylized
    if not p["loop"]:
        frames = hold_density(frames, p["hold"])
    return grade(frames, p["gamma"])


def hold_density(frames, hold):
    """A spreading puff thins to nothing by its last frames, which the INI's Alpha keys should
    decide, not the texture.  Each frame's density (optical depth, -log(1 - alpha)) is scaled so its
    densest part keeps part of the first frame's, smoothly: the shape stays readable to the end."""
    import numpy as np
    alpha = np.clip(frames[..., 3], 0.0, 0.999)
    peak = np.array([np.percentile(a[a > 0.02], 98) if (a > 0.02).any() else 0.02 for a in alpha])
    depth = -np.log(1.0 - peak)
    reference = min(depth[0], 1.5)			# a saturated first frame would ask for everything
    gain = np.clip((reference / np.maximum(smooth(depth), 1e-3)) ** hold, 1.0, 4.0)
    held = 1.0 - (1.0 - alpha) ** gain[:, None, None]
    scale = np.where(alpha > 1e-4, held / np.maximum(alpha, 1e-4), 1.0)
    return frames * scale[..., None]


def grade(frames, gamma):
    """Levels on the straight colour: the bright side of the smoke to near white, so the INI's Color
    is what the lit side shows, and a gamma that deepens the self-shadow."""
    import numpy as np
    alpha = frames[..., 3:]
    straight = frames[..., :3] / np.maximum(alpha, 1e-3)
    solid = alpha[..., 0] > 0.3
    top = np.percentile(straight[solid].mean(axis=-1), 97) if solid.any() else 1.0
    straight = np.clip(straight * (0.97 / top), 0.0, 1.0) ** gamma
    alpha = np.clip((alpha - ALPHA_FLOOR) / (1.0 - ALPHA_FLOOR), 0.0, 1.0)
    return np.concatenate((straight * alpha, alpha), axis=-1)


# ---------------------------------------------------------------------------------------------
# Previews.

def over(frames, background):
    """Premultiplied frames over a background (h, w, 3)."""
    return frames[..., :3] + background * (1.0 - frames[..., 3:])


def battlefield(size, seed=5):
    """A dark olive-brown ground: broad patches and a fine grit, nothing at the smoke's own scale."""
    import numpy as np
    from PIL import Image
    rng = np.random.default_rng(seed)
    broad = np.asarray(Image.fromarray(rng.random((3, 3)).astype(np.float32)).resize((size, size), Image.BICUBIC))
    grit = rng.random((size, size)).astype(np.float32)
    base = 0.75 + 0.35 * broad + 0.12 * (grit - 0.5)
    colour = np.array([0.24, 0.20, 0.14], np.float32)
    return np.clip(colour * base[..., None], 0.0, 1.0)


def save_png(path, rgb):
    import numpy as np
    from PIL import Image
    Image.fromarray(np.uint8(np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5)).save(path)


def encode(path, frames_rgb, fps, loops=3):
    """frames (n, h, w, 3) in 0..1 to an H.264 MP4."""
    import numpy as np
    n, h, w, _ = frames_rgb.shape
    data = np.uint8(np.clip(frames_rgb, 0.0, 1.0) * 255.0 + 0.5)
    data = np.concatenate([data] * loops)
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (w, h),
                    "-r", str(fps), "-i", "-", "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p", path],
                   input=data.tobytes(), check=True)


def sprite(frame, px, tint, flip):
    import numpy as np
    from PIL import Image
    if flip:
        frame = frame[:, ::-1]
    out = np.stack([np.asarray(Image.fromarray(frame[..., c]).resize((px, px), Image.BILINEAR)) for c in range(4)], -1)
    out[..., :3] *= tint
    return out


def paste(image, s, cx, cy):
    h, w = s.shape[:2]
    x0, y0 = cx - w // 2, cy - h // 2
    sx0, sy0 = max(0, -x0), max(0, -y0)
    x1, y1 = min(image.shape[1], x0 + w), min(image.shape[0], y0 + h)
    if x1 <= max(x0, 0) or y1 <= max(y0, 0):
        return
    s = s[sy0:sy0 + y1 - max(y0, 0), sx0:sx0 + x1 - max(x0, 0)]
    region = image[max(y0, 0):y1, max(x0, 0):x1]
    region[:] = s[..., :3] + region * (1.0 - s[..., 3:])


def particle_field(frames, loop, seconds=6.0, fps=30, size=720, count=12, seed=11, scale=1.0):
    """count tinted particles, each a random start, rising, growing and fading over its life, in a
    stream that respawns them: roughly what a ParticleSystem with this flipbook draws.  scale 1 is
    close up; about 0.35 is the default camera's size for a building's smoke."""
    import numpy as np
    rng = np.random.default_rng(seed)
    background = battlefield(size)
    life = 3.0
    total = int(seconds * fps)
    spawns = int(count * (seconds + life) / life)
    births = np.linspace(-life, seconds, spawns) + rng.uniform(-0.1, 0.1, spawns)
    xs = rng.uniform(0.35, 0.65, births.size) * size
    tints = rng.choice([np.array([0.42, 0.40, 0.38]), np.array([0.32, 0.30, 0.29]), np.array([0.55, 0.52, 0.48])], births.size)
    starts = rng.integers(0, FRAMES, births.size)
    spins = rng.choice([0, 1], births.size)
    out = np.zeros((total, size, size, 3), np.float32)
    for t in range(total):
        now = t / fps
        image = background.copy()
        for k in np.argsort(births):
            age = now - births[k]
            if not 0.0 <= age < life:
                continue
            u = age / life
            index = int(starts[k] + age * 16.0) % FRAMES if loop else min(FRAMES - 1, int(u * FRAMES))
            px = max(4, int(size * scale * (0.18 + 0.27 * u)))
            s = sprite(frames[index], px, tints[k], spins[k]) * (min(1.0, u / 0.1) * (1.0 - u) ** 0.8)
            cx = int(xs[k] + 40.0 * scale * np.sin(u * 3.0 + k))
            cy = int(size * (0.85 - 0.6 * scale * u))
            paste(image, s, cx, cy)
        out[t] = image
    return out


def blasts(frames, size=720, seconds=4.0, fps=30, scale=1.0, seed=3):
    """Three explosions of the fire flipbook as the game's fireball systems throw them: each four
    cards of random size, turn and small delay played once over 1.6 s, over the ground."""
    import numpy as np
    from PIL import Image
    rng = np.random.default_rng(seed)
    background = battlefield(size)
    life = 1.6
    cards = []
    for i, (bx, at) in enumerate(((0.25, 0.2), (0.62, 0.9), (0.45, 1.9))):
        for _ in range(4):
            cards.append(dict(x=size * (bx + rng.uniform(-0.04, 0.04)), y=size * (0.62 + rng.uniform(-0.04, 0.03)),
                              born=at + rng.uniform(0.0, 0.18), px=size * scale * rng.uniform(0.18, 0.3),
                              turn=rng.uniform(-30, 30), flip=rng.integers(0, 2)))
    total = int(seconds * fps)
    out = np.zeros((total, size, size, 3), np.float32)
    for t in range(total):
        image = background.copy()
        for c in cards:
            u = (t / fps - c["born"]) / life
            if not 0.0 <= u < 1.0:
                continue
            frame = frames[min(FRAMES - 1, int(u * FRAMES))]
            rotated = np.stack([np.asarray(Image.fromarray(frame[..., k]).rotate(c["turn"], Image.BILINEAR)) for k in range(4)], -1)
            px = max(4, int(c["px"] * (0.8 + 0.4 * u)))
            paste(image, sprite(rotated, px, 1.0, c["flip"]), int(c["x"]), int(c["y"] - 0.08 * size * u))
        out[t] = image
    return np.clip(out, 0.0, 1.0)


def wreck_fire(frames, size=720, seconds=6.0, fps=30, scale=1.0, seed=7):
    """A looping flame flipbook as a burning wreck would carry it: a dark hulk on the ground and four
    cards of different size, tint, start frame and mirror standing on it, their feet on its deck."""
    import numpy as np
    rng = np.random.default_rng(seed)
    image = battlefield(size)
    y, x = np.mgrid[0:size, 0:size] / float(size)
    cx, cy = 0.5, 0.72
    hull = (np.abs(x - cx) < 0.2 * scale) & (np.abs(y - cy) < 0.055 * scale)
    turret = (np.abs(x - cx + 0.03 * scale) < 0.08 * scale) & (np.abs(y - cy + 0.08 * scale) < 0.035 * scale)
    image[hull | turret] = (0.05, 0.045, 0.04)
    cards = [dict(dx=dx * scale, dy=dy * scale, px=int(size * scale * s), tint=np.array(t, np.float32),
                  start=int(rng.integers(0, FRAMES)), flip=k % 2)
             for k, (dx, dy, s, t) in enumerate(((-0.1, -0.03, 0.42, (1.0, 0.95, 0.9)), (0.06, -0.09, 0.5, (1.0, 1.0, 1.0)),
                                                 (0.14, -0.02, 0.32, (0.95, 0.85, 0.8)), (-0.02, 0.0, 0.36, (0.9, 0.85, 0.85))))]
    out = np.zeros((int(seconds * fps), size, size, 3), np.float32)
    for t in range(len(out)):
        frame = image.copy()
        for c in cards:
            s = sprite(frames[(c["start"] + t) % FRAMES], c["px"], c["tint"], c["flip"])
            # the card's foot (0.9 of its height, the look's base) on the deck
            paste(frame, s, int(size * (cx + c["dx"])), int(size * (cy + c["dy"]) - c["px"] * 0.4))
        out[t] = frame
    return np.clip(out, 0.0, 1.0)


def gun_shots(frames, unit=10.0, size=11.0, life=5, pitch=37.5):
    """The jet as a ground-aligned card turned towards its emitter draws it, at four barrel headings
    (rows) over its life (columns): a quad 2 size a side lying flat at the muzzle, its top edge on the
    muzzle and the rest down the barrel's line, seen from a camera pitch degrees above the ground, so
    the run along the view is shortened.  unit is pixels a game unit; about 4 is the default camera."""
    import numpy as np
    from PIL import Image
    side = int(2 * size * unit)
    panel = int(side * 2.4)
    squash = math.sin(math.radians(pitch))
    rows = []
    for heading in (0.0, 40.0, 90.0, 155.0):
        row = []
        for step in range(life):
            image = battlefield(panel)
            frame = frames[min(len(frames) - 1, int(step / life * len(frames)))]
            card = np.stack([np.asarray(Image.fromarray(frame[..., k]).resize((side, side), Image.LANCZOS)) for k in range(4)], -1)
            # flat on the ground, root (the top edge) at the card's centre-top: turn it about that point
            pad = np.zeros((2 * side, 2 * side, 4), np.float32)
            pad[side:, side // 2:side // 2 + side] = card		# the root at the pad's centre
            turned = np.stack([np.asarray(Image.fromarray(pad[..., k]).rotate(heading, Image.BILINEAR)) for k in range(4)], -1)
            seen = np.stack([np.asarray(Image.fromarray(turned[..., k]).resize((2 * side, max(1, int(2 * side * squash))), Image.BILINEAR)) for k in range(4)], -1)
            cx, cy = panel // 2, panel // 2
            # the barrel: a dark bar from the hull's centre to the muzzle, against the jet's heading
            dx, dy = math.sin(math.radians(heading)), math.cos(math.radians(heading)) * squash
            for t in np.linspace(0.0, 1.0, 200):
                x, y = int(cx - dx * t * 0.6 * side), int(cy - dy * t * 0.6 * side)
                image[max(y - 2, 0):y + 3, max(x - 2, 0):x + 3] = 0.06
            paste(image, seen, cx, cy)
            row.append(image)
        rows.append(np.concatenate(row, axis=1))
    return np.clip(np.concatenate(rows, axis=0), 0.0, 1.0)


def seam(frames):
    """A loop's seam, the mean texel change from the last frame to the first, against the mean and
    the largest change between neighbours inside it."""
    import numpy as np
    steps = [np.abs(frames[(i + 1) % len(frames)] - frames[i]).mean() for i in range(len(frames))]
    return steps[-1], float(np.mean(steps[:-1])), float(np.max(steps[:-1]))


def previews(look, frames, p, folder):
    import numpy as np
    fire = p["kind"] == "fire"
    whole = sheet(frames)
    small = sheet(resize(frames, GAME_CELL))
    dark = battlefield(whole.shape[0])
    dark_small = battlefield(small.shape[0])
    if fire:
        save_png(os.path.join(folder, look + "-sheet-dark.png"), over(whole, dark))
        save_png(os.path.join(folder, look + "-sheet-game-scale.png"), over(small, dark_small))
        big = resize(frames, 512)
        encode(os.path.join(folder, look + "-loop.mp4"), over(big, battlefield(512)), 30)
        if p.get("root_up"):
            save_png(os.path.join(folder, look + "-shots.png"), gun_shots(frames))
            save_png(os.path.join(folder, look + "-shots-game-scale.png"), gun_shots(frames, unit=4.0))
            return
        if p["loop"]:
            clip = wreck_fire(frames)
            save_png(os.path.join(folder, look + "-wreck.png"), clip[len(clip) // 3])
            encode(os.path.join(folder, look + "-wreck.mp4"), clip, 30, loops=1)
            encode(os.path.join(folder, look + "-wreck-game-scale.mp4"), wreck_fire(frames, scale=0.35), 30, loops=1)
            return
        if p.get("nuke") or p.get("parts"):
            return		# the cap over the stem is judged together (nuke_composite); variants by their sheet
        encode(os.path.join(folder, look + "-blasts.mp4"), blasts(frames), 30, loops=2)
        encode(os.path.join(folder, look + "-blasts-game-scale.mp4"), blasts(frames, scale=0.35, seed=4), 30, loops=2)
        return
    tint = np.array([0.42, 0.40, 0.38], np.float32)		# the dark tint the INI gives a death's smoke
    tinted, tinted_small = whole.copy(), small.copy()
    tinted[..., :3] *= tint
    tinted_small[..., :3] *= tint
    save_png(os.path.join(folder, look + "-sheet-dark.png"), over(tinted, dark))
    save_png(os.path.join(folder, look + "-sheet-game-scale.png"), over(tinted_small, dark_small))
    big = resize(frames, 512)
    big[..., :3] *= tint
    encode(os.path.join(folder, look + "-loop.mp4"), over(big, battlefield(512)), 20)
    if p.get("parts"):
        return		# a particle field would cross from one variant's run into the next
    encode(os.path.join(folder, look + "-particles.mp4"), particle_field(frames, p["loop"]), 30, loops=2)
    encode(os.path.join(folder, look + "-particles-game-scale.mp4"), particle_field(frames, p["loop"], scale=0.35, count=8), 30, loops=2)


def nuke_composite(built, folder, size=360, seconds=4.0, fps=30):
    """The mushroom as the game would stack it, at about the default camera's scale: the stem card
    standing on the ground, the cap card on its top rising a little, two flame jets bursting at the
    foot if they were built, each played once over the clip.  An mp4 and a strip of four moments."""
    import numpy as np
    stem, cap, jet = built["nukestem"], built["nukecap"], built.get("nukejet")
    total = int(seconds * fps)
    ground = int(size * 0.88)
    clip = np.zeros((total, size, size, 3), np.float32)
    for t in range(total):
        u = t / total
        image = battlefield(size)
        s = sprite(stem[min(len(stem) - 1, int(u * len(stem)))], int(size * 0.62), 1.0, False)
        paste(image, s, size // 2, ground - int(s.shape[0] * (STEM_BASE - 0.5)))
        top = ground - int(s.shape[0] * (STEM_BASE - 0.05))		# the stem's top, the cap's seat
        if jet is not None and u < 0.4:
            for dx, flip in ((-0.06, False), (0.05, True)):
                j = sprite(jet[min(len(jet) - 1, int(u / 0.4 * len(jet)))], int(size * 0.4), 1.0, flip)
                paste(image, j, int(size * (0.5 + dx)), ground - int(j.shape[0] * 0.43))
        c = sprite(cap[min(len(cap) - 1, int(u * len(cap)))], int(size * (0.5 + 0.15 * u)), 1.0, False)
        paste(image, c, size // 2, top - int(size * 0.06 * u))
        clip[t] = image
    clip = np.clip(clip, 0.0, 1.0)
    encode(os.path.join(folder, "nuke-composite-game-scale.mp4"), clip, fps, loops=1)
    save_png(os.path.join(folder, "nuke-composite-game-scale.png"),
             np.concatenate([clip[int(k * (total - 1) / 3)] for k in range(4)], axis=1))


def pack(work, folder=None, only=()):
    import numpy as np
    built = {}
    for look, p in LOOKS.items():
        if (only and look not in only) or not p["name"]:
            continue		# a variant's run goes into its atlas, never alone
        runs = []
        for part in p.get("parts", (look,)):
            q = LOOKS[part]
            source = os.path.join(work, part, "frames")
            needed = frame_count(q) + (LOOP_BLEND if q["loop"] else 0)
            if not os.path.isdir(source) or len(os.listdir(source)) < needed:
                print("skipped %s: %s does not hold %d frames" % (look, source, needed))
                break
            runs.append(build(load_sequence(source, q["kind"] == "fire", q.get("key")), q))
        if len(runs) < len(p.get("parts", (look,))):
            continue
        frames = np.concatenate(runs)
        path = os.path.normpath(os.path.join(TEXTURES, p["name"]))
        with open(path, "wb") as out:
            out.write(dds(atlas(frames), frames.shape[1]))
        print(path, os.path.getsize(path))
        if p["loop"]:
            print("  seam %.4f, steps inside the loop: mean %.4f, largest %.4f" % seam(frames))
        if folder:
            os.makedirs(folder, exist_ok=True)
            previews(look, frames, p, folder)
        built[look] = frames
    if folder and "nukecap" in built and "nukestem" in built:
        nuke_composite(built, folder)


def selfcheck():
    import numpy as np
    # The loop: the frame after the last is the sim's next, and the head is the tail faded in.
    frames = np.arange(FRAMES + LOOP_BLEND, dtype=np.float32)[:, None, None, None] * np.ones((1, 1, 1, 4), np.float32)
    looped = crossfade_loop(frames)
    assert looped.shape[0] == FRAMES
    assert abs(looped[0, 0, 0, 0] - (FRAMES * (1 - 0.5 / LOOP_BLEND) + 0.0)) < 1e-3
    assert looped[LOOP_BLEND, 0, 0, 0] == LOOP_BLEND
    # The window: zero at the border, one in the middle.
    w = window(64)
    assert w[0, 0] < 1e-3 and w[0, 32] < 0.05 and w[32, 32] == 1.0
    # A solid atlas: mips run to 8-pixel frames plus two, every kept level's rings empty.
    levels = mips(np.ones((64, 64, 4), np.float32), 32)
    assert [level.shape[0] for level in levels] == [64, 32, 16, 8, 4]
    for level, cell in zip(levels[:-THROWAWAY_LEVELS], (32, 16, 8)):
        assert not level[::cell, :, 3].any() and not level[cell - 1::cell, :, 3].any()
        assert level[cell // 2, cell // 2, 3] > 0.0
    # Fire's tone curve: light where nothing is opaque still shows, covering what is behind it, and
    # a premultiplied colour never passes its alpha.
    hot = np.zeros((1, 2, 2, 4), np.float32)
    hot[0, 0, 0] = (8.0, 3.0, 0.6, 0.0)		# bright flame, no soot
    hot[0, 0, 1] = (0.0, 0.0, 0.0, 0.9)		# soot, no flame
    mapped = tone_map(hot)
    assert mapped[0, 0, 0, 3] > 0.9 and mapped[0, 0, 0, 0] > mapped[0, 0, 0, 2]
    assert abs(mapped[0, 0, 1, 3] - 0.9) < 1e-5 and mapped[0, 0, 1, :3].max() == 0.0
    assert (mapped[..., :3] <= mapped[..., 3:] + 1e-6).all()
    # The burn's exposure: a blinding ignition comes down to the key, a dying ember is not lit up.
    burn = np.zeros((16, 4, 4, 4), np.float32)
    burn[:8, ..., 0] = 100.0
    burn[8:, ..., 0] = 0.05
    exposed = expose_burn(burn)
    assert abs(exposed[0, 0, 0, 0] - FIRE_KEY[0][1]) < 0.3 and exposed[-1, 0, 0, 0] <= 0.1 + 1e-6
    # A steady fire: one factor for all frames, so the brighter frame stays brighter.
    steady = expose_steady(burn[:8] * np.linspace(1.0, 2.0, 8)[:, None, None, None], 1.0)
    assert steady[0, 0, 0, 0] < steady[-1, 0, 0, 0] and 0.5 < steady[4, 0, 0, 0] < 1.5
    # The seam: a loop whose last frame steps back to the first as gently as any other step.
    ring = np.sin(np.linspace(0, 2 * np.pi, FRAMES, endpoint=False))[:, None, None, None] * np.ones((1, 2, 2, 4))
    last, mean, largest = seam(ring)
    assert last <= largest + 1e-9
    last, mean, largest = seam(np.linspace(0.0, 1.0, FRAMES)[:, None, None, None] * np.ones((1, 2, 2, 4)))
    assert last > 10 * largest		# a ramp is no loop: its seam is the whole climb back
    # The blur keeps a flat field flat and an impulse's total; the grade paints a steady flame EA's
    # gold and leaves grey soot grey.
    flat = np.full((9, 9, 1), 0.5, np.float32)
    assert np.allclose(box_blur(flat, 2), 0.5)
    spike = np.zeros((21, 21, 1), np.float32)
    spike[10, 10] = 1.0
    assert abs(box_blur(spike, 2).sum() - 1.0) < 1e-4
    card = np.zeros((1, 16, 16, 4), np.float32)
    card[0, :, :8] = (0.9, 0.5, 0.1, 1.0)		# flame
    card[0, :, 8:] = (0.3, 0.3, 0.3, 1.0)		# soot
    graded = stylize(card)[0]
    r, g, b = graded[8, 2, :3]
    assert min(STYLE_MID[0], STYLE_HIGH[0]) - 0.1 < r < 0.9 and r > g > b and np.allclose(graded[8, 13, :3], 0.3, atol=0.01)
    # Variants: an emitter turned a quarter about the vertical, and mirrored; four whole runs an atlas.
    (x, y, z, r, a, b), = turned(((1.0, 0.0, 2.0, 0.5, 1, 4),), 90)
    assert abs(x) < 1e-9 and abs(y - 1.0) < 1e-9 and (z, r, a, b) == (2.0, 0.5, 1, 4)
    assert abs(turned(((0.0, 1.0, 0.0, 1.0, 1, 1),), 0, True)[0][1] + 1.0) < 1e-9
    for look, p in LOOKS.items():
        if p.get("parts"):
            assert sum(frame_count(LOOKS[part]) for part in p["parts"]) == FRAMES, look
            assert all(LOOKS[part]["first"] + (VARIANT_FRAMES - 1) * LOOKS[part]["step"] <= LOOKS[part]["frame_end"]
                       for part in p["parts"]), look
    # Holding density: a thinning puff's last frame comes back denser, its colour untouched.
    fading = np.zeros((FRAMES, 16, 16, 4), np.float32)
    fading[..., 3] = np.linspace(0.9, 0.1, FRAMES)[:, None, None]
    fading[..., :3] = fading[..., 3:] * 0.5
    held = hold_density(fading, 0.35)
    assert abs(held[0, 0, 0, 3] - 0.9) < 1e-4 and 0.15 < held[-1, 0, 0, 3] < 0.5
    assert abs(held[-1, 0, 0, 0] / held[-1, 0, 0, 3] - 0.5) < 1e-4
    tiny = dds(np.zeros((8, 8, 4), np.float32), 8)
    assert len(tiny) == 128 + 4 * 16 + 16 + 16 and tiny[:4] == b"DDS " and tiny[84:88] == b"DXT5"


def main():
    if "bpy" in sys.modules:
        args = sys.argv[sys.argv.index("--") + 1:]
        assert args[0] == "render", args
        blender_render(args[1], args[2])
        return
    selfcheck()
    args = sys.argv[1:]
    if args[:1] == ["pack"]:
        pack(args[1], args[2] if len(args) > 2 else None, tuple(args[3:]))
    elif args[:1] != ["selfcheck"]:
        print(__doc__)


if __name__ == "__main__":
    main()
