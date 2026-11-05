"""Convert CodeParade's NonEuclidean .obj meshes to .glb for import into Unreal.

The original engine (github.com/HackerPoet/NonEuclidean, MIT) uses its own OBJ
dialect that standard importers can't read:
  * "f *"  / "f **"  -> triangle / quad made of the last 3 / 4 vertices and UVs
  * "c ..." lines    -> collider triangles (not rendered; ignored here)
  * "vt u v w"       -> 3-component UVs (w is a texture-array layer; only the
                        out-of-scope VR floor plan uses it, so it's dropped)

This script parses the files with the same rules as the original Mesh.cpp and
writes glTF binaries:
  * positions unchanged (metres, Y-up, right-handed: glTF's own convention)
  * flat per-face normals, computed exactly like Mesh::AddFace
  * UVs unchanged: the original flips BMP rows on load, so its v=0 is the top
    of the image, which is also glTF's convention
  * counter-clockwise front faces (the original renders with GL_CULL_FACE and
    the default CCW front face), also glTF's convention

Usage: python -I convert_original_meshes.py <src_meshes_dir> <out_dir>
"""

import json
import math
import os
import struct
import sys

SKIP = {"floorplan.obj"}  # VR demo (out of scope)


def parse_obj(path):
    verts, uvs = [], []  # palettes
    tris = []  # list of ((vi, ti), (vi, ti), (vi, ti)), 0-based
    with open(path, "r", encoding="ascii") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if line.startswith("v "):
                x, y, z = (float(t) for t in line[2:].split()[:3])
                verts.append((x, y, z))
            elif line.startswith("vt "):
                parts = [float(t) for t in line[3:].split()]
                uvs.append((parts[0], parts[1]))
            elif line.startswith("f "):
                body = line[2:]
                wild = body[:1] == "*"
                wild2 = body[1:2] == "*"
                if wild:
                    n = 4 if wild2 else 3
                    vi = list(range(len(verts) - n, len(verts)))
                    ti = list(range(len(uvs) - n, len(uvs))) if uvs else [None] * n
                    corners = list(zip(vi, ti))
                else:
                    corners = []
                    for tok in body.split():
                        fields = tok.split("/")
                        v = int(fields[0]) - 1
                        t = int(fields[1]) - 1 if len(fields) > 1 and fields[1] else v
                        corners.append((v, t if uvs else None))
                if len(corners) == 3:
                    tris.append(tuple(corners))
                elif len(corners) == 4:
                    a, b, c, d = corners
                    tris.append((a, b, c))
                    tris.append((c, d, a))  # same split as Mesh.cpp
                else:
                    raise ValueError(f"{path}: unsupported face '{line}'")
    return verts, uvs, tris


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(a):
    m = math.sqrt(a[0] ** 2 + a[1] ** 2 + a[2] ** 2)
    return (a[0] / m, a[1] / m, a[2] / m) if m > 0 else (0.0, 1.0, 0.0)


def build_streams(verts, uvs, tris):
    pos, nrm, tex = [], [], []
    for tri in tris:
        p = [verts[v] for v, _ in tri]
        n = normalize(cross(sub(p[1], p[0]), sub(p[2], p[0])))
        for (v, t), pv in zip(tri, p):
            pos.append(pv)
            nrm.append(n)
            tex.append(uvs[t] if t is not None else (0.0, 0.0))
    return pos, nrm, tex


def write_glb(path, name, pos, nrm, tex):
    count = len(pos)
    pos_b = b"".join(struct.pack("<3f", *p) for p in pos)
    nrm_b = b"".join(struct.pack("<3f", *n) for n in nrm)
    tex_b = b"".join(struct.pack("<2f", *t) for t in tex)
    blob = pos_b + nrm_b + tex_b
    mins = [min(p[i] for p in pos) for i in range(3)]
    maxs = [max(p[i] for p in pos) for i in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "NonEuclideanTest convert_original_meshes.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "material": 0,
            "mode": 4,
        }]}],
        "materials": [{"name": "MI_" + name, "doubleSided": False}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pos_b), "target": 34962},
            {"buffer": 0, "byteOffset": len(pos_b), "byteLength": len(nrm_b), "target": 34962},
            {"buffer": 0, "byteOffset": len(pos_b) + len(nrm_b), "byteLength": len(tex_b), "target": 34962},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3", "min": mins, "max": maxs},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC2"},
        ],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)
    return mins, maxs


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for fname in sorted(os.listdir(src)):
        if not fname.endswith(".obj") or fname in SKIP:
            continue
        verts, uvs, tris = parse_obj(os.path.join(src, fname))
        pos, nrm, tex = build_streams(verts, uvs, tris)
        name = "SM_" + "".join(p.capitalize() for p in fname[:-4].split("_"))
        mins, maxs = write_glb(os.path.join(out, name + ".glb"), name, pos, nrm, tex)
        print(f"{fname:20s} -> {name}.glb  tris={len(tris):5d}  min={[round(v, 3) for v in mins]}  max={[round(v, 3) for v in maxs]}")

    # Axis calibration mesh: one triangle touching +1 m on X, +2 m on Y, +3 m on Z.
    pos = [(1.0, 0.0, 0.0), (0.0, 2.0, 0.0), (0.0, 0.0, 3.0)]
    n = normalize(cross(sub(pos[1], pos[0]), sub(pos[2], pos[0])))
    write_glb(os.path.join(out, "SM_AxisCalibration.glb"), "SM_AxisCalibration", pos, [n] * 3, [(0, 0), (1, 0), (0, 1)])
    print("SM_AxisCalibration.glb (verts at +X 1 m, +Y 2 m, +Z 3 m)")


if __name__ == "__main__":
    main()
