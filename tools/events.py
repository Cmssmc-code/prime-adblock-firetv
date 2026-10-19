#!/usr/bin/env python3
"""Read the hook's persistent event log from the device and summarise it.

    python tools/events.py pull    [--serial HOST:5555] [--adb adb]   # copy to ./events.log
    python tools/events.py summary [events.log ...]                   # counts per event type

The hook writes /sdcard/Android/data/<package>/files/pvhook_events.log (rotated at 1 MB, previous
generation in .log.1). Lines look like:
    2026-10-19 15:40:56.529 pid=5791 AD-KILLED chunks=6 bytes=67116
Events: START, HOOK-ON, HOOK-OFF, AD-GATED, AD-KILLED, AD-MISSED, AD-UNGATED, CRASH.
A healthy day has as many AD-GATED as AD-KILLED, and no AD-MISSED / AD-UNGATED / CRASH / HOOK-OFF.
"""
import argparse, collections, re, subprocess, sys

PKG = "com.amazon.amazonvideo.livingroom.mod"

def pull(a):
    base = ["adb"] if not a.adb else [a.adb]
    if a.serial: base += ["-s", a.serial]
    out = b""
    for suffix in (".1", ""):
        r = subprocess.run(base + ["exec-out", f"cat /sdcard/Android/data/{PKG}/files/pvhook_events.log{suffix}"],
                           capture_output=True)
        if r.returncode == 0: out += r.stdout
    open(a.out, "wb").write(out)
    print(f"{len(out)} bytes -> {a.out}")

def summary(files):
    ev = collections.Counter(); bad = []
    for f in files:
        for line in open(f, errors="replace"):
            m = re.match(r"(\S+ \S+) pid=(\d+) (\S+)(.*)", line)
            if not m: continue
            ev[m.group(3)] += 1
            if m.group(3) in ("AD-MISSED", "AD-UNGATED", "CRASH", "HOOK-OFF"): bad.append(line.rstrip())
    for k, v in sorted(ev.items()): print(f"{k:12} {v}")
    print(f"\nsuccess rate: {ev['AD-KILLED']}/{ev['AD-GATED']} gated responses emptied")
    if bad:
        print("\nto look at:"); [print(" ", b) for b in bad[:50]]

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pull"); p.add_argument("--serial"); p.add_argument("--adb"); p.add_argument("--out", default="events.log")
    s = sub.add_parser("summary"); s.add_argument("files", nargs="*", default=["events.log"])
    a = ap.parse_args()
    pull(a) if a.cmd == "pull" else summary(a.files)
