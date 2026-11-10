#!/usr/bin/env python3
"""Print HID report descriptor items found in a firmware region.
usage: hiddesc.py app.bin START END   (flash addresses, app.bin loaded at 0x1E00120)
Shows Usage Page / Usage / Report ID / Collection / Report Size/Count / Input lines; enough to map report bits.
"""
import sys

PAGES = {1: "GenericDesktop", 7: "Keyboard", 9: "Button", 0x0C: "Consumer", 0x0D: "Digitizer"}
CONSUMER = {0x30: "Power", 0x40: "Menu", 0xB5: "NextTrack", 0xB6: "PrevTrack", 0xCD: "PlayPause", 0xE2: "Mute",
            0xE9: "VolUp", 0xEA: "VolDown", 0x223: "ACHome", 0x224: "ACBack", 0x221: "ACSearch", 0x01: "ConsumerControl",
            0xB0: "Play", 0xB7: "Stop", 0x65: "Snapshot", 0x6F: "BrightUp", 0x70: "BrightDown"}
TAGS = {(0, 8): "Input", (0, 9): "Output", (0, 10): "Collection", (0, 11): "Feature", (0, 12): "EndCollection", (1, 0): "UsagePage",
        (1, 1): "LogMin", (1, 2): "LogMax", (1, 3): "PhysMin", (1, 4): "PhysMax", (1, 5): "UnitExp", (1, 6): "Unit", (1, 7): "ReportSize", (1, 8): "ReportID", (1, 9): "ReportCount",
        (2, 0): "Usage", (2, 1): "UsageMin", (2, 2): "UsageMax"}

def items(b):
    i = 0
    while i < len(b):
        p = b[i]; n = (0, 1, 2, 4)[p & 3]; typ, tag = (p >> 2) & 3, p >> 4
        yield i, TAGS.get((typ, tag)), int.from_bytes(b[i + 1:i + 1 + n], "little"), n
        i += 1 + n

if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read(); s, e = (int(x, 16) - 0x1E00120 for x in sys.argv[2:4])
    page, depth = 0, 0
    for off, name, v, n in items(d[s:e]):
        if name is None:
            print(f"{s + off + 0x1E00120:#x}  ?? item {d[s + off]:#04x} (not a descriptor here)"); break
        if name == "UsagePage": page = v
        if name == "EndCollection": depth -= 1
        txt = {"UsagePage": PAGES.get(v, hex(v)), "Usage": (CONSUMER.get(v) if page == 0x0C else None) or hex(v),
               "UsageMin": hex(v), "UsageMax": hex(v)}.get(name, hex(v) if n else "")
        print(f"{s + off + 0x1E00120:#x}  {'  ' * depth}{name} {txt}")
        if name == "Collection": depth += 1
