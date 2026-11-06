#!/usr/bin/env python3
"""Asserts on a `run-wobbly.sh gpuwobbly` result: the window wobbled, was frosted, then was let go.

Usage: check.py <shots dir> <kwin log>   (after run-wobbly.sh gpuwobbly)
"""
import re
import sys

from PIL import Image

shots, log = sys.argv[1:3]
energy = [(float(a), float(v)) for a, v in re.findall(r"energy (\S+) (\S+)", open(log).read())]
assert energy, "physics never ran"
assert max(v for _, v in energy) > 10, "window never wobbled"
assert energy[-1][0] < 0.5 and energy[-1][1] < 0.5, f"never settled: {energy[-1]}"


def pixel(name):
    # a checker square of the released window (frame at 600,400, client area from y 436)
    return Image.open(f"{shots}/{name}.png").convert("RGB").getpixel((625, 460))


assert pixel("rel-3000") in ((0xF0, 0xC0, 0x20), (0x20, 0x50, 0xA0)), "settled window is not drawn plain"
assert pixel("rel-0150") != pixel("rel-3000"), "no translucency or frost while wobbling"
print(f"ok: {len(energy)} energy readings, peak velocity sum {max(v for _, v in energy):.0f}")
