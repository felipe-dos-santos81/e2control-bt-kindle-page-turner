#!/usr/bin/env python3
"""Unpack a Jieli AC632N (BD19) raw flash image (jl_isd.bin).

Layout (from hlyflash/flashimage.py and the BD19 SDK):
  0x0000  header, 32 B, ENC key 0xFFFF
  0x0020  boot file table, 32 B records, ENC key 0xFFFF (uboot.boot, isd_config.ini, app_dir_head, key_mac)
  app_dir_head.offset  app area, ENC per 32 B block with key = chip_key ^ (block_offset >> 2)
                       starts with its own file table (app_area_head, app.bin, cfg_tool.bin, VM, PRCT, BTIF)
app.bin runs from CODE_BEG = 0x1E00120 (cpu/bd19/sdk_ld.c), so the app area maps at 0x1E00000.

usage: jlimg.py IMAGE OUTDIR [--key HEX]   (key is brute-forced when omitted)
       jlimg.py pack BASE_IMAGE APP_BIN OUT [--cfg CFG_TOOL] [--key HEX]
         put APP_BIN (and CFG_TOOL, default: the base's) into BASE_IMAGE, rest stays byte-identical
"""
import json, struct, sys
from pathlib import Path

CODE_BASE = 0x1E00000  # app area address in the CPU map
BOOT_SCAN = 4096 + 32  # boot area bytes scanned for the boot file table: 32 B header + 4096 B of records


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc


def enc(data, key):
    out = bytearray(data)
    for i in range(len(out)):
        out[i] ^= key & 0xFF
        key = ((key << 1) ^ 0x1021 if key & 0x8000 else key << 1) & 0xFFFF
    return bytes(out)


def record(raw):
    if crc16(raw[2:]) != int.from_bytes(raw[:2], "little"):
        return None
    _, crc, off, size, attr, _, last, name = struct.unpack("<HHIIBBH16s", raw)
    return dict(name=name.split(b"\0")[0].decode("latin-1"), offset=off, size=size, attr=attr, crc=crc, last=last)


def table(buf, start, skip_first_last=False):
    out = []
    for a in range(start, min(len(buf), start + 4096), 32):
        r = record(buf[a:a + 32])
        if r is None:
            raise ValueError(f"bad table CRC at {a:#x}")
        out.append(r)
        if r["last"] and not (skip_first_last and a == start):
            return out
    raise ValueError("table has no terminator")


def find_key(block):
    # ponytail: 64k brute force over a 16-bit key, ~1 s; pass --key to skip
    for k in range(0x10000):
        r = record(enc(block, k))
        if r and r["name"] == "app_area_head":
            return k
    raise ValueError("no chip key decrypts app_area_head")


def ram_copies(app):
    """Find the SDK startup copy loops: mov r0,#dst ; mov r1,#src(flash) ; mov r2,#len  (q32s ffcN imm32)."""
    out = []
    for i in range(0, min(len(app), 0x200) - 18, 2):
        w = struct.unpack_from("<9H", app, i)
        if w[0] == 0xFFC0 and w[3] == 0xFFC1 and w[6] == 0xFFC2:
            dst, src, n = (w[1] | w[2] << 16), (w[4] | w[5] << 16), (w[7] | w[8] << 16)
            if CODE_BASE <= src < CODE_BASE + 0x1000000:
                out.append((dst, src, n))
    return out


def scramble(buf, key):
    """App-area scrambler; its own inverse. Block at offset o uses key ^ (o >> 2)."""
    return b"".join(enc(buf[o:o + 32], key ^ (o >> 2)) for o in range(0, len(buf), 32))


def boot_table(image):
    hdr = enc(image[:32], 0xFFFF)
    if crc16(hdr[2:]) != int.from_bytes(hdr[:2], "little"):
        raise ValueError("bad header CRC (not a jl_isd.bin?)")
    return table(bytes(enc(image[:BOOT_SCAN], 0xFFFF)[:32]) + b"".join(enc(image[a:a + 32], 0xFFFF) for a in range(32, BOOT_SCAN, 32)), 32)


def app_base(image):
    """Flash offset of the app area (the app_dir_head entry of the boot table)."""
    return next(r["offset"] for r in boot_table(image) if r["name"] == "app_dir_head")


def unpack(image, outdir, key=None):
    out = Path(outdir); out.mkdir(parents=True, exist_ok=True)
    boot = boot_table(image)
    base = app_base(image)
    for r in boot:
        if r["name"] in ("uboot.boot", "isd_config.ini"):
            data = image[r["offset"]:r["offset"] + r["size"]]
            if r["name"] == "uboot.boot":  # BANKCB: 16 B header + body, each ENC 0xFFFF
                data = enc(data[:16], 0xFFFF) + enc(data[16:], 0xFFFF)
            r["crc_ok"] = crc16(data) == r["crc"]
            (out / r["name"]).write_bytes(data)
    if key is None:
        key = find_key(image[base:base + 32])
    plain = scramble(image[base:], key)
    (out / "app_area.bin").write_bytes(plain)
    app = table(plain, 0, skip_first_last=True)
    for r in app:
        if r["attr"] & 0x1F == 2:  # file (0x10 = partition: VM, PRCT, BTIF)
            data = plain[r["offset"]:r["offset"] + r["size"]]
            r["crc_ok"] = crc16(data) == r["crc"]
            r["load_addr"] = hex(CODE_BASE + r["offset"])
            (out / r["name"]).write_bytes(data)
    app_bin = (out / "app.bin").read_bytes()
    copies = ram_copies(app_bin)  # .data and RAM-resident code, copied from flash at boot
    (out / "ram_copies.tsv").write_text("".join(f"{d:#x}\t{s:#x}\t{n:#x}\n" for d, s, n in copies))
    info = dict(chip=image[16:32].rstrip(b"\0\xff").decode(errors="replace"),
                chip_key=hex(key), app_base=hex(base), boot=boot, app=app,
                ram_copies=[dict(ram=hex(d), flash=hex(s), size=hex(n)) for d, s, n in copies])
    (out / "layout.json").write_text(json.dumps(info, indent=2))
    return info


def pack(base_image, app_bin, cfg_tool, key):
    """Return base_image with app.bin (and cfg_tool.bin unless None) replaced. Everything else is kept byte for byte."""
    base = app_base(base_image)
    plain = scramble(base_image[base:], key)
    recs = {r["name"]: (i, r) for i, r in enumerate(table(plain, 0, skip_first_last=True))}  # raises on a wrong key
    head, app, cfg, vm = (recs[n][1] for n in ("app_area_head", "app.bin", "cfg_tool.bin", "VM"))
    if cfg_tool is None:
        cfg_tool = plain[cfg["offset"]:cfg["offset"] + cfg["size"]]
    cfg_off = app["offset"] + len(app_bin)
    end = cfg_off + len(cfg_tool)
    # partition offsets are physical flash offsets (VM starts exactly at the end of these images)
    if base + end > min(vm["offset"], len(base_image)):
        raise ValueError(f"app + cfg_tool end at flash {base + end:#x}, past the VM partition at {vm['offset']:#x}")
    out = bytearray(plain)
    out[end:head["size"]] = b"\xff" * (head["size"] - end)  # space freed by a smaller app
    out[app["offset"]:cfg_off] = app_bin
    out[cfg_off:end] = cfg_tool
    for name, off, data in (("app.bin", app["offset"], app_bin), ("cfg_tool.bin", cfg_off, cfg_tool)):
        patch(out, recs[name][0], off, len(data), crc16(data))
    # the head's data CRC covers everything after its own record; its offset field stays as in the base
    patch(out, 0, head["offset"], end, crc16(out[32:end]))
    result = base_image[:base] + scramble(bytes(out), key)
    check(result, base_image, base, key)  # the output goes onto a real device: never return an image that does not parse
    return result


def check(image, base_image, base, key):
    if len(image) != len(base_image) or image[:base] != base_image[:base]:
        raise ValueError("self-check failed: length or boot area changed")
    plain = scramble(image[base:], key)
    recs = table(plain, 0, skip_first_last=True)  # raises on a bad record CRC
    for r in recs:
        if r["attr"] & 0x1F == 2 and crc16(plain[r["offset"]:r["offset"] + r["size"]]) != r["crc"]:
            raise ValueError(f"self-check failed: bad data CRC for {r['name']}")
    for name in ("app.bin", "cfg_tool.bin"):  # pack patches these by name, so verify them by name too
        r = next((r for r in recs if r["name"] == name), None)
        if r is None or crc16(plain[r["offset"]:r["offset"] + r["size"]]) != r["crc"]:
            raise ValueError(f"self-check failed: {name} missing or bad data CRC")
    if crc16(plain[32:recs[0]["size"]]) != recs[0]["crc"]:
        raise ValueError("self-check failed: bad data CRC for app_area_head")


def patch(buf, idx, offset, size, data_crc):
    a = idx * 32
    struct.pack_into("<HII", buf, a + 2, data_crc, offset, size)
    buf[a:a + 2] = crc16(buf[a + 2:a + 32]).to_bytes(2, "little")


def main_pack(argv):
    opt = lambda f: argv[argv.index(f) + 1] if f in argv else None
    pos = [a for i, a in enumerate(argv) if not a.startswith("--") and (i == 0 or not argv[i - 1].startswith("--"))]
    if len(pos) != 3:
        sys.exit(__doc__)
    base = Path(pos[0]).read_bytes()
    base_off = app_base(base)
    key = int(opt("--key"), 16) if opt("--key") else find_key(base[base_off:base_off + 32])
    cfg = Path(opt("--cfg")).read_bytes() if opt("--cfg") else None
    try:
        out = pack(base, Path(pos[1]).read_bytes(), cfg, key)
    except ValueError as e:
        sys.exit(f"jlimg pack: {e}")
    Path(pos[2]).write_bytes(out)  # only reached when pack succeeded


if __name__ == "__main__":
    if sys.argv[1:2] == ["pack"]:
        main_pack(sys.argv[2:]); sys.exit()
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    k = int(sys.argv[sys.argv.index("--key") + 1], 16) if "--key" in sys.argv else None
    info = unpack(Path(sys.argv[1]).read_bytes(), sys.argv[2], k)
    assert all(r.get("crc_ok", True) for r in info["boot"] + info["app"]), "CRC mismatch"
    print(json.dumps({k: info[k] for k in ("chip", "chip_key", "app_base")}),
          *(f"{r['name']:16} {r['offset']:#9x} {r['size']:#8x} {r.get('load_addr', '')}" for r in info["app"]), sep="\n")
