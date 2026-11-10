import pathlib, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import jlimg
FW = pathlib.Path(__file__).parent.parent / "firmware"
IMG = FW / "e2control-zoom-phone-tablet.bin"

def parts(image):
    with tempfile.TemporaryDirectory() as d:
        jlimg.unpack(image, d)
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
    got_app, got_cfg = parts(out)                                    # unpack re-checks every CRC
    assert got_app == new_app and got_cfg == cfg

def test_app_too_large_is_refused():
    base = IMG.read_bytes()
    try:
        jlimg.pack(base, bytes(0x2D000), None, 0x284C)               # reaches the VM partition
    except ValueError as e:
        assert "VM" in str(e)
    else:
        raise AssertionError("oversized app accepted")

if __name__ == "__main__":
    for n, f in list(globals().items()):
        if n.startswith("test_"): f(); print("ok", n)
