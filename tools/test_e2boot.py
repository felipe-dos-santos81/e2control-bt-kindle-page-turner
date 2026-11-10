#!/usr/bin/env python3
"""Tests for e2boot.py against a model of the boot code's updater. No device, no images needed."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import e2boot
from e2boot import Boot, flash, packet, parse, u32
from jlimg import crc16

ADDR, ALIGN = 0x3000, 0x1000        # as the E2 reports them (docs/update-path.md)
ERASE_SIZE = {1: 0x100, 2: 0x1000, 3: 0x10000}   # the boot code's table; the vendor tool sends the same types


class FakeDevice:
    """Flash + the updater's command handlers, as read from the boot code. Like the E2, it reports base offset 0:
    command addresses count from the start of the selected region (ADDR)."""

    def __init__(self, flash_bytes):
        self.flash, self.reply, self.writes = bytearray(flash_bytes), None, []

    def write(self, pkt):
        cmd, _, p = parse(pkt)
        n = lambda i: int.from_bytes(p[i:i + 4], "little")
        at = ADDR + n(0)
        status, out = 0, b""
        if cmd == e2boot.INIT:
            out = u32(ADDR, 0xFFFFFFFF, 0, ALIGN) if p[:13] == b"app_dir_head\0" else b"\0" * 16
            status = 0 if out.strip(b"\0") else 4
        elif cmd == e2boot.CRC:
            assert n(8) == ALIGN and n(4) // ALIGN <= e2boot.CRC_MAX
            out = b"".join(crc16(self.flash[a:a + ALIGN]).to_bytes(2, "little") for a in range(at, at + n(4), ALIGN))
        elif cmd == e2boot.ERASE:
            assert ERASE_SIZE[n(4)] == ALIGN and n(0) % ALIGN == 0   # the E2 erases one block: type 2, 4 KB
            self.flash[at:at + ALIGN] = b"\xff" * ALIGN
        elif cmd == e2boot.WRITE:
            data = p[8:]
            assert len(data) == n(4) <= e2boot.WRITE_MAX
            for i, b in enumerate(data):                     # NOR flash: a write can only clear bits
                self.flash[at + i] &= b
            self.writes.append(at)
        self.reply = packet(cmd, out, status)

    def read(self):
        return self.reply


def test_packet_round_trip():
    pkt = packet(0xC4, b"\x01\x02\x03", 7)
    assert pkt[:4] == b"\xaa\x55\x05\x00" and parse(pkt) == (0xC4, 7, b"\x01\x02\x03")
    assert pkt[4] == 0xC4 ^ 0x4E and pkt[5] == 7 ^ 0x61     # the mask starts at the command byte


def test_damaged_packet_is_rejected():
    pkt = bytearray(packet(0xC0, b"abc"))
    pkt[6] ^= 1
    for bad in (bytes(pkt), b"\x00" * 64):
        try:
            parse(bad)
        except ValueError:
            continue
        raise AssertionError("accepted a damaged packet")


def test_failed_command_raises():
    try:
        Boot(FakeDevice(b"")).init("no_such_region")
    except IOError:
        return
    raise AssertionError("status 4 was ignored")


def image(seed, size=0x3000 + 5 * ALIGN + 10):
    return bytes((i * seed + (i >> 8)) & 0xFF for i in range(size))


def device(image_bytes):
    return FakeDevice(image_bytes + b"\xff" * ALIGN)        # erased flash after the image


def test_flash_writes_the_image_and_the_head_last():
    old, new = image(7), image(11)
    dev = device(old)
    flash(Boot(dev), new, log=lambda s: None)
    end = len(new) + (-(len(new) - ADDR) % ALIGN)
    assert dev.flash[ADDR:len(new)] == new[ADDR:] and dev.flash[len(new):end] == b"\xff" * (end - len(new))
    assert dev.flash[:ADDR] == old[:ADDR], "the boot area was touched"
    assert dev.writes[-1] == ADDR and ADDR not in dev.writes[:-1], "the head must be the last write"


def test_flash_skips_equal_blocks():
    old = image(7)
    new = bytearray(old)
    new[ADDR + 3 * ALIGN + 5] ^= 0xFF
    dev = device(old)
    flash(Boot(dev), bytes(new), log=lambda s: None)
    assert dev.flash[:len(new)] == new
    assert {w // ALIGN * ALIGN for w in dev.writes} == {ADDR, ADDR + 3 * ALIGN}   # block 0 is always rewritten


def test_flash_does_nothing_when_equal():
    dev = device(image(7))
    flash(Boot(dev), image(7), log=lambda s: None)
    assert not dev.writes


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
