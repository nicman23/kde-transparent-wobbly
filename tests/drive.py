#!/usr/bin/env python3
"""Drag a window through KWin's EIS interface and screenshot the workspace along the way.

Usage: drive.py <outdir> <x0> <y0> <x1> <y1>
Screenshots: drag-NN.png while moving, rel-NNNN.png at ms after release.
"""
import ctypes
import faulthandler
import os
import select
import sys
import time

import dbus
from PIL import Image

faulthandler.enable()

EI_EVENT_SEAT_ADDED, EI_EVENT_DEVICE_RESUMED = 3, 8
EI_DEVICE_CAP_POINTER_ABSOLUTE, EI_DEVICE_CAP_BUTTON = 1 << 1, 1 << 5
BTN_LEFT, BTN_RIGHT = 0x110, 0x111

out = sys.argv[1]
x0, y0, x1, y1 = map(float, sys.argv[2:6])
os.makedirs(out, exist_ok=True)

bus = dbus.SessionBus()
remote = bus.get_object("org.kde.KWin", "/org/kde/KWin/EIS/RemoteDesktop")
fd, _cookie = remote.connectToEIS(dbus.Int32(2), dbus_interface="org.kde.KWin.EIS.RemoteDesktop")

ei = ctypes.CDLL("libei.so.1")
for name, res, args in [
    ("ei_new_sender", ctypes.c_void_p, [ctypes.c_void_p]),
    ("ei_configure_name", None, [ctypes.c_void_p, ctypes.c_char_p]),
    ("ei_setup_backend_fd", ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    ("ei_get_fd", ctypes.c_int, [ctypes.c_void_p]),
    ("ei_dispatch", None, [ctypes.c_void_p]),
    ("ei_get_event", ctypes.c_void_p, [ctypes.c_void_p]),
    ("ei_event_get_type", ctypes.c_int, [ctypes.c_void_p]),
    ("ei_event_get_seat", ctypes.c_void_p, [ctypes.c_void_p]),
    ("ei_event_get_device", ctypes.c_void_p, [ctypes.c_void_p]),
    ("ei_event_unref", ctypes.c_void_p, [ctypes.c_void_p]),
    ("ei_device_has_capability", ctypes.c_bool, [ctypes.c_void_p, ctypes.c_int]),
    ("ei_device_ref", ctypes.c_void_p, [ctypes.c_void_p]),
    ("ei_device_start_emulating", None, [ctypes.c_void_p, ctypes.c_uint32]),
    ("ei_device_pointer_motion_absolute", None, [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]),
    ("ei_device_button_button", None, [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_bool]),
    ("ei_device_frame", None, [ctypes.c_void_p, ctypes.c_uint64]),
    ("ei_now", ctypes.c_uint64, [ctypes.c_void_p]),
]:
    getattr(ei, name).restype = res
    getattr(ei, name).argtypes = args
ei.ei_seat_bind_capabilities.restype = None

ctx = ei.ei_new_sender(None)
ei.ei_configure_name(ctx, b"gpuwobbly-test")
assert ei.ei_setup_backend_fd(ctx, fd.take()) == 0

device = None
deadline = time.time() + 5
while device is None:
    assert time.time() < deadline, "no EIS pointer device"
    select.select([ei.ei_get_fd(ctx)], [], [], 0.1)
    ei.ei_dispatch(ctx)
    while ev := ei.ei_get_event(ctx):
        kind = ei.ei_event_get_type(ev)
        if kind == EI_EVENT_SEAT_ADDED:
            ei.ei_seat_bind_capabilities(ctypes.c_void_p(ei.ei_event_get_seat(ev)), ctypes.c_int(EI_DEVICE_CAP_POINTER_ABSOLUTE), ctypes.c_int(EI_DEVICE_CAP_BUTTON), ctypes.c_void_p(None))
        elif kind == EI_EVENT_DEVICE_RESUMED:
            dev = ei.ei_event_get_device(ev)
            if ei.ei_device_has_capability(dev, EI_DEVICE_CAP_POINTER_ABSOLUTE):
                device = ei.ei_device_ref(dev)
        ei.ei_event_unref(ev)

ei.ei_device_start_emulating(device, 1)


def frame():
    ei.ei_device_frame(device, ei.ei_now(ctx))
    ei.ei_dispatch(ctx)


def move(x, y):
    ei.ei_device_pointer_motion_absolute(device, x, y)
    frame()


def button(pressed, which=BTN_LEFT):
    ei.ei_device_button_button(device, which, pressed)
    frame()


shooter = dbus.Interface(bus.get_object("org.kde.KWin", "/org/kde/KWin/ScreenShot2"), "org.kde.KWin.ScreenShot2")


def shot(name):
    r, w = os.pipe()
    meta = shooter.CaptureWorkspace({}, dbus.types.UnixFd(w))
    os.close(w)
    data = b""
    while chunk := os.read(r, 1 << 20):
        data += chunk
    os.close(r)
    width, height, stride, fmt = int(meta["width"]), int(meta["height"]), int(meta["stride"]), int(meta["format"])
    # QImage formats: 4-6 are 32 bit BGRA in memory, 16-18 RGBA
    raw = {4: "BGRA", 5: "BGRA", 6: "BGRA", 16: "RGBA", 17: "RGBA", 18: "RGBA"}[fmt]
    Image.frombuffer("RGBA", (width, height), data, "raw", raw, stride, 1).convert("RGB").save(f"{out}/{name}.png")


move(x0, y0)
time.sleep(0.2)
shot("before")
if os.environ.get("RIGHT_CLICK") == "1":
    # open a context menu at (x0, y0) and capture it
    button(True, BTN_RIGHT)
    button(False, BTN_RIGHT)
    time.sleep(1.0)
    shot("menu")
    sys.exit(0)
button(True)
time.sleep(0.05)
steps = int(os.environ.get("DRAG_STEPS", "40"))
interval = float(os.environ.get("DRAG_INTERVAL", "0.012"))
drag_shots = os.environ.get("DRAG_SHOTS", "1") == "1"
for i in range(1, steps + 1):
    move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
    time.sleep(interval)
    if drag_shots and i in (10, 25, 40):
        shot(f"drag-{i:02d}")
start = time.time()
button(False)
for ms in map(int, os.environ.get("RELEASE_SHOTS", "50,150,300,600,3000").split(",")):
    time.sleep(max(0, start + ms / 1000 - time.time()))
    shot(f"rel-{ms:04d}")
print("done")
