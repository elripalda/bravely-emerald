#!/usr/bin/env python3
# usage: shot_stats.py a.png b.png
# Prints the mean RGB of two screenshots and B minus A, to check that a path change keeps colours.
import sys
from PIL import Image, ImageStat

a, b = (Image.open(p).convert("RGB") for p in sys.argv[1:3])
ma, mb = ImageStat.Stat(a).mean, ImageStat.Stat(b).mean
print("A mean", [round(x, 2) for x in ma])
print("B mean", [round(x, 2) for x in mb])
print("B-A   ", [round(y - x, 2) for x, y in zip(ma, mb)])
