#!/usr/bin/env python3
"""Render the enclosure STL exports to PNG previews (matplotlib, no CAD needed).

    python3 enclosure/tools/render_stl.py            # writes docs/images/enclosure_*.png
"""
import struct
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

ROOT = Path(__file__).resolve().parents[2]
STL_DIR = ROOT / "enclosure" / "stl"
OUT_DIR = ROOT / "docs" / "images"


def load_stl(path):
    data = path.read_bytes()
    n = struct.unpack_from("<I", data, 80)[0]
    rec = np.frombuffer(data, dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")]),
                        count=n, offset=84)
    return rec["v"].astype(float)


def shade(tris, base_rgb):
    normals = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    normals /= np.linalg.norm(normals, axis=1, keepdims=True) + 1e-12
    light = np.array([-0.4, -0.6, 0.9]); light /= np.linalg.norm(light)
    lam = np.clip(normals @ light, 0, 1)
    k = 0.35 + 0.65 * lam
    return np.clip(np.outer(k, base_rgb), 0, 1)


def render(parts, out, elev=28, azim=-60):
    fig = plt.figure(figsize=(8, 5.5), dpi=150)
    ax = fig.add_subplot(projection="3d")
    # One collection, so matplotlib depth-sorts all parts together.
    tris = np.concatenate([t for t, _ in parts])
    cols = np.concatenate([shade(t, rgb) for t, rgb in parts])
    ax.add_collection3d(Poly3DCollection(tris, facecolors=cols, edgecolors=cols, linewidths=0.3))
    allv = tris.reshape(-1, 3)
    lo, hi = allv.min(0), allv.max(0)
    ax.set_xlim(lo[0], hi[0]); ax.set_ylim(lo[1], hi[1]); ax.set_zlim(lo[2], hi[2])
    ax.set_box_aspect(hi - lo)
    ax.view_init(elev=elev, azim=azim)
    ax.set_axis_off()
    fig.subplots_adjust(0, 0, 1, 1)
    fig.savefig(out, transparent=True, bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)
    print("wrote", out.relative_to(ROOT))


def main():
    base = load_stl(STL_DIR / "base.stl")
    lid_l = load_stl(STL_DIR / "lid_left.stl")
    lid_r = load_stl(STL_DIR / "lid_right.stl")
    for name, t in (("base", base), ("lid_left", lid_l), ("lid_right", lid_r)):
        print(name, len(t), "tris  min", t.reshape(-1, 3).min(0).round(1), "max", t.reshape(-1, 3).max(0).round(1))
    if "--info" in sys.argv:
        return
    grey, blue, teal = np.array([0.62, 0.64, 0.68]), np.array([0.30, 0.50, 0.85]), np.array([0.25, 0.70, 0.65])
    # Exploded view: lids lifted above the base.
    lift = 45  # lids are exported in their assembled position
    off = np.array([0, 0, lift])
    render([(base, grey), (lid_l + off, blue), (lid_r + off, teal)], OUT_DIR / "enclosure_exploded.png")
    render([(lid_l, blue), (lid_r, teal)], OUT_DIR / "enclosure_lids.png", elev=55, azim=-90)


if __name__ == "__main__":
    main()
