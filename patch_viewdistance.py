#!/usr/bin/env python3
"""Raise the draw distance in Interstate '82 by editing i82.zfs in place.

Each of the game's 34 levels carries its own World_Data block as plain text
inside i82.zfs:

    Fog_Max:        200.00
    Clipping_Plane: 200

The two values are equal by design -- the fog is what hides the edge where the
world stops being drawn. Raising the clipping plane alone would only move the
pop-in into a fog bank, so both are scaled here, with separate factors.

Fog_Alpha is 128, so the fog never becomes fully opaque and geometry beyond
Fog_Max still reads as a silhouette. Scaling the clipping plane further than
the fog therefore adds depth rather than drawing something invisible: 3x for
clipping and 2x for fog is the default for that reason.

On the hardware this was developed against the change cost nothing measurable
(107 fps at 3840x2160, against 100 before), because the engine is not limited
by geometry here. The short view distance was a decision made for 1999
hardware, not a constraint of the engine.

The values sit in fixed-width fields padded with trailing spaces, so each one
is rewritten to the same byte length. Every offset in the archive directory
stays valid and the file does not need repacking. A field that cannot hold its
new value in the existing width is left untouched rather than shifting the
archive. The archive carries no checksum -- the four-byte field in each
directory entry is a 1999 timestamp.

This edits your installed game data. It keeps an untouched copy as
i82.zfs.original next to the archive and always computes from that copy, so
running it repeatedly with different factors will not compound. Use --restore
to put the original back.

    python patch_viewdistance.py                     # what it would change
    python patch_viewdistance.py --apply             # clipping 3x, fog 2x
    python patch_viewdistance.py --clip 4 --fog 2.5 --apply
    python patch_viewdistance.py --restore

Requires nothing but a Python 3 interpreter. Not affiliated with the game's
publisher; it changes data you already own and redistributes nothing.
"""
import argparse
import os
import re
import shutil
import sys

DEFAULT_GAMEDIR = r"C:\Program Files (x86)\GOG Galaxy\Games\Interstate 82"
ARCHIVE = "i82.zfs"
ORIGINAL = "i82.zfs.original"

# key -> value is written with two decimals
KEYS = [
    (b"Clipping_Plane", False, "clip"),
    (b"Underground_Clipping_Plane", False, "clip"),
    (b"Fog_Max", True, "fog"),
    (b"Underground_Fog_Max", True, "fog"),
]


def scale(data, clip, fog, verbose):
    out = bytearray(data)
    total_changed = total_skipped = 0
    for key, is_float, which in KEYS:
        factor = clip if which == "clip" else fog
        # anchored to the start of a line so Underground_* is never matched as
        # a bare Clipping_Plane
        pat = re.compile(
            br"(?m)^(\t?" + re.escape(key) + br":\t)([0-9]+(?:\.[0-9]+)?)([ \t]*)(?=[\r\n])")
        changed = skipped = 0
        seen = {}
        for m in pat.finditer(bytes(data)):
            val, pad = m.group(2), m.group(3)
            field = len(val) + len(pad)
            new = float(val) * factor
            txt = (b"%.2f" % new) if is_float else (b"%d" % int(round(new)))
            if len(txt) > field:
                skipped += 1
                continue
            start = m.start(2)
            out[start:start + field] = txt + b" " * (field - len(txt))
            changed += 1
            seen[(val, txt)] = seen.get((val, txt), 0) + 1
        if verbose:
            for (o, n), c in sorted(seen.items()):
                print("    %-28s %8s -> %-8s (%d levels)"
                      % (key.decode(), o.decode(), n.decode(), c))
        if skipped:
            print("    %-28s %d value(s) too wide for their field, left alone"
                  % (key.decode(), skipped))
        total_changed += changed
        total_skipped += skipped
    return bytes(out), total_changed, total_skipped


def main():
    ap = argparse.ArgumentParser(
        description="Raise Interstate '82's draw distance by editing i82.zfs in place.")
    ap.add_argument("--game", default=DEFAULT_GAMEDIR, help="game directory")
    ap.add_argument("--clip", type=float, default=3.0, help="clipping plane factor (default 3)")
    ap.add_argument("--fog", type=float, default=2.0, help="fog distance factor (default 2)")
    ap.add_argument("--apply", action="store_true", help="write the change (default: dry run)")
    ap.add_argument("--restore", action="store_true", help="put the original archive back")
    a = ap.parse_args()

    archive = os.path.join(a.game, ARCHIVE)
    original = os.path.join(a.game, ORIGINAL)
    if not os.path.isfile(archive):
        print("not found: %s\nPass --game with your installation directory." % archive)
        return 2

    if a.restore:
        if not os.path.isfile(original):
            print("no %s to restore from" % ORIGINAL)
            return 2
        shutil.copy2(original, archive)
        print("restored %s from %s" % (ARCHIVE, ORIGINAL))
        return 0

    if not os.path.isfile(original):
        if a.apply:
            shutil.copy2(archive, original)
            print("kept an untouched copy as %s (%d bytes)"
                  % (ORIGINAL, os.path.getsize(original)))
        else:
            print("no %s yet; --apply would make one first" % ORIGINAL)

    src = original if os.path.isfile(original) else archive
    data = open(src, "rb").read()
    print("source: %s, %d bytes -- clipping x%g, fog x%g"
          % (os.path.basename(src), len(data), a.clip, a.fog))

    out, changed, skipped = scale(data, a.clip, a.fog, verbose=True)
    if len(out) != len(data):
        print("size would change by %+d bytes -- refusing to write"
              % (len(out) - len(data)))
        return 3
    print("  %d values rewritten, %d skipped, size unchanged" % (changed, skipped))
    if not changed:
        print("  nothing matched -- is this the right archive?")
        return 4

    if not a.apply:
        print("\nDry run. Pass --apply to write, --restore to undo later.")
        return 0

    open(archive, "wb").write(out)
    print("written to %s" % archive)
    print("Start a mission to see it; the menu shows nothing of this.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
