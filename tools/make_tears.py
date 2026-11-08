# Writes the tear meshes and textures into dist\meshes\Lacrima and
# dist\textures\Lacrima. Adapted from OStrands' tools/make_fluid.py by
# LeonVanWinkle (Saint), GPL-3.0.
#
# The bone names and counts are read by src/Tears.cpp: BlobNN bones (at least 12)
# and ROPE_BONES RopeNN bones (Tears::kRopeBones). Change both sides together.
#
#   python tools/make_tears.py

import math
import os

from nifkit import (ALPHA_BLEND, SLSF1_RECEIVE_SHADOWS, SLSF1_REMAPPABLE_TEXTURES, SLSF1_SKINNED, SLSF1_SPECULAR,
                    SLSF1_ZBUFFER_TEST, TEXTURE_DIR, build_skinned_nif, dds_bgra, f32, glint_normal_map, i32, u32)

ROPE_BONES = 16
ROPE_SIDES = 8
SLSF2_ZBUFFER_WRITE = 0x1

# Tears read as wet skin: a slightly darkening, cool tint under a strong highlight.
# The game sets opacity, highlight strength and sharpness at run time from the
# [look] settings; the values here are only what the mesh starts with.
TEAR_COLOR = (178, 188, 198)
TEAR_ALPHA = 0.55


def normalize(p):
    length = math.sqrt(sum(v * v for v in p))
    return tuple(v / length for v in p)


def icosphere():
    t = (1 + 5 ** 0.5) / 2
    points = [normalize(p) for p in [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                                     (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
             (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
             (8, 6, 7), (9, 8, 1)]
    edges = {}

    def midpoint(a, b):
        key = tuple(sorted((a, b)))
        if key not in edges:
            edges[key] = len(points)
            points.append(normalize(tuple(points[a][i] + points[b][i] for i in range(3))))
        return edges[key]

    refined = []
    for a, b, c in faces:
        ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
        refined.extend([(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)])

    oriented = []
    for a, b, c in refined:
        u = tuple(points[b][i] - points[a][i] for i in range(3))
        v = tuple(points[c][i] - points[a][i] for i in range(3))
        cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        outward = sum(cross[i] * (points[a][i] + points[b][i] + points[c][i]) for i in range(3)) > 0
        oriented.append((a, b, c) if outward else (a, c, b))
    return points, oriented


def blob_vertices(points):
    result = []
    for index, (x, y, z) in enumerate(points):
        tangent = normalize((-y, x, 0)) if abs(z) < 0.999 else (1.0, 0.0, 0.0)
        bitangent = (y * tangent[2] - z * tangent[1], z * tangent[0] - x * tangent[2], x * tangent[1] - y * tangent[0])
        uv = (0.5 + math.atan2(y, x) / math.tau, math.acos(max(-1.0, min(1.0, z))) / math.pi)
        result.append(((x, y, z), uv, (x, y, z), tangent, bitangent, index))
    return result


def rope_geometry():
    verts = []
    for i in range(ROPE_BONES):
        for j in range(ROPE_SIDES + 1):
            a = 2 * math.pi * j / ROPE_SIDES
            c, s = math.cos(a), math.sin(a)
            verts.append(((c, float(i), s), (j / ROPE_SIDES, i / (ROPE_BONES - 1)), (c, 0.0, s), (-s, 0.0, c), (0.0, 1.0, 0.0), i))
    caps = []
    for i, ny in ((0, -1.0), (ROPE_BONES - 1, 1.0)):
        caps.append(len(verts))
        verts.append(((0.0, float(i), 0.0), (0.5, i / (ROPE_BONES - 1)), (0.0, ny, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0), i))
    ring = ROPE_SIDES + 1
    tris = []
    for i in range(ROPE_BONES - 1):
        for j in range(ROPE_SIDES):
            a = i * ring + j
            b = a + 1
            c = a + ring
            d = c + 1
            tris += [(a, c, b), (b, c, d)]
    last = (ROPE_BONES - 1) * ring
    for j in range(ROPE_SIDES):
        tris.append((caps[0], j + 1, j))
        tris.append((caps[1], last + j, last + j + 1))
    return verts, tris


def tear_shader(texset):
    flags1 = SLSF1_SPECULAR | SLSF1_SKINNED | SLSF1_RECEIVE_SHADOWS | SLSF1_REMAPPABLE_TEXTURES | SLSF1_ZBUFFER_TEST
    out = u32(0) + i32(-1) + u32(0) + i32(-1) + u32(flags1) + u32(SLSF2_ZBUFFER_WRITE)
    out += f32(0.0, 0.0) + f32(1.0, 1.0) + i32(texset)
    out += f32(0.0, 0.0, 0.0) + f32(1.0)
    out += u32(3)
    out += f32(TEAR_ALPHA) + f32(0.0) + f32(160.0)
    out += f32(1.0, 1.0, 1.0) + f32(3.0)
    out += f32(0.3) + f32(2.0)
    return out


TEXTURES = [TEXTURE_DIR + 'tear.dds', TEXTURE_DIR + 'tear_n.dds']


def build_blob():
    points, faces = icosphere()
    return build_skinned_nif('BlobRoot', ['Blob%02d' % i for i in range(len(points))], list(points), 'BlobShape',
                             blob_vertices(points), faces, (0.0, 0.0, 0.0, 1.0), 0.25, tear_shader, TEXTURES, ALPHA_BLEND)


def build_rope():
    verts, tris = rope_geometry()
    length = ROPE_BONES - 1
    return build_skinned_nif('RopeRoot', ['Rope%02d' % i for i in range(ROPE_BONES)], [(0.0, float(i), 0.0) for i in range(ROPE_BONES)],
                             'RopeShape', verts, tris, (0.0, length / 2, 0.0, length / 2 + 1.0), 1.0, tear_shader, TEXTURES,
                             ALPHA_BLEND)


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dist')
    mesh_dir = os.path.join(root, 'meshes', 'Lacrima')
    tex_dir = os.path.join(root, 'textures', 'Lacrima')
    os.makedirs(mesh_dir, exist_ok=True)
    os.makedirs(tex_dir, exist_ok=True)

    for kind, builder in (('blob', build_blob), ('rope', build_rope)):
        data, verts, tris = builder()
        open(os.path.join(mesh_dir, 'tear_%s.nif' % kind), 'wb').write(data)
        print('tear_%s.nif: %d vertices, %d triangles, %d bytes' % (kind, verts, tris, len(data)))

    r, g, b = TEAR_COLOR
    open(os.path.join(tex_dir, 'tear.dds'), 'wb').write(dds_bgra(8, 8, lambda x, y: (r, g, b, 255)))
    # A gentle ripple normal map; its alpha is the specular mask, so the highlight
    # breaks into small glints along a trail instead of one flat sheen.
    open(os.path.join(tex_dir, 'tear_n.dds'), 'wb').write(dds_bgra(32, 64, glint_normal_map(32, 64), normal=True))
    print('tear.dds, tear_n.dds')


if __name__ == '__main__':
    main()
