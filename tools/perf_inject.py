#!/usr/bin/env python3
"""Inject synthetic key presses into a running port and log their timestamps.

Two backends are supported:

* uinput: creates a temporary virtual keyboard through /dev/uinput, so events
  travel through libinput, the Wayland compositor and XWayland before reaching
  SDL. This is the backend used for the recorded latency numbers.
* x11: XTestFakeKeyEvent. Some XWayland sessions expose the XTEST extension but
  do not deliver its events; the tool reports that case as zero matches.

Both backends record CLOCK_MONOTONIC immediately before each press request, on
the same clock as the port's local instrumentation.

Example:
  python3 tools/perf_inject.py --window sa2.sdl --key c --count 20 \
      --interval 0.25 --log /tmp/perf/sa2-latency.inject.log
"""

import argparse
import ctypes
import ctypes.util
import fcntl
import os
import struct
import sys
import time

Window = ctypes.c_ulong
KeySym = ctypes.c_ulong

UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_DEV_SETUP = 0x405C5503
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
EV_SYN = 0
EV_KEY = 1
SYN_REPORT = 0

KEY_CODES = {
    "c": 46,
    "x": 45,
    "s": 31,
    "d": 32,
    "Return": 28,
    "space": 57,
    "Left": 105,
    "Right": 106,
    "Up": 103,
    "Down": 108,
}


def load_x11():
    x11 = ctypes.CDLL(ctypes.util.find_library("X11"))
    xtst = ctypes.CDLL(ctypes.util.find_library("Xtst"))
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    x11.XDefaultRootWindow.restype = Window
    x11.XQueryTree.argtypes = [
        ctypes.c_void_p,
        Window,
        ctypes.POINTER(Window),
        ctypes.POINTER(Window),
        ctypes.POINTER(ctypes.POINTER(Window)),
        ctypes.POINTER(ctypes.c_uint),
    ]
    x11.XQueryTree.restype = ctypes.c_int
    x11.XFetchName.argtypes = [ctypes.c_void_p, Window, ctypes.POINTER(ctypes.c_char_p)]
    x11.XFetchName.restype = ctypes.c_int
    x11.XFree.argtypes = [ctypes.c_void_p]
    x11.XRaiseWindow.argtypes = [ctypes.c_void_p, Window]
    x11.XSetInputFocus.argtypes = [ctypes.c_void_p, Window, ctypes.c_int, ctypes.c_ulong]
    x11.XGetInputFocus.argtypes = [ctypes.c_void_p, ctypes.POINTER(Window), ctypes.POINTER(ctypes.c_int)]
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
    x11.XStringToKeysym.restype = KeySym
    x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, KeySym]
    x11.XKeysymToKeycode.restype = ctypes.c_ubyte
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeKeyEvent.restype = ctypes.c_int
    return x11, xtst


def window_name(x11, display, window):
    name = ctypes.c_char_p()
    if x11.XFetchName(display, window, ctypes.byref(name)) and name.value:
        title = name.value.decode("utf-8", "replace")
        x11.XFree(ctypes.cast(name, ctypes.c_void_p))
        return title
    return ""


def find_window(x11, display, root, needle, seen=None):
    """Return the deepest matching window, so the client wins over its frame."""
    if seen is None:
        seen = set()
    if root in seen:
        return None
    seen.add(root)
    best = root if needle.lower() in window_name(x11, display, root).lower() else None
    root_return = Window()
    parent_return = Window()
    children = ctypes.POINTER(Window)()
    count = ctypes.c_uint()
    if not x11.XQueryTree(display, root, ctypes.byref(root_return), ctypes.byref(parent_return),
                          ctypes.byref(children), ctypes.byref(count)):
        return best
    try:
        for index in range(count.value):
            found = find_window(x11, display, children[index], needle, seen)
            if found is not None:
                best = found
    finally:
        if children:
            x11.XFree(ctypes.cast(children, ctypes.c_void_p))
    return best


def focus_window(args):
    x11, xtst = load_x11()
    display = x11.XOpenDisplay(None)
    if not display:
        sys.exit("could not open X display")
    root = x11.XDefaultRootWindow(display)
    deadline = time.monotonic() + args.window_timeout
    window = None
    while time.monotonic() < deadline:
        window = find_window(x11, display, root, args.window)
        if window is not None:
            break
        time.sleep(0.1)
    if window is None:
        sys.exit(f"window containing {args.window!r} not found")
    x11.XRaiseWindow(display, window)
    x11.XSetInputFocus(display, window, 2, 0)
    x11.XFlush(display)
    time.sleep(0.2)
    focus = Window()
    revert = ctypes.c_int()
    x11.XGetInputFocus(display, ctypes.byref(focus), ctypes.byref(revert))
    return x11, xtst, display, window, focus.value


def create_uinput(name, keycodes):
    path = "/dev/uinput"
    if not os.access(path, os.W_OK):
        sys.exit(f"{path} is not writable; use --backend x11 or add permission")
    fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    for code in sorted(set(keycodes)):
        fcntl.ioctl(fd, UI_SET_KEYBIT, code)
    setup = struct.pack("<HHHH80sI", 0x03, 0x1209, 0x0001, 1, name.encode()[:79], 0)
    fcntl.ioctl(fd, UI_DEV_SETUP, setup)
    fcntl.ioctl(fd, UI_DEV_CREATE)
    time.sleep(0.5)
    return fd


def uinput_event(fd, code, value):
    now = time.clock_gettime(time.CLOCK_MONOTONIC)
    seconds = int(now)
    micros = int((now - seconds) * 1_000_000)
    event = struct.pack("<qqHHi", seconds, micros, EV_KEY, code, value)
    syn = struct.pack("<qqHHi", seconds, micros, EV_SYN, SYN_REPORT, 0)
    os.write(fd, event + syn)
    return now


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--window", required=True, help="substring of the X11 window title")
    parser.add_argument("--key", default="c", help="key name understood by the selected backend")
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--interval", type=float, default=0.25)
    parser.add_argument("--hold", type=float, default=0.03)
    parser.add_argument("--start-delay", type=float, default=0.0)
    parser.add_argument("--window-timeout", type=float, default=15.0)
    parser.add_argument("--backend", choices=("auto", "uinput", "x11"), default="auto")
    parser.add_argument("--log", required=True)
    args = parser.parse_args()

    if args.start_delay:
        time.sleep(args.start_delay)
    x11, xtst, display, window, focus = focus_window(args)
    backend = args.backend
    if backend == "auto":
        backend = "uinput" if os.access("/dev/uinput", os.W_OK) else "x11"

    if backend == "uinput":
        if args.key not in KEY_CODES:
            sys.exit(f"{args.key!r} has no uinput keycode in this helper")
        keycode = KEY_CODES[args.key]
        fd = create_uinput("sar-perf-inject", [keycode])
    else:
        keysym = x11.XStringToKeysym(args.key.encode())
        keycode = x11.XKeysymToKeycode(display, keysym)
        if keycode == 0:
            sys.exit(f"no X keycode for {args.key!r}")
        fd = None

    with open(args.log, "w") as log:
        log.write(f"backend,{backend}\n")
        log.write(f"focus,{focus},{window}\n")
        log.write(f"key,{args.key},{keycode}\n")
        for index in range(args.count):
            if backend == "uinput":
                uinput_event(fd, keycode, 0)
                time.sleep(0.01)
                pressed = uinput_event(fd, keycode, 1)
            else:
                xtst.XTestFakeKeyEvent(display, keycode, 0, 0)
                x11.XFlush(display)
                time.sleep(0.01)
                pressed = time.clock_gettime(time.CLOCK_MONOTONIC)
                xtst.XTestFakeKeyEvent(display, keycode, 1, 0)
                x11.XFlush(display)
            log.write(f"press,{index},{pressed:.9f},{args.key},{keycode}\n")
            log.flush()
            time.sleep(args.hold)
            released = time.clock_gettime(time.CLOCK_MONOTONIC)
            if backend == "uinput":
                uinput_event(fd, keycode, 0)
            else:
                xtst.XTestFakeKeyEvent(display, keycode, 0, 0)
                x11.XFlush(display)
            log.write(f"release,{index},{released:.9f},{args.key},{keycode}\n")
            log.flush()
            time.sleep(max(0.0, args.interval - args.hold - 0.01))

    if backend == "uinput":
        fcntl.ioctl(fd, UI_DEV_DESTROY)
        os.close(fd)
    x11.XCloseDisplay(display)
    print(f"injected {args.count} presses of {args.key} via {backend} into window {window}")


if __name__ == "__main__":
    main()
