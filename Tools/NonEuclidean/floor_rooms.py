"""Classify the room under the camera in each frame of a house run by its floor colour.

  python -I Tools/NonEuclidean/floor_rooms.py Saved/Screenshots/NE/<run>

Samples the bottom-centre of each frame and names the nearest of the original house floor
colours (blue, red, green, grey, purple) or the outside ground.
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

# Floor colours as rendered (texel x 0.9 shading for an upward-facing floor), from the
# three_room / three_room2 textures; both checker tones of each floor. The outside ground uses
# the green tones too.
ROOMS = {
    "blue": [(0, 145, 208), (137, 195, 210)],
    "red": [(122, 0, 18), (213, 25, 32)],
    "green": [(30, 159, 68), (162, 207, 26)],
    "grey": [(114, 114, 114), (196, 196, 196)],
    "purple": [(113, 0, 208), (171, 138, 210)],
    "orange": [(229, 114, 35), (229, 180, 12)],
    "olive": [(112, 122, 0), (175, 213, 26)],
}

files = sorted(glob.glob(os.path.join(sys.argv[1], "*.png")))
sequence = []
for f in files:
    img = np.asarray(Image.open(f).convert("RGB"), dtype=float)
    patch = img[-30:, 560:720].reshape(-1, 3)
    best = None
    for name, tones in ROOMS.items():
        d = min(np.abs(patch - np.array(t)).sum(1).mean() for t in tones)
        if best is None or d < best[1]:
            best = (name, d)
    frame = int(os.path.splitext(f)[0].rsplit("_", 1)[1])
    sequence.append((frame, best[0]))
rooms = []
for frame, room in sequence:
    if not rooms or rooms[-1][1] != room:
        rooms.append((frame, room))
print(" -> ".join(f"{room}@{frame}" for frame, room in rooms))
