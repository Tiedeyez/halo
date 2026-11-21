# Halo's turntable, built from code in Blender (5.x) so the art can be
# rebuilt, changed and reviewed like the rest of the source.
#
#   blender -b -P turntable.py -- --out DIR [--preview] [--samples N] [--size PX]
#   blender -b -P turntable.py -- --out DIR --layers [--frames N]   (the Turntable visual's layers)
#   ... --layers --deck-only   (only deck.png and deck-lit.png, for a fix that leaves the arm alone)
#   blender -b -P turntable.py -- --out DIR --energy [--frames N]   (the Hologram visual's layers)
#
# An original design (not a copy of any real deck): a dark anodised plinth
# with chamfered edges, a machined platter with strobe dots, a straight
# tonearm with an angled headshell, and Halo's own touch, a light ring round
# the platter. Units are metres, at real size (a 12" record is 0.3048 m).

import math
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


OUT = arg("--out", "/tmp/halo-turntable")
PREVIEW = "--preview" in argv
SAMPLES = int(arg("--samples", "96"))
SIZE = int(arg("--size", "1024"))
VIEW = arg("--view", "top")                      # "top" (face up) or "three-quarter"
ARM_PROGRESS = float(arg("--progress", "0.35"))   # 0 = lead-in, 1 = run-out; -1 = at rest

# ---------------------------------------------------------------- layout --
PLINTH = (0.46, 0.36, 0.055)           # width, depth, height
PLATTER_C = Vector((-0.045, 0.0, 0.0))  # platter centre (x, y) on the plinth
PLATTER_R = 0.162
PLATTER_H = 0.021
RECORD_R = 0.1524
RECORD_T = 0.0022
LABEL_R = 0.0505
PIVOT = Vector((0.165, 0.105, 0.0))    # tonearm pivot (x, y)
ARM_L = 0.232                           # pivot to stylus
TOP = PLINTH[2]                         # top of the plinth
DECK = TOP + PLATTER_H + 0.003          # top of the slipmat
GROOVE_OUT, GROOVE_IN = 0.146, 0.062    # where the stylus plays, lead-in to run-out


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def smooth(obj, angle=35):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(angle))
    except Exception:
        try:
            bpy.ops.object.shade_smooth()
        except Exception:
            pass
    obj.select_set(False)


def bevel(obj, width, segments=3, angle=40):
    m = obj.modifiers.new("Bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle)
    m.harden_normals = True
    return m


def cyl(name, r, depth, loc, verts=128, mat=None, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=depth, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    if mat:
        o.data.materials.append(mat)
    return o


def box(name, size, loc, mat=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    # Scale only: the default also bakes location and rotation, which puts
    # the origin at the world centre so a later rotation swings the part away.
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if mat:
        o.data.materials.append(mat)
    return o


# ------------------------------------------------------------- materials --
def principled(name, base, metal=0.0, rough=0.4, aniso=0.0, coat=0.0, emit=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*base, 1)
    p.inputs["Metallic"].default_value = metal
    p.inputs["Roughness"].default_value = rough
    if aniso:
        p.inputs["Anisotropic"].default_value = aniso
    if coat:
        p.inputs["Coat Weight"].default_value = coat
        p.inputs["Coat Roughness"].default_value = 0.05
    if emit:
        p.inputs["Emission Color"].default_value = (*emit, 1)
        p.inputs["Emission Strength"].default_value = strength
    return m


def radial_tangent(m, axis="Z"):
    """Anisotropy along circles round the object's Z: the look of machined
    rings and of a record's grooves."""
    nt = m.node_tree
    t = nt.nodes.new("ShaderNodeTangent")
    t.direction_type = "RADIAL"
    t.axis = axis
    nt.links.new(t.outputs["Tangent"], nt.nodes["Principled BSDF"].inputs["Tangent"])


def groove_bump(m, scale, strength):
    """Fine concentric rings as a bump: a record's grooves, a platter's
    machining marks."""
    nt = m.node_tree
    tc = nt.nodes.new("ShaderNodeTexCoord")
    w = nt.nodes.new("ShaderNodeTexWave")
    w.wave_type = "RINGS"
    # Round the object's Z, so the rings are concentric on the flat top. The
    # default (X) draws straight bands mirrored about the centre line, which
    # shows as a crease through the record seen from above.
    w.rings_direction = "Z"
    w.inputs["Scale"].default_value = scale
    w.inputs["Distortion"].default_value = 0.4
    w.inputs["Detail"].default_value = 2
    b = nt.nodes.new("ShaderNodeBump")
    b.inputs["Strength"].default_value = strength
    b.inputs["Distance"].default_value = 0.0002
    nt.links.new(tc.outputs["Object"], w.inputs["Vector"])
    nt.links.new(w.outputs["Fac"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], nt.nodes["Principled BSDF"].inputs["Normal"])


def build_materials():
    M = {}
    M["plinth"] = principled("Plinth", (0.035, 0.038, 0.045), metal=1.0, rough=0.38)
    M["top"] = principled("Top plate", (0.05, 0.053, 0.06), metal=1.0, rough=0.28, aniso=0.6)
    radial_tangent(M["top"])
    M["platter"] = principled("Platter", (0.62, 0.63, 0.66), metal=1.0, rough=0.18, aniso=0.85)
    radial_tangent(M["platter"])
    groove_bump(M["platter"], 900, 0.08)
    M["mat"] = principled("Slipmat", (0.012, 0.012, 0.014), rough=0.85)
    M["vinyl"] = principled("Vinyl", (0.008, 0.008, 0.009), rough=0.22, aniso=0.92, coat=0.6)
    radial_tangent(M["vinyl"])
    groove_bump(M["vinyl"], 1800, 0.18)
    M["label"] = principled("Label", (0.85, 0.85, 0.85), rough=0.55)
    M["chrome"] = principled("Chrome", (0.9, 0.9, 0.92), metal=1.0, rough=0.07)
    M["satin"] = principled("Satin", (0.7, 0.71, 0.74), metal=1.0, rough=0.3)
    M["black"] = principled("Black", (0.015, 0.015, 0.017), metal=0.0, rough=0.45, coat=0.3)
    M["rubber"] = principled("Rubber", (0.02, 0.02, 0.02), rough=0.9)
    # The halo ring and the lamps are white emitters: Halo tints them with
    # the theme's accent when it composes the layers.
    M["halo"] = principled("Halo ring", (0.02, 0.02, 0.02), rough=0.3, emit=(1, 1, 1), strength=6.0)
    M["led"] = principled("LED", (0.02, 0.02, 0.02), rough=0.3, emit=(1, 1, 1), strength=12.0)
    M["strobe"] = principled("Strobe dot", (0.75, 0.76, 0.8), metal=1.0, rough=0.12)
    return M


# ----------------------------------------------------------------- model --
def build_plinth(M):
    w, d, h = PLINTH
    body = box("Plinth", (w, d, h - 0.004), (0, 0, (h - 0.004) / 2), M["plinth"])
    bevel(body, 0.006, 4)
    top = box("Top plate", (w - 0.006, d - 0.006, 0.004), (0, 0, h - 0.002), M["top"])
    bevel(top, 0.0015, 2)
    smooth(body)
    smooth(top)
    # Feet: wide, damped, with a chrome ring.
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (w / 2 - 0.045), sy * (d / 2 - 0.045)
            f = cyl("Foot", 0.03, 0.016, (x, y, -0.006), 64, M["rubber"])
            bevel(f, 0.004, 3)
            r = cyl("Foot ring", 0.031, 0.004, (x, y, 0.001), 64, M["chrome"])
            bevel(r, 0.0012, 2)
            smooth(f)
            smooth(r)


def build_platter(M):
    cx, cy = PLATTER_C.x, PLATTER_C.y
    # The halo: a light ring set into the plate round the platter.
    halo = cyl("Halo ring", PLATTER_R + 0.012, 0.0016, (cx, cy, TOP + 0.0002), 192, M["halo"])
    cut = cyl("Halo cut", PLATTER_R + 0.007, 0.01, (cx, cy, TOP), 192)
    mod = halo.modifiers.new("Inner", "BOOLEAN")
    mod.object = cut
    mod.operation = "DIFFERENCE"
    cut.hide_render = True
    cut.hide_viewport = True
    # Platter: machined, with a bevelled lip and a row of strobe dots.
    p = cyl("Platter", PLATTER_R, PLATTER_H, (cx, cy, TOP + PLATTER_H / 2 + 0.001), 256, M["platter"])
    bevel(p, 0.0025, 4)
    smooth(p)
    dots = 90
    for row, (z, r) in enumerate(((TOP + 0.007, 0.0016), (TOP + 0.014, 0.0016))):
        for i in range(dots):
            a = 2 * math.pi * (i + 0.5 * row) / dots
            x = cx + (PLATTER_R + 0.0004) * math.cos(a)
            y = cy + (PLATTER_R + 0.0004) * math.sin(a)
            bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=6, radius=r, location=(x, y, z))
            s = bpy.context.active_object
            s.name = "Strobe dot"
            s.scale = (0.6, 0.6, 1.0)
            s.data.materials.append(M["strobe"])
    # Slipmat, record and label.
    cyl("Slipmat", PLATTER_R - 0.004, 0.003, (cx, cy, TOP + PLATTER_H + 0.0025), 256, M["mat"])
    rec = cyl("Record", RECORD_R, RECORD_T, (cx, cy, DECK + RECORD_T / 2), 256, M["vinyl"])
    bevel(rec, 0.0008, 2)
    smooth(rec)
    cyl("Label", LABEL_R, 0.0003, (cx, cy, DECK + RECORD_T + 0.00015), 256, M["label"])
    # Spindle, with a chrome dome.
    sp = cyl("Spindle", 0.0036, 0.02, (cx, cy, DECK + 0.008), 48, M["chrome"])
    bevel(sp, 0.0012, 3)
    smooth(sp)


def stylus_angle(radius):
    """The arm's heading (radians, from +x) that puts the stylus `radius`
    from the platter's centre, on the inside of the swing."""
    c = PLATTER_C.to_2d()
    p = PIVOT.to_2d()
    d = (c - p).length
    # Triangle pivot-centre-stylus: angle at the pivot between the line to
    # the centre and the arm.
    cosA = (ARM_L ** 2 + d ** 2 - radius ** 2) / (2 * ARM_L * d)
    a = math.acos(max(-1.0, min(1.0, cosA)))
    base = math.atan2(c.y - p.y, c.x - p.x)
    return base + a


def tube_between(name, a, b, r, mat, verts=48):
    """A cylinder from point a to point b (local coordinates)."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d.length, location=(a + b) / 2)
    o = bpy.context.active_object
    o.name = name
    o.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    o.data.materials.append(mat)
    return o


def build_tonearm(M, heading):
    """The tonearm. Fixed parts sit on the plate; everything that swings is
    built round the origin pointing along +x, parented to an empty at the
    pivot, and the empty is turned to `heading`."""
    px, py = PIVOT.x, PIVOT.y
    fixed = []
    # Bearing tower: a stepped satin base, a black collar with a height
    # (VTA) ring, and a lit index line on the collar.
    base = cyl("Arm base", 0.032, 0.005, (px, py, TOP + 0.0025), 128, M["satin"])
    bevel(base, 0.0015, 3)
    step = cyl("Arm base step", 0.025, 0.004, (px, py, TOP + 0.007), 128, M["black"])
    bevel(step, 0.001, 2)
    collar = cyl("Arm collar", 0.0195, 0.02, (px, py, TOP + 0.019), 128, M["black"])
    bevel(collar, 0.0018, 3)
    vta = cyl("VTA ring", 0.0205, 0.0035, (px, py, TOP + 0.012), 128, M["satin"])
    bevel(vta, 0.0008, 2)
    for k in range(48):   # knurling on the VTA ring
        a = 2 * math.pi * k / 48
        n = box("VTA knurl", (0.0012, 0.0006, 0.0033), (px + 0.0207 * math.cos(a), py + 0.0207 * math.sin(a), TOP + 0.012), M["satin"])
        n.rotation_euler = (0, 0, a)
    fixed += [base, step, collar, vta]

    # Arm rest: a post with a rubber-lined cradle and a satin clip, where
    # the arm parks (heading -90 degrees).
    rest_d = 0.175
    rx, ry = px, py - rest_d
    post = cyl("Rest post", 0.0032, 0.03, (rx, ry, TOP + 0.015), 48, M["satin"])
    pbase = cyl("Rest base", 0.008, 0.003, (rx, ry, TOP + 0.0015), 64, M["satin"])
    bevel(pbase, 0.001, 2)
    cradle = box("Rest cradle", (0.014, 0.008, 0.005), (rx, ry, TOP + 0.032), M["rubber"])
    bevel(cradle, 0.0018, 3)
    clip = tube_between("Rest clip", (rx - 0.006, ry - 0.004, TOP + 0.034), (rx + 0.006, ry - 0.004, TOP + 0.04), 0.0009, M["chrome"])
    fixed += [post, pbase, cradle]

    # Cue (lift) lever and platform, beside the tower.
    cue_base = box("Cue base", (0.012, 0.03, 0.006), (px + 0.045, py - 0.03, TOP + 0.003), M["black"])
    bevel(cue_base, 0.002, 3)
    cue = tube_between("Cue lever", (px + 0.045, py - 0.04, TOP + 0.006), (px + 0.045, py - 0.058, TOP + 0.016), 0.0022, M["satin"])
    cue_knob = cyl("Cue knob", 0.004, 0.006, (px + 0.045, py - 0.058, TOP + 0.017), 48, M["black"])
    cue_post = cyl("Cue post", 0.0028, 0.022, (px + 0.024, py - 0.06, TOP + 0.011), 32, M["satin"])
    cue_pad = box("Cue pad", (0.018, 0.005, 0.002), (px + 0.024, py - 0.06, TOP + 0.022), M["rubber"])
    fixed += [cue_base, cue_knob, cue_post]

    # Anti-skate: a dial behind the tower with a lit index and tick marks.
    dial = cyl("Anti-skate", 0.012, 0.008, (px - 0.042, py + 0.05, TOP + 0.004), 96, M["satin"])
    bevel(dial, 0.0016, 3)
    cap = cyl("Anti-skate cap", 0.0085, 0.002, (px - 0.042, py + 0.05, TOP + 0.0085), 96, M["black"])
    for k in range(13):
        a = math.radians(-120 + 20 * k)
        box("Anti-skate tick", (0.0045 if k % 3 == 0 else 0.0028, 0.0006, 0.0003),
            (px - 0.042 + 0.0165 * math.cos(a), py + 0.05 + 0.0165 * math.sin(a), TOP + 0.00015), M["satin"]).rotation_euler = (0, 0, a)
    fixed += [dial, cap]
    for o in fixed:
        smooth(o)

    # ---- the moving arm, built at the origin along +x ----
    bpy.ops.object.empty_add(location=(0, 0, 0))
    swing = bpy.context.active_object
    swing.name = "Arm swing"
    parts = []
    z = 0.042   # arm height above the plate
    # Gimbal: a chrome cup and a satin yoke with two visible bearing screws.
    parts.append(cyl("Gimbal", 0.012, 0.012, (0, 0, 0.034), 96, M["chrome"]))
    yoke = box("Yoke", (0.026, 0.02, 0.011), (0, 0, z), M["satin"])
    bevel(yoke, 0.0035, 4)
    parts.append(yoke)
    for sy in (-1, 1):
        parts.append(cyl("Bearing screw", 0.0028, 0.002, (0, sy * 0.0105, z), 32, M["chrome"], rot=(math.radians(90), 0, 0)))
    # Arm tube: chrome, with a satin collar where it leaves the yoke and a
    # slight taper toward the headshell (two sections).
    reach = ARM_L - 0.032
    parts.append(tube_between("Arm tube", (0.012, 0, z), (reach * 0.55, 0, z), 0.0048, M["chrome"]))
    parts.append(tube_between("Arm tube tip", (reach * 0.55, 0, z), (reach, 0, z - 0.002), 0.0041, M["chrome"]))
    parts.append(tube_between("Arm collar ring", (0.012, 0, z), (0.022, 0, z), 0.0058, M["satin"]))
    # Rear stub, tracking-force ring with ticks, and a counterweight with
    # knurled black bands and a lit index line.
    parts.append(tube_between("Stub", (-0.012, 0, z), (-0.078, 0, z), 0.0046, M["satin"]))
    force = tube_between("Force ring", (-0.03, 0, z), (-0.036, 0, z), 0.0145, M["black"], 96)
    bevel(force, 0.0012, 2)
    parts.append(force)
    for k in range(16):
        a = 2 * math.pi * k / 16
        parts.append(box("Force tick", (0.0045, 0.0006, 0.0006),
                         (-0.033, 0.0147 * math.cos(a), z + 0.0147 * math.sin(a)), M["satin"]))
        parts[-1].rotation_euler = (a, 0, 0)
    cw = tube_between("Counterweight", (-0.04, 0, z), (-0.072, 0, z), 0.018, M["satin"], 128)
    bevel(cw, 0.0035, 5)
    parts.append(cw)
    for k in range(4):
        x = -0.046 - k * 0.006
        parts.append(tube_between("Knurl band", (x, 0, z), (x - 0.0022, 0, z), 0.0184, M["black"], 128))
    parts.append(box("Weight index", (0.03, 0.0008, 0.0014), (-0.056, 0, z + 0.0184), M["led"]))
    # Headshell: angled 22 degrees inward; a tapered black shell with a
    # satin finger lift curling up, four coloured tag wires to a satin
    # cartridge with a lit line, and the stylus.
    off = math.radians(22)
    hx = ARM_L - 0.021
    head = []
    connector = tube_between("Headshell collar", (reach - 0.003, 0, z - 0.002), (reach + 0.006, 0, z - 0.002), 0.0055, M["satin"])
    bevel(connector, 0.0008, 2)
    parts.append(connector)
    shell = box("Headshell", (0.036, 0.018, 0.0032), (hx, 0.002, z - 0.005), M["black"])
    bevel(shell, 0.0015, 3)
    head.append(shell)
    lift_a = tube_between("Finger lift", (hx + 0.006, 0.011, z - 0.004), (hx + 0.009, 0.026, z + 0.002), 0.0013, M["satin"])
    lift_b = tube_between("Finger lift tip", (hx + 0.009, 0.026, z + 0.002), (hx + 0.009, 0.031, z + 0.006), 0.0013, M["satin"])
    head += [lift_a, lift_b]
    cart = box("Cartridge", (0.02, 0.012, 0.012), (hx + 0.006, 0.0, z - 0.0125), M["satin"])
    bevel(cart, 0.0016, 3)
    body = box("Cartridge body", (0.016, 0.0125, 0.004), (hx + 0.008, 0.0, z - 0.0185), M["black"])
    bevel(body, 0.001, 2)
    head += [cart, body]
    head.append(box("Cartridge mark", (0.013, 0.0006, 0.0018), (hx + 0.006, -0.0063, z - 0.0125), M["led"]))
    for k, col in enumerate(((0.8, 0.1, 0.1), (0.1, 0.6, 0.15), (0.12, 0.25, 0.85), (0.85, 0.85, 0.85))):
        wm = bpy.data.materials.get("Wire %d" % k) or principled("Wire %d" % k, col, rough=0.35)
        y = -0.0045 + k * 0.003
        head.append(tube_between("Tag wire", (hx - 0.012, y, z - 0.0035), (hx - 0.003, y, z - 0.0085), 0.0006, wm, 12))
    for sx in (-1, 1):
        head.append(cyl("Headshell screw", 0.0016, 0.0012, (hx + 0.006 + sx * 0.006, 0.0, z - 0.0032), 24, M["chrome"]))
    head.append(tube_between("Cantilever", (hx + 0.012, 0, z - 0.0205), (ARM_L, 0, z - 0.0215), 0.0004, M["chrome"], 12))
    head.append(cyl("Stylus", 0.0005, 0.002, (ARM_L, 0, z - 0.0225), 12, M["chrome"]))
    # The headshell group turns about the stylus, so the stylus stays on
    # the arm's line (where the geometry puts it on the groove).
    for o in head:
        o.location = Vector(o.location) - Vector((ARM_L, 0, 0))
        o.location.rotate(__import__("mathutils").Euler((0, 0, off)))
        o.location += Vector((ARM_L, 0, 0))
        o.rotation_euler.z += off
    parts += head
    for o in parts:
        smooth(o)
        o.parent = swing
    swing.location = (px, py, TOP)
    swing.rotation_euler = (0, 0, heading)
    return swing


def build_controls(M):
    w, d, _ = PLINTH
    # Start/stop: a wide, low button front left, with 33 / 45 beside it and
    # a lit pip above the active speed.
    ss = box("Start", (0.05, 0.03, 0.006), (-w / 2 + 0.052, -d / 2 + 0.034, TOP + 0.003), M["satin"])
    bevel(ss, 0.0028, 4)
    smooth(ss)
    box("Start inlay", (0.03, 0.0012, 0.0004), (-w / 2 + 0.052, -d / 2 + 0.034, TOP + 0.0062), M["black"])
    for i, x in enumerate((-w / 2 + 0.098, -w / 2 + 0.124)):
        b = cyl("Speed", 0.0085, 0.005, (x, -d / 2 + 0.034, TOP + 0.0025), 96, M["satin"])
        bevel(b, 0.0016, 3)
        smooth(b)
        cyl("Speed pip", 0.0014, 0.0008, (x, -d / 2 + 0.048, TOP + 0.0004), 24, M["led" if i == 0 else "black"])
    # Pitch fader: a long slot with a centre detent mark and a ribbed cap,
    # front right, clear of the arm rest.
    fx, fy = w / 2 - 0.022, -d / 2 + 0.085
    box("Pitch slot", (0.005, 0.1, 0.0016), (fx, fy, TOP + 0.0002), M["black"])
    for k in range(-4, 5):
        box("Pitch tick", (0.006 if k == 0 else 0.0035, 0.0006, 0.0003), (fx - 0.008, fy + k * 0.011, TOP + 0.00015),
            M["led" if k == 0 else "satin"])
    cap = box("Pitch cap", (0.02, 0.011, 0.009), (fx, fy - 0.006, TOP + 0.0055), M["satin"])
    bevel(cap, 0.0028, 4)
    smooth(cap)
    for k in range(5):
        box("Pitch rib", (0.016, 0.0007, 0.0005), (fx, fy - 0.0095 + k * 0.0018, TOP + 0.0102), M["black"])
    # Screws at the plate's corners and the HALO wordmark, engraved-look.
    for sx in (-1, 1):
        for sy in (-1, 1):
            s = cyl("Plate screw", 0.0028, 0.001, (sx * (w / 2 - 0.012), sy * (d / 2 - 0.012), TOP + 0.0004), 32, M["satin"])
    bpy.ops.object.text_add(location=(0.05, d / 2 - 0.026, TOP + 0.0002))   # back edge, between record and arm
    t = bpy.context.active_object
    t.name = "Wordmark"
    t.data.body = "HALO"
    t.data.size = 0.012
    t.data.extrude = 0.0002
    t.data.space_character = 1.6
    t.data.materials.append(M["satin"])


# ------------------------------------------------------- camera and light --
def build_stage():
    scene = bpy.context.scene
    # Straight down on the deck, face up, as if standing over it: the record
    # is a true circle and the back of the deck is at the top of the image.
    # A normal lens from a metre up keeps a little of the arm's and knobs'
    # height. `--view three-quarter` gives the old high front view.
    bpy.ops.object.camera_add()
    cam = bpy.context.active_object
    if VIEW == "top":
        target = Vector((0.0, 0.0, TOP))
        dist = 0.92
        elev = math.radians(90)
        azim = math.radians(-90)
    else:
        target = Vector((0.005, 0.012, TOP * 0.6))
        dist = 1.05
        elev = math.radians(42)
        azim = math.radians(-90 + 12)
    cam.location = target + Vector((dist * math.cos(elev) * math.cos(azim),
                                    dist * math.cos(elev) * math.sin(azim),
                                    dist * math.sin(elev)))
    direction = target - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    if VIEW == "top":
        # Looking straight down the track is ambiguous; no roll: +y (the
        # back of the deck) is up in the image.
        cam.location = target + Vector((0, 0, dist))
        cam.rotation_euler = (0, 0, 0)
    cam.data.lens = 56
    cam.data.dof.use_dof = False
    scene.camera = cam

    def area(name, loc, size, power, colour=(1, 1, 1)):
        bpy.ops.object.light_add(type="AREA", location=loc)
        l = bpy.context.active_object
        l.name = name
        l.data.shape = "RECTANGLE"
        l.data.size, l.data.size_y = size
        l.data.energy = power
        l.data.color = colour
        d = target - l.location
        l.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        return l

    # Key: a big soft box above and behind-left, so the record's grooves
    # catch a long anisotropic streak. Rim: a strip behind right.
    # Out of the record's mirror angle, so the vinyl stays black and only
    # its grooves catch the light, as a streak through the centre.
    area("Key", (-0.95, -0.15, 0.62), (0.7, 0.45), 28)
    area("Fill", (0.8, -0.7, 0.4), (0.6, 0.6), 5, (0.85, 0.9, 1.0))
    area("Rim", (0.45, 0.8, 0.32), (1.2, 0.06), 22)
    area("Top strip", (-0.1, 0.55, 0.9), (1.2, 0.05), 9)

    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.004, 0.005, 0.007, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
    scene.world = world


def setup_render():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    for kind in ("OPTIX", "CUDA"):
        try:
            prefs.compute_device_type = kind
            prefs.get_devices()
            if any(d.type == kind for d in prefs.devices):
                break
        except Exception:
            continue
    for d in prefs.devices:
        d.use = True
    scene.cycles.device = "GPU"
    scene.cycles.samples = 32 if PREVIEW else SAMPLES
    scene.cycles.use_denoising = True
    scene.render.resolution_x = SIZE
    scene.render.resolution_y = SIZE
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"


def build_scene():
    clear()
    M = build_materials()
    build_plinth(M)
    build_platter(M)
    if ARM_PROGRESS < 0:
        heading = math.radians(-90)
    else:
        r = GROOVE_OUT + (GROOVE_IN - GROOVE_OUT) * ARM_PROGRESS
        heading = stylus_angle(r)
    swing = build_tonearm(M, heading)
    build_controls(M)
    build_stage()
    setup_render()
    return M, swing


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


REST = math.radians(-90)


def label_mapping():
    """The homography from the label's plane (u, v in label radii, +v away
    from the viewer) to the image (0..1, y down), and its inverse, so a
    shader can find, for each pixel, where on the label it lands."""
    import numpy as np
    from bpy_extras.object_utils import world_to_camera_view
    scene = bpy.context.scene
    cam = scene.camera
    z = DECK + RECORD_T + 0.0003
    rows = []
    for k in range(16):
        a = 2 * math.pi * k / 16
        for rr in (0.5, 1.0):
            u, v = rr * math.cos(a), rr * math.sin(a)
            co = Vector((PLATTER_C.x + LABEL_R * u, PLATTER_C.y + LABEL_R * v, z))
            q = world_to_camera_view(scene, cam, co)
            x, y = q.x, 1.0 - q.y
            rows.append([u, v, 1, 0, 0, 0, -x * u, -x * v, -x])
            rows.append([0, 0, 0, u, v, 1, -y * u, -y * v, -y])
    _, _, vt = np.linalg.svd(np.array(rows))
    H = vt[-1].reshape(3, 3)
    H /= H[2, 2]
    Hi = np.linalg.inv(H)
    Hi /= Hi[2, 2]
    return H.tolist(), Hi.tolist()


def layers():
    """Halo's turntable visual, as layers the app composes: the deck (lamps
    off, a white label the cover is printed onto), the lamps' light alone,
    the spindle and the tonearm at each angle of its swing, with their
    shadows. Frames go to OUT; tools/turntable-layers crops and packs them."""
    import json
    M, swing = build_scene()
    scene = bpy.context.scene
    arm = [swing] + [o for o in bpy.data.objects if o.parent == swing]
    spindle = [o for o in bpy.data.objects if o.name.startswith("Spindle")]
    moving = set(arm) | set(spindle)
    deck = [o for o in bpy.data.objects if o.type in ("MESH", "FONT") and o not in moving]
    lab = bpy.data.objects["Label"]
    white = principled("Label white", (0.8, 0.8, 0.8), rough=0.5)
    lab.data.materials.clear()
    lab.data.materials.append(white)

    def show(objs, on):
        for o in objs:
            o.hide_render = not on

    def lamps(strength):
        for name, full in (("Halo ring", 6.0), ("LED", 12.0)):
            bpy.data.materials[name].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = full * strength

    # Deck, lamps off; then lamps on (the app subtracts to get the light).
    show(arm + spindle, False)
    lamps(0)
    render(OUT + "/deck.png")
    lamps(1)
    render(OUT + "/deck-lit.png")
    if "--deck-only" in argv:
        return
    # Moving parts over a deck that only catches their shadows.
    for o in deck:
        o.is_shadow_catcher = True
    show(spindle, True)
    render(OUT + "/spindle.png")
    show(spindle, False)
    show(arm, True)
    lead_in, run_out = stylus_angle(GROOVE_OUT), stylus_angle(GROOVE_IN)
    count = int(arg("--frames", "36"))
    headings = [REST + (run_out - REST) * k / (count - 1) for k in range(count)]
    for k, h in enumerate(headings):
        swing.rotation_euler = (0, 0, h)
        render(OUT + "/arm-%02d.png" % k)
    H, Hi = label_mapping()
    manifest = {
        "size": SIZE,
        "headings": headings,
        "rest": REST, "leadIn": lead_in, "runOut": run_out,
        "pivot": [PIVOT.x, PIVOT.y], "centre": [PLATTER_C.x, PLATTER_C.y],
        "armLength": ARM_L, "grooveOut": GROOVE_OUT, "grooveIn": GROOVE_IN,
        "label": {"toImage": H, "fromImage": Hi},
    }
    with open(OUT + "/manifest.json", "w") as f:
        json.dump(manifest, f, indent=1)


def energy():
    """The Hologram visual's layers: the same deck made of light. Every edge
    the camera sees is a line of light (Freestyle), edges hidden behind are
    fainter (an x-ray of the mechanism), and surfaces glow where they turn
    away from the viewer (a fresnel rim), like a hologram's skin. Rendered
    white on black; the app tints, animates and adds them. Passes:
      lines (visible + hidden edges) and body (rim glow), for the deck, the
      spindle and each tonearm frame. Frames go to OUT, named energy-*."""
    import json
    _, swing = build_scene()
    scene = bpy.context.scene
    vl = scene.view_layers[0]
    arm = [swing] + [o for o in bpy.data.objects if o.parent == swing]
    spindle = [o for o in bpy.data.objects if o.name.startswith("Spindle")]
    moving = set(arm) | set(spindle)
    deck = [o for o in bpy.data.objects if o.type in ("MESH", "FONT") and o not in moving]
    # The record's own edge lines would be too many rings; the app draws
    # its grooves as energy. Keep the record's rim and the label's edge.
    for o in bpy.data.objects:
        if o.type == "LIGHT":
            o.hide_render = True
    scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0
    scene.cycles.samples = 24
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"

    holdout = bpy.data.materials.new("Holdout")
    holdout.use_nodes = True
    nt = holdout.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(nt.nodes.new("ShaderNodeHoldout").outputs[0], out.inputs["Surface"])

    rim = bpy.data.materials.new("Rim")
    rim.use_nodes = True
    nt = rim.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    lw = nt.nodes.new("ShaderNodeLayerWeight")
    lw.inputs["Blend"].default_value = 0.5
    pw = nt.nodes.new("ShaderNodeMath")
    pw.operation = "POWER"
    pw.inputs[1].default_value = 2.2
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = "MULTIPLY_ADD"
    add.inputs[1].default_value = 1.0
    add.inputs[2].default_value = 0.035   # a faint skin, even facing the viewer
    em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(lw.outputs["Facing"], pw.inputs[0])
    nt.links.new(pw.outputs[0], add.inputs[0])
    nt.links.new(add.outputs[0], em.inputs["Strength"])
    nt.links.new(em.outputs[0], out.inputs["Surface"])

    fs = vl.freestyle_settings
    fs.crease_angle = math.radians(140)
    for ls in list(fs.linesets):
        fs.linesets.remove(ls)
    def lineset(name, visibility, thickness, alpha):
        ls = fs.linesets.new(name)
        ls.select_by_visibility = True
        ls.visibility = visibility
        ls.select_by_edge_types = True
        ls.select_silhouette = True
        ls.select_border = True
        ls.select_crease = True
        ls.linestyle.color = (1, 1, 1)
        ls.linestyle.alpha = alpha
        ls.linestyle.thickness = thickness * SIZE / 1536
        return ls
    lineset("Seen", "VISIBLE", 1.25, 1.0)
    lineset("Hidden", "HIDDEN", 0.8, 0.22)

    def show(objs, on):
        for o in objs:
            o.hide_render = not on

    def passes(name):
        scene.render.use_freestyle = True
        vl.material_override = holdout
        render(OUT + "/energy-%s-lines.png" % name)
        scene.render.use_freestyle = False
        vl.material_override = rim
        render(OUT + "/energy-%s-body.png" % name)

    show(arm + spindle, False)
    passes("deck")
    show(deck, False)
    show(spindle, True)
    passes("spindle")
    show(spindle, False)
    show(arm, True)
    lead_in, run_out = stylus_angle(GROOVE_OUT), stylus_angle(GROOVE_IN)
    count = int(arg("--frames", "36"))
    headings = [REST + (run_out - REST) * k / (count - 1) for k in range(count)]
    for k, h in enumerate(headings):
        swing.rotation_euler = (0, 0, h)
        passes("arm-%02d" % k)
    show(deck, True)
    H, Hi = label_mapping()
    manifest = {
        "size": SIZE, "headings": headings, "rest": REST, "leadIn": lead_in, "runOut": run_out,
        "pivot": [PIVOT.x, PIVOT.y], "centre": [PLATTER_C.x, PLATTER_C.y],
        "armLength": ARM_L, "grooveOut": GROOVE_OUT, "grooveIn": GROOVE_IN,
        "recordRadius": RECORD_R / LABEL_R, "grooveOutLabel": GROOVE_OUT / LABEL_R,
        "grooveInLabel": GROOVE_IN / LABEL_R,
        "label": {"toImage": H, "fromImage": Hi},
    }
    with open(OUT + "/manifest.json", "w") as f:
        json.dump(manifest, f, indent=1)


def main():
    if "--energy" in argv:
        energy()
        return
    if "--layers" in argv:
        layers()
        return
    build_scene()
    render(OUT + "/beauty.png")
    bpy.ops.wm.save_as_mainfile(filepath=OUT + "/turntable.blend")


main()
