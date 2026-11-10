import pathlib, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import jlimg
FW = pathlib.Path(__file__).parent.parent / "firmware"
IMG = FW / "e2control-zoom-phone-tablet.bin"

def parts(image):
    """Unpack, assert every CRC (incl. the app_area_head data CRC, which unpack does not check), return (app, cfg)."""
    with tempfile.TemporaryDirectory() as d:
        info = jlimg.unpack(image, d)
        assert all(r.get("crc_ok", True) for r in info["boot"] + info["app"]), "CRC mismatch"
        assert {r["name"] for r in info["app"] if "crc_ok" in r} >= {"app.bin", "cfg_tool.bin"}, "app.bin/cfg_tool.bin not CRC-checked"
        head, plain = info["app"][0], pathlib.Path(d, "app_area.bin").read_bytes()
        assert head["name"] == "app_area_head" and jlimg.crc16(plain[32:head["size"]]) == head["crc"], "head data CRC"
        return (pathlib.Path(d, "app.bin").read_bytes(), pathlib.Path(d, "cfg_tool.bin").read_bytes())

def test_roundtrip_is_byte_identical():
    base = IMG.read_bytes(); app, cfg = parts(base)
    assert jlimg.pack(base, app, cfg, 0x284C) == base

def test_roundtrip_all_images():
    for f in sorted(FW.glob("*.bin")):
        base = f.read_bytes(); app, cfg = parts(base)
        assert jlimg.pack(base, app, cfg, 0x284C) == base, f.name

def test_new_app_unpacks_and_keeps_boot_area():
    base = IMG.read_bytes(); app, cfg = parts(base)
    new_app = app[:0x1000] + bytes(len(app) - 0x1000 - 64)          # shorter, different
    out = jlimg.pack(base, new_app, None, 0x284C)
    assert len(out) == len(base) and out[:0x3000] == base[:0x3000]
    old_end = 0x3000 + 0x120 + len(app) + len(cfg)
    assert out[old_end:] == base[old_end:]                           # beyond the old app area (VM starts at len(base)) untouched
    got_app, got_cfg = parts(out)                                    # parts() asserts every CRC, head included
    assert got_app == new_app and got_cfg == cfg

def test_app_too_large_is_refused():
    base = IMG.read_bytes()
    try:
        jlimg.pack(base, bytes(0x2D000), None, 0x284C)               # reaches the VM partition
    except ValueError as e:
        assert "VM" in str(e)
    else:
        raise AssertionError("oversized app accepted")

def test_size_limit_boundary():
    base = IMG.read_bytes(); app, cfg = parts(base)
    room = 0x2D000 - 0x3000 - 0x120 - len(cfg)                       # app + cfg_tool may end exactly at VM
    out = jlimg.pack(base, bytes(room), None, 0x284C)
    assert parts(out)[0] == bytes(room)
    try:
        jlimg.pack(base, bytes(room + 1), None, 0x284C)
    except ValueError as e:
        assert "VM" in str(e)
    else:
        raise AssertionError("one byte over the VM limit accepted")

def test_check_rejects_corrupt_app_bin():
    base = IMG.read_bytes(); app, cfg = parts(base)
    out = bytearray(jlimg.pack(base, app, None, 0x284C))
    jlimg.check(bytes(out), base, 0x3000, 0x284C)                    # intact image passes
    out[0x3000 + 0x120 + 0x100] ^= 1                                 # one bit inside app.bin (scrambling is bytewise, so it maps 1:1)
    try:
        jlimg.check(bytes(out), base, 0x3000, 0x284C)
    except ValueError as e:
        assert "app.bin" in str(e)
    else:
        raise AssertionError("corrupt app.bin passed check()")

def test_full_device_dump_tail_untouched():
    import random
    base = IMG.read_bytes(); app, cfg = parts(base)
    tail = random.Random(1).randbytes(0x13000)                       # stands for VM, BTIF, key_mac
    dump = base + tail
    assert jlimg.pack(dump, app, cfg, 0x284C) == dump
    out = jlimg.pack(dump, app[:-64], None, 0x284C)
    assert len(out) == len(dump) and out[len(base):] == tail
    parts(out)

def test_cli_error_exits_cleanly_and_writes_nothing():
    import subprocess
    with tempfile.TemporaryDirectory() as d:
        big = pathlib.Path(d, "big.bin"); big.write_bytes(bytes(0x2D000))
        out = pathlib.Path(d, "out.bin")
        r = subprocess.run([sys.executable, "-I", str(pathlib.Path(__file__).parent / "jlimg.py"), "pack", str(IMG), str(big), str(out)],
                           capture_output=True, text=True)
        assert r.returncode != 0 and "VM" in r.stderr and "Traceback" not in r.stderr and not out.exists()

if __name__ == "__main__":
    for n, f in list(globals().items()):
        if n.startswith("test_"): f(); print("ok", n)
