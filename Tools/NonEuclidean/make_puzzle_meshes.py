"""Write the object-puzzle meshes (L_NE_ObjectPuzzles) as .glb files, for import by
ue_build_content.py (stage "puzzle").

  python -I Tools/NonEuclidean/make_puzzle_meshes.py SourceArt/NonEuclidean/Generated/Puzzle

SM_PW_Key: a chiral tetracube (four 20 cm cubes in a three-step twist), so its mirror image can't be
  turned to match it. One end cube ("Head", red) makes its orientation easy to read; the other three
  are "Body". Pivot at the centroid, so the point that crosses portals is the middle of the key.
SM_PW_Crate: a 40 cm cube, pivot at its centre.

Cells are given in Unreal coordinates (cm, Z up). glTF is metres, Y up, and the importer maps
glTF (x, y, z) to Unreal (x, z, y) * 100 (see README), so vertices are written as (X, Z, Y) / 100.
KEY_CELLS and CELL are repeated in ue_build_content.py for the box collision.
"""

import json
import os
import struct
import sys

CELL = 20.0  # cm
KEY_CELLS = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 1, 1)]  # Unreal axes; the first is the head
CRATE = 40.0  # cm


def key_centroid():
    n = len(KEY_CELLS)
    return tuple(sum(c[i] for c in KEY_CELLS) / n for i in range(3))


def to_gltf(p):
    """Unreal cm (X, Y, Z) -> glTF metres (x, y, z) = (X, Z, Y) / 100."""
    return (p[0] / 100.0, p[2] / 100.0, p[1] / 100.0)


def cube_faces(centre_ue, half):
    """Triangles (pos, normal, uv) of an axis-aligned cube, counter-clockwise from outside in glTF space."""
    c = to_gltf(centre_ue)
    h = half / 100.0
    tris = []
    axes = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]
    for a in range(3):
        for sign in (1, -1):
            n = tuple(sign * v for v in axes[a])
            u = axes[(a + 1) % 3]
            v = axes[(a + 2) % 3]
            if sign < 0:
                u, v = v, u  # keep u x v == n
            corners = []
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                corners.append(tuple(c[i] + h * (n[i] + su * u[i] + sv * v[i]) for i in range(3)))
            uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
            for i0, i1, i2 in ((0, 1, 2), (2, 3, 0)):
                tris.append([(corners[i], n, uvs[i]) for i in (i0, i1, i2)])
    return tris


def write_glb(path, name, primitives):
    """primitives: list of (material name, triangles)."""
    blob = b""
    views, accessors, prims, materials = [], [], [], []
    for mat_index, (mat_name, tris) in enumerate(primitives):
        verts = [v for tri in tris for v in tri]
        streams = (
            (b"".join(struct.pack("<3f", *v[0]) for v in verts), "VEC3", True),
            (b"".join(struct.pack("<3f", *v[1]) for v in verts), "VEC3", False),
            (b"".join(struct.pack("<2f", *v[2]) for v in verts), "VEC2", False),
        )
        attr = {}
        for key, (data, kind, with_bounds) in zip(("POSITION", "NORMAL", "TEXCOORD_0"), streams):
            views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": 34962})
            acc = {"bufferView": len(views) - 1, "componentType": 5126, "count": len(verts), "type": kind}
            if with_bounds:
                acc["min"] = [min(v[0][i] for v in verts) for i in range(3)]
                acc["max"] = [max(v[0][i] for v in verts) for i in range(3)]
            accessors.append(acc)
            attr[key] = len(accessors) - 1
            blob += data
        prims.append({"attributes": attr, "material": mat_index, "mode": 4})
        materials.append({"name": mat_name, "doubleSided": False})
    gltf = {
        "asset": {"version": "2.0", "generator": "NonEuclideanTest make_puzzle_meshes.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": prims}],
        "materials": materials,
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    centroid = key_centroid()
    head, body = [], []
    for i, cell in enumerate(KEY_CELLS):
        centre = tuple((cell[k] - centroid[k]) * CELL for k in range(3))
        (head if i == 0 else body).extend(cube_faces(centre, CELL / 2))
        print(f"key cell {cell}: centre {centre} cm")
    write_glb(os.path.join(out, "SM_PW_Key.glb"), "SM_PW_Key", [("Head", head), ("Body", body)])
    write_glb(os.path.join(out, "SM_PW_Crate.glb"), "SM_PW_Crate", [("Crate", cube_faces((0, 0, 0), CRATE / 2))])
    print(f"wrote SM_PW_Key.glb, SM_PW_Crate.glb to {out}")


if __name__ == "__main__":
    main()
