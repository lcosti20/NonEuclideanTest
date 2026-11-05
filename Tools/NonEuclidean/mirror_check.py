"""Checks a mirroring portal against an independent prediction (L_NE_Mirror).

  python -I Tools/NonEuclidean/mirror_check.py <shot> <hidden> <hidden_reflected> <pink> [--plain]

A mirroring portal shows the world reflected across its mirror plane. The reflected world seen
from a camera is the left-right flip of the normal world seen from the reflected camera, so:
  - inside the portal (pink pixels of the Portal.Debug shot): <shot> should equal
    hflip(<hidden_reflected>), the reflected pose rendered with Portal.Hide 1;
  - outside it: <shot> should equal <hidden>, the same pose with Portal.Hide 1.
Prints the mean absolute difference (0-255) of each region and the share of pixels off by more
than 16. --plain checks an ordinary portal the same way: inside, <shot> should equal <hidden>
(pass it as <hidden_reflected> too).
"""
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


shot, hidden, hidden_reflected, pink = (load(p) for p in sys.argv[1:5])
# Pink after any tonemapping: strong red and blue, clearly less green.
mask = (pink[..., 0] > 200) & (pink[..., 2] > 200) & (pink[..., 1] < pink[..., 0] - 60)
through = hidden_reflected if "--plain" in sys.argv else hidden_reflected[:, ::-1]
predicted = np.where(mask[..., None], through, hidden)
diff = np.abs(shot - predicted).max(axis=2)
for name, region in (("inside portal", mask), ("outside portal", ~mask)):
    if region.any():
        d = diff[region]
        print(f"{name}: {region.mean() * 100:.1f}% of the image, mean diff {d.mean():.2f}, "
              f"{(d > 16).mean() * 100:.2f}% of pixels off by > 16")
Image.fromarray(np.clip(diff * 4, 0, 255).astype(np.uint8)).save(sys.argv[1].replace(".png", "_diff.png"))
