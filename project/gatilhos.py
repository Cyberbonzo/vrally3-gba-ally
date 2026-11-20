"""Translate this game's two analog triggers to its keyboard acceleration and brake."""
import ctypes as C
import fcntl
import os
from pathlib import Path
import select
import signal
import struct
import sys
import time


EVENT = struct.Struct("@llHHi")
ABS_INFO = struct.Struct("=6i")
AXES = {5: b"z", 2: b"x"}
NAMES = ("Microsoft X-Box One Elite 2 pad", "Microsoft X-Box 360 pad 0")


def bind(lib, name, result, *args):
    fn = getattr(lib, name)
    fn.restype = result
    fn.argtypes = list(args)
    return fn


def open_pad():
    candidates = {}
    for event in Path("/sys/class/input").glob("event*"):
        try:
            name = (event / "device/name").read_text().strip()
        except OSError:
            continue
        if name in NAMES:
            candidates[name] = event.name
    for name in NAMES:
        event = candidates.get(name)
        if event is None:
            continue
        try:
            fd = os.open("/dev/input/" + event, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            continue
        try:
            axes = {
                n: ABS_INFO.unpack(fcntl.ioctl(fd, 0x80184540 + n, bytes(ABS_INFO.size)))
                for n in AXES
            }
            if all(a[2] > a[1] for a in axes.values()):
                return fd, axes
        except OSError:
            pass
        os.close(fd)
    return None, {}


def main():
    pid = int(sys.argv[1])
    x11 = C.CDLL("libX11.so.6")
    test = C.CDLL("libXtst.so.6")
    ptr, uint, word, integer = C.c_void_p, C.c_uint, C.c_ulong, C.c_int
    xopen = bind(x11, "XOpenDisplay", ptr, C.c_char_p)
    xclose = bind(x11, "XCloseDisplay", integer, ptr)
    focus = bind(x11, "XGetInputFocus", integer, ptr, C.POINTER(word), C.POINTER(integer))
    get_name = bind(x11, "XFetchName", integer, ptr, word, C.POINTER(ptr))
    free = bind(x11, "XFree", integer, ptr)
    keycode = bind(x11, "XKeysymToKeycode", C.c_ubyte, ptr, word)
    flush = bind(x11, "XFlush", integer, ptr)
    fake = bind(test, "XTestFakeKeyEvent", integer, ptr, uint, integer, word)
    dpy = xopen(None)
    if not dpy:
        return
    keys = {n: keycode(dpy, s[0]) for n, s in AXES.items()}
    down = dict.fromkeys(AXES, False)
    fd = None
    final = False

    def stop(_sig, _frame):
        nonlocal final
        final = True

    def change(axis, requested):
        if down[axis] != requested:
            fake(dpy, keys[axis], requested, 0)
            flush(dpy)
            down[axis] = requested

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        while not final and Path(f"/proc/{pid}").exists():
            if fd is None:
                fd, axes = open_pad()
                if fd is None:
                    time.sleep(0.35)
                    continue
                values = {n: p[0] for n, p in axes.items()}
            if select.select([fd], [], [], 0.02)[0]:
                try:
                    data = os.read(fd, EVENT.size * 48)
                except BlockingIOError:
                    continue
                except OSError:
                    data = b""
                if not data:
                    os.close(fd)
                    fd = None
                    for axis in AXES:
                        change(axis, False)
                    continue
                for _, _, kind, axis, value in EVENT.iter_unpack(data):
                    if kind == 3 and axis in values:
                        values[axis] = value
            window, revert = word(), integer()
            focus(dpy, C.byref(window), C.byref(revert))
            title = ptr()
            active = False
            if window.value > 1 and get_name(dpy, window, C.byref(title)) and title.value:
                active = C.string_at(title).startswith(b"vrally3-us")
                free(title)
            for axis, value in values.items():
                _, low, high, *_ = axes[axis]
                threshold = 0.12 if down[axis] else 0.19
                change(axis, active and value >= low + (high - low) * threshold)
    finally:
        for axis in AXES:
            change(axis, False)
        if fd is not None:
            os.close(fd)
        xclose(dpy)


if __name__ == "__main__":
    main()

