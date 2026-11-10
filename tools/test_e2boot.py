#!/usr/bin/env python3
"""Tests for e2boot.py against a model of the boot code's updater. No device, no images needed."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import e2boot
from e2boot import Boot, flash, packet, parse, u32
from jlimg import crc16

ADDR, ALIGN = 0x3000, 0x1000        # as the E2 reports them (docs/update-path.md)
REGIONS = {b"app_dir_head": ADDR, b"uboot_zone": 0}     # name -> start in flash
ERASE_SIZE = {1: 0x100, 2: 0x1000, 3: 0x10000}   # the boot code's table; the vendor tool sends the same types


class FakeDevice:
    """Flash + the updater's command handlers, as read from the boot code. Like the E2, it reports base offset 0:
    command addresses count from the start of the selected region (ADDR)."""

    def __init__(self, flash_bytes, fail_after=None):
        self.flash, self.reply, self.writes, self.erases = bytearray(flash_bytes), None, [], []
        self.base = 0                    # start of the region selected by the last init
        self.fail_after = fail_after     # cable pulled after this many erase/write commands

    def write(self, pkt):
        cmd, _, p = parse(pkt)
        n = lambda i: int.from_bytes(p[i:i + 4], "little")
        status, out = 0, b""
        if cmd in (e2boot.ERASE, e2boot.WRITE):
            if self.fail_after == 0:
                raise IOError("cable pulled")
            if self.fail_after:
                self.fail_after -= 1
        if cmd == e2boot.INIT:
            name = p[:16].rstrip(b"\0")
            if name in REGIONS:
                self.base = REGIONS[name]
                out = u32(self.base, 0xFFFFFFFF, 0, ALIGN)
            else:
                status, out = 4, b"\0" * 16
        at = self.base + n(0)
        if cmd == e2boot.CRC:
            assert n(8) == ALIGN and n(4) // ALIGN <= e2boot.CRC_MAX
            out = b"".join(crc16(self.flash[a:a + ALIGN]).to_bytes(2, "little") for a in range(at, at + n(4), ALIGN))
        elif cmd == e2boot.ERASE:
            assert ERASE_SIZE[n(4)] == ALIGN and n(0) % ALIGN == 0   # the E2 erases one block: type 2, 4 KB
            self.flash[at:at + ALIGN] = b"\xff" * ALIGN
            self.erases.append(at)
        elif cmd == e2boot.WRITE:
            data = p[8:]
            assert len(data) == n(4) <= e2boot.WRITE_MAX
            for i, b in enumerate(data):                     # NOR flash: a write can only clear bits
                self.flash[at + i] &= b
            self.writes.append(at)
        self.reply = packet(cmd, out, status)

    def read(self):
        return self.reply


def quiet(*_):
    pass


def raised(exc, fn, *args, **kw):
    """The exception fn raises; fails when it raises none."""
    try:
        fn(*args, **kw)
    except exc as e:
        return e
    raise AssertionError(f"{fn.__name__} did not raise {exc.__name__}")


def test_packet_round_trip():
    pkt = packet(0xC4, b"\x01\x02\x03", 7)
    assert pkt[:4] == b"\xaa\x55\x05\x00" and parse(pkt) == (0xC4, 7, b"\x01\x02\x03")
    assert pkt[4] == 0xC4 ^ 0x4E and pkt[5] == 7 ^ 0x61     # the mask starts at the command byte


def test_damaged_packet_is_rejected():
    pkt = bytearray(packet(0xC0, b"abc"))
    pkt[6] ^= 1
    for bad in (bytes(pkt), b"\x00" * 64):
        raised(ValueError, parse, bad)


def test_failed_command_raises():
    raised(IOError, Boot(FakeDevice(b"")).init, "no_such_region")


BOOT = bytes((i * 3 + (i >> 8)) & 0xFF for i in range(ADDR))     # boot area: the same on the device and in its images


def image(seed, size=ADDR + 20 * ALIGN + 10):       # more blocks than one CRC packet holds (CRC_MAX)
    return BOOT + bytes((i * seed + (i >> 8)) & 0xFF for i in range(ADDR, size))


def device(image_bytes, fail_after=None):
    return FakeDevice(image_bytes + b"\xff" * ALIGN, fail_after)    # erased flash after the image


def test_flash_writes_the_image_and_the_head_last():
    old, new = image(7), image(11)
    dev = device(old)
    flash(Boot(dev), new, log=quiet)
    end = len(new) + (-(len(new) - ADDR) % ALIGN)
    assert dev.flash[ADDR:len(new)] == new[ADDR:] and dev.flash[len(new):end] == b"\xff" * (end - len(new))
    assert dev.flash[:ADDR] == old[:ADDR], "the boot area was touched"
    assert dev.writes[-1] == ADDR and ADDR not in dev.writes[:-1], "the head must be the last write"
    assert dev.erases[0] == ADDR, "block 0 must be erased before anything is written"


def test_interrupted_flash_leaves_no_valid_head():
    old, new = image(7), image(11)
    for commands in (1, 5, 400):                    # stopped after the first erase, early, and late in the run
        dev = device(old, fail_after=commands)
        raised(IOError, flash, Boot(dev), new, log=quiet)
        assert dev.flash[ADDR:ADDR + e2boot.HEAD] == b"\xff" * e2boot.HEAD, f"head present after {commands} commands"


def test_image_with_another_boot_area_is_refused():
    other = bytearray(image(11))
    other[0x1234] ^= 1                              # e.g. an image built for another chip
    dev = device(image(7))
    raised(IOError, flash, Boot(dev), bytes(other), log=quiet)
    assert not dev.writes and not dev.erases


def test_commands_that_change_the_device_need_yes():
    for op in e2boot.WRITES:                        # refused before the image, the port or the device is touched
        assert "--yes" in str(raised(SystemExit, e2boot.main, ["e2boot.py", op, "no-such-file"]))


def test_flash_skips_equal_blocks():
    old = image(7)
    new = bytearray(old)
    new[ADDR + 3 * ALIGN + 5] ^= 0xFF
    dev = device(old)
    flash(Boot(dev), bytes(new), log=quiet)
    assert dev.flash[:len(new)] == new
    assert {w // ALIGN * ALIGN for w in dev.writes} == {ADDR, ADDR + 3 * ALIGN}   # block 0 is always rewritten


def test_flash_does_nothing_when_equal():
    dev = device(image(7))
    flash(Boot(dev), image(7), log=quiet)
    assert not dev.writes


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
