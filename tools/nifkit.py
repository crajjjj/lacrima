# NIF and DDS writing helpers for the Lacrima tear meshes, adapted from OStrands'
# tools/make_strand.py by LeonVanWinkle (Saint), GPL-3.0. They write the binaries
# directly and need no external tools.

import math
import os
import random
import struct

TEXTURE_DIR = 'textures\\Lacrima\\'

VERTEX_DESC = 0x0005B0007065040A
VERTEX_SIZE = 40

SLSF1_SPECULAR = 0x1
SLSF1_SKINNED = 0x2
SLSF1_RECEIVE_SHADOWS = 0x100
SLSF1_REMAPPABLE_TEXTURES = 0x2000000
SLSF1_OWN_EMIT = 0x400000
SLSF1_ZBUFFER_TEST = 0x80000000
SLSF2_DOUBLE_SIDED = 0x10
SLSF2_ENVMAP_LIGHT_FADE = 0x8000

ALPHA_BLEND = 0x00ED

def u8(v): return struct.pack('<B', v)
def u16(v): return struct.pack('<H', v)
def u32(v): return struct.pack('<I', v)
def i32(v): return struct.pack('<i', v)
def f32(*v): return struct.pack('<%df' % len(v), *v)
def half(*v): return struct.pack('<%de' % len(v), *v)
def sized(s): b = s.encode('latin1'); return u32(len(b)) + b
def short(s): b = s.encode('latin1') + b'\x00'; return u8(len(b)) + b

IDENTITY = (1, 0, 0, 0, 1, 0, 0, 0, 1)

def unit_byte(x):
    return max(0, min(255, round((x + 1.0) * 0.5 * 255)))

def vertex_bytes(v):
    pos, uv, n, t, b, bone = v
    out = f32(*pos) + f32(b[0])
    out += half(*uv)
    out += bytes([unit_byte(n[0]), unit_byte(n[1]), unit_byte(n[2]), unit_byte(b[1])])
    out += bytes([unit_byte(t[0]), unit_byte(t[1]), unit_byte(t[2]), unit_byte(b[2])])
    out += half(1.0, 0.0, 0.0, 0.0) + bytes([bone, 0, 0, 0])
    assert len(out) == VERTEX_SIZE
    return out

def av_object(name, flags, translation=(0.0, 0.0, 0.0)):
    return i32(name) + u32(0) + i32(-1) + u32(flags) + f32(*translation) + f32(*IDENTITY) + f32(1.0) + i32(-1)

def build_skinned_nif(root_name, bone_names, bone_offsets, shape_name, verts, tris, bound, bone_radius, shader, textures, alpha_flags):
    bones = len(bone_names)
    strings = [root_name] + bone_names + [shape_name]
    S = {s: k for k, s in enumerate(strings)}

    ROOT, BONE0 = 0, 1
    SHAPE = BONE0 + bones
    SKIN, DATA, PART, SHADER, TEXSET, ALPHA = range(SHAPE + 1, SHAPE + 7)
    blocks = []

    children = list(range(BONE0, BONE0 + bones)) + [SHAPE]
    blocks.append(('NiNode', av_object(S[root_name], 0x8000E) + u32(len(children)) + b''.join(i32(c) for c in children) + u32(0)))
    for i, name in enumerate(bone_names):
        blocks.append(('NiNode', av_object(S[name], 0x8000E, bone_offsets[i]) + u32(0) + u32(0)))

    shape = av_object(S[shape_name], 0x0E)
    shape += f32(*bound)
    shape += i32(SKIN) + i32(SHADER) + i32(ALPHA)
    shape += struct.pack('<Q', VERTEX_DESC) + u16(0) + u16(0) + u32(0) + u32(0)
    blocks.append(('BSTriShape', shape))

    # A plain NiSkinInstance, not a BSDismemberSkinInstance: the tear nodes hang under
    # the actor's head bone, and the game hides every dismember partition under an
    # actor's 3D whose body-part slot an equipped item covers (how a helmet hides the
    # ears, how SOS hides the body's crotch). A partition-less skin has no slot to hide.
    skin = i32(DATA) + i32(PART) + i32(ROOT) + u32(bones) + b''.join(i32(BONE0 + i) for i in range(bones))
    blocks.append(('NiSkinInstance', skin))

    data = f32(*IDENTITY) + f32(0.0, 0.0, 0.0) + f32(1.0) + u32(bones) + u8(1)
    for i in range(bones):
        group = [k for k, v in enumerate(verts) if v[5] == i]
        ox, oy, oz = bone_offsets[i]
        data += f32(*IDENTITY) + f32(0.0 - ox, 0.0 - oy, 0.0 - oz) + f32(1.0)
        data += f32(0.0, 0.0, 0.0, bone_radius)
        data += u16(len(group)) + b''.join(u16(k) + f32(1.0) for k in group)
    blocks.append(('NiSkinData', data))

    vb = b''.join(vertex_bytes(v) for v in verts)
    tri_bytes = b''.join(u16(a) + u16(b) + u16(c) for a, b, c in tris)
    part = u32(1) + u32(len(vb)) + u32(VERTEX_SIZE) + struct.pack('<Q', VERTEX_DESC) + vb
    part += u16(len(verts)) + u16(len(tris)) + u16(bones) + u16(0) + u16(4)
    part += b''.join(u16(i) for i in range(bones))
    part += u8(1) + b''.join(u16(k) for k in range(len(verts)))
    part += u8(1) + b''.join(f32(1.0, 0.0, 0.0, 0.0) for _ in verts)
    part += u8(1) + tri_bytes
    part += u8(1) + b''.join(bytes([v[5], 0, 0, 0]) for v in verts)
    part += u8(0) + u8(0) + struct.pack('<Q', VERTEX_DESC) + tri_bytes
    blocks.append(('NiSkinPartition', part))

    blocks.append(('BSLightingShaderProperty', shader(TEXSET)))

    paths = textures + [''] * (9 - len(textures))
    blocks.append(('BSShaderTextureSet', u32(len(paths)) + b''.join(sized(p) for p in paths)))

    blocks.append(('NiAlphaProperty', i32(-1) + u32(0) + i32(-1) + u16(alpha_flags) + u8(0)))

    types = []
    for t, _ in blocks:
        if t not in types:
            types.append(t)
    head = b'Gamebryo File Format, Version 20.2.0.7\n'
    head += u32(0x14020007) + u8(1) + u32(12) + u32(len(blocks)) + u32(100)
    head += short('') + short('Lacrima generator') + short('')
    head += u16(len(types)) + b''.join(sized(t) for t in types)
    head += b''.join(u16(types.index(t)) for t, _ in blocks)
    head += b''.join(u32(len(b)) for _, b in blocks)
    head += u32(len(strings)) + u32(max(len(s) for s in strings)) + b''.join(sized(s) for s in strings)
    head += u32(0)
    footer = u32(1) + i32(ROOT)
    return head + b''.join(b for _, b in blocks) + footer, len(verts), len(tris)

def downsample(image, width, height, normal):
    half_w, half_h = max(width // 2, 1), max(height // 2, 1)
    result = []
    for y in range(half_h):
        row = []
        for x in range(half_w):
            texels = [image[min(2 * y + dy, height - 1)][min(2 * x + dx, width - 1)] for dy in (0, 1) for dx in (0, 1)]
            r, g, b, a = (sum(t[k] for t in texels) / 4.0 for k in range(4))
            if normal:
                nx, ny, nz = (r / 127.5 - 1.0, g / 127.5 - 1.0, b / 127.5 - 1.0)
                length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                r, g, b = (unit_byte(nx / length), unit_byte(ny / length), unit_byte(nz / length))
            row.append(tuple(int(round(c)) for c in (r, g, b, a)))
        result.append(row)
    return result, half_w, half_h

def dds_bgra(width, height, pixel, normal=False):
    image = [[pixel(x, y) for x in range(width)] for y in range(height)]
    levels = [image]
    w, h = width, height
    while w > 1 or h > 1:
        image, w, h = downsample(image, w, h, normal)
        levels.append(image)
    header = b'DDS ' + u32(124) + u32(0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000) + u32(height) + u32(width)
    header += u32(width * 4) + u32(0) + u32(len(levels)) + b'\x00' * 44
    header += u32(32) + u32(0x41) + u32(0) + u32(32) + u32(0x00FF0000) + u32(0x0000FF00) + u32(0x000000FF) + u32(0xFF000000)
    header += u32(0x1000 | 0x8 | 0x400000) + u32(0) + u32(0) + u32(0) + u32(0)
    body = bytearray()
    for level in levels:
        for row in level:
            for r, g, b, a in row:
                body += bytes([b, g, r, a])
    return header + bytes(body)

def glint_normal_map(width, height):
    rng = random.Random(1337)
    waves = [(rng.randint(2, 5), 0, rng.uniform(0, 2 * math.pi), rng.uniform(0.4, 1.0)) for _ in range(3)]
    waves += [(rng.randint(1, 3), rng.randint(6, 28) * rng.choice((-1, 1)), rng.uniform(0, 2 * math.pi), rng.uniform(0.2, 0.7))
              for _ in range(12)]

    def sample(u, v):
        height_value = du = dv = 0.0
        for fu, fv, phase, amp in waves:
            angle = 2 * math.pi * (fu * u + fv * v) + phase
            height_value += amp * math.sin(angle)
            du += amp * 2 * math.pi * fu * math.cos(angle)
            dv += amp * 2 * math.pi * fv * math.cos(angle) / 8.0
        return height_value, du, dv

    samples = [[sample(x / width, y / height) for x in range(width)] for y in range(height)]
    peak_slope = max(math.hypot(du, dv) for row in samples for _, du, dv in row) or 1.0
    heights = [h for row in samples for h, _, _ in row]
    low, high = min(heights), max(heights)

    def pixel(x, y):
        h, du, dv = samples[y][x]
        nx, ny = -0.4 * du / peak_slope, -0.4 * dv / peak_slope
        nz = math.sqrt(max(1.0 - nx * nx - ny * ny, 0.0))
        sparkle = ((h - low) / (high - low)) ** 3 if high > low else 0.0
        return (unit_byte(nx), unit_byte(ny), unit_byte(nz), int(round(150 + 105 * sparkle)))

    return pixel
