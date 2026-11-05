"""Frame-to-frame change along a -NESequence run, to spot pops at portal crossings.

  python -I Tools/NonEuclidean/seq_continuity.py Saved/Screenshots/NE/<run> [frame frame ...]

Prints the mean absolute difference (0-255) between consecutive frames, the median over the run,
and the frames whose change exceeds 2x the median. Frames listed on the command line (e.g. the
traversal frames) are reported explicitly.
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

folder = sys.argv[1]
marked = {int(f) for f in sys.argv[2:]}
files = sorted(glob.glob(os.path.join(folder, "*.png")))
frames = [int(os.path.splitext(f)[0].rsplit("_", 1)[1]) for f in files]
prev = None
diffs = {}
for n, f in zip(frames, files):
    img = np.asarray(Image.open(f).convert("RGB"), dtype=np.int16)
    if prev is not None:
        diffs[n] = float(np.abs(img - prev).mean())
    prev = img
med = float(np.median(list(diffs.values())))
print(f"{len(files)} frames, median frame-to-frame change {med:.2f}, max {max(diffs.values()):.2f} (frame {max(diffs, key=diffs.get)})")
for n in sorted(marked):
    if n in diffs:
        print(f"  frame {n:3d}: change {diffs[n]:.2f}  (prev {diffs.get(n - 1, float('nan')):.2f}, next {diffs.get(n + 1, float('nan')):.2f})")
outliers = [n for n, d in sorted(diffs.items()) if d > 2 * med and d > 1.0]
print("  frames changing > 2x median:", outliers if outliers else "none")
