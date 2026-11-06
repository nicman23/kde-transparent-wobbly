#!/usr/bin/env python3
"""Prints when the screen stopped changing after release, from drive.py's rel-NNNN.png series.

Usage: settle.py <shots dir>
"""
import glob
import sys

import numpy as np
from PIL import Image

shots = sorted(glob.glob(f"{sys.argv[1]}/rel-*.png"))
frames = [(int(p[-8:-4]), np.asarray(Image.open(p).convert("L"), dtype=np.int16)) for p in shots]
final = frames[-1][1]
moving = [ms for ms, f in frames[:-1] if np.abs(f - final).max() > 8]
print(f"last frame differing from the final one: {moving[-1] if moving else 0} ms")
