#!/usr/bin/env python3
"""Client for the E2's boot-area USB HID updater (the device after JUMPBOOT, 4c4a:4155, usage page 0xFF00).

  e2boot.py info                 region facts the updater reports (read-only)
  e2boot.py verify IMAGE         compare the device's app area with a jl_isd.bin, block by block (read-only)
  e2boot.py enter PORT --yes     normal mode -> update mode (sends JUMPBOOT on the serial port)
  e2boot.py flash IMAGE --yes    write IMAGE from the app area's start to its end (same steps as the vendor's
                                 UbootHid.exe). Refused when IMAGE's boot area differs from the device's.
  e2boot.py reboot --yes         leave update mode
  e2boot.py rescue IMAGE --yes   wait until the E2 shows up on USB in either mode, then flash IMAGE and reboot

The last four change the device and are refused without --yes: they need the owner's go-ahead (AGENTS.md).

Protocol (docs/update-path.md): 64-byte HID reports holding
  AA 55 | len u16 | cmd | status | payload | crc16      len = 2 + payload, crc over AA..payload
with cmd..payload XORed with MASK. The updater has no read command; it reports a CRC per block.
IMAGE is a vendor-format jl_isd.bin: it ends where the VM partition starts, so settings and pairing data stay.
Needs hidapi (brew install hidapi). No other dependency.
"""
import ctypes
import ctypes.util
import fcntl
import functools
import glob
import os
import pathlib
import subprocess
import sys
import termios
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))     # the Makefile runs python3 -I: no script directory
from jlimg import crc16

VID, PID = 0x4C4A, 0x4155
MASK = bytes.fromhex("4e61bc00")
INIT, ERASE, WRITE, CRC, REBOOT = 0xC0, 0xC2, 0xC3, 0xC4, 0xCA
ERASE_TYPE = {0x100: 1, 0x1000: 2, 0x10000: 3}     # erase size -> type field: boot code's table = vendor tool's values
WRITE_MAX = 41                                      # data bytes per write packet, as the vendor tool
CRC_MAX = 16                                        # blocks per CRC packet, as the vendor tool
HEAD = 0x20                                         # first bytes of the area, written last: they make it valid
WRITES = {"enter", "flash", "rescue", "reboot"}     # commands that change the device: refused without --yes


def mask(data):
    return bytes(b ^ MASK[i & 3] for i, b in enumerate(data))


def packet(cmd, payload=b"", status=0):
    head = b"\xaa\x55" + (len(payload) + 2).to_bytes(2, "little") + mask(bytes([cmd, status]) + payload)
    return head + crc16(head).to_bytes(2, "little")


def parse(raw):
    """-> (cmd, status, payload). Raises ValueError on a damaged packet."""
    if raw[:2] != b"\xaa\x55":
        raise ValueError("no packet marker: " + raw[:8].hex())
    end = 4 + int.from_bytes(raw[2:4], "little")
    if end + 2 > len(raw) or crc16(raw[:end]) != int.from_bytes(raw[end:end + 2], "little"):
        raise ValueError("bad packet CRC")
    body = mask(raw[4:end])
    return body[0], body[1], body[2:]


def u32(*values):
    return b"".join(v.to_bytes(4, "little") for v in values)


class Boot:
    """The updater's commands. `dev` has write(packet) and read() -> one reply packet."""

    def __init__(self, dev):
        self.dev = dev

    def cmd(self, cmd, payload=b""):
        self.dev.write(packet(cmd, payload))
        got, status, reply = parse(self.dev.read())
        if got != cmd:
            raise IOError(f"reply to {got:#x}, expected {cmd:#x}")
        if status:
            raise IOError(f"command {cmd:#x} failed, status {status}")
        return reply

    def init(self, name="app_dir_head"):
        r = self.cmd(INIT, name.encode().ljust(16, b"\0") + b"\0")
        addr, length, eoffset, align = (int.from_bytes(r[i:i + 4], "little") for i in range(0, 16, 4))
        return {"addr": addr, "len": length, "eoffset": eoffset, "align": align}

    def crcs(self, addr, count, align):
        out = []
        while len(out) < count:
            n = min(CRC_MAX, count - len(out))
            r = self.cmd(CRC, u32(addr + len(out) * align, n * align, align))
            out += [int.from_bytes(r[i:i + 2], "little") for i in range(0, 2 * n, 2)]
        return out

    def erase(self, addr, size):
        self.cmd(ERASE, u32(addr, ERASE_TYPE[size]))

    def write(self, addr, data):
        for o in range(0, len(data), WRITE_MAX):
            chunk = data[o:o + WRITE_MAX]
            self.cmd(WRITE, u32(addr + o, len(chunk)) + chunk)

    def reboot(self):
        self.dev.write(packet(REBOOT))              # the device resets; no reply


def blocks(image, info):
    """The part of the image the updater owns, padded to whole blocks."""
    data, align = image[info["addr"]:], info["align"]
    data += b"\xff" * (-len(data) % align)
    return [data[o:o + align] for o in range(0, len(data), align)]


def diff(boot, image, info):
    """-> (blocks of the image, indexes of the blocks that differ on the device)."""
    want = blocks(image, info)
    got = boot.crcs(info["eoffset"], len(want), info["align"])
    return want, [k for k, b in enumerate(want) if crc16(b) != got[k]]


def same_boot_area(boot, image, size):
    """True when the device's flash below the app area (boot table, boot code) equals the image's. Selects uboot_zone."""
    zone = boot.init("uboot_zone")
    align = zone["align"]
    got = boot.crcs(zone["eoffset"], size // align, align)
    return size % align == 0 and all(crc16(image[k * align:(k + 1) * align]) == c for k, c in enumerate(got))


def flash(boot, image, log=print):
    """Write the differing blocks. Block 0 is erased first and its first HEAD bytes are written last,
    so an interrupted run leaves an area the boot code rejects (it then stays in update mode)."""
    if not same_boot_area(boot, image, boot.init()["addr"]):
        raise IOError("the image's boot area differs from the device's: not an image for this unit, nothing written")
    info = boot.init()                              # select the app area again
    align = info["align"]
    if align not in ERASE_TYPE:                     # one erase command per block: the block size must be an erase size
        raise IOError(f"unexpected block size {align:#x}")
    want, bad = diff(boot, image, info)
    if not bad:
        log("device already holds this image")
        return
    log(f"{len(bad)} of {len(want)} blocks differ")
    base = info["eoffset"]
    boot.erase(base, align)
    for k in bad:
        if k:
            boot.erase(base + k * align, align)
            boot.write(base + k * align, want[k])
    boot.write(base + HEAD, want[0][HEAD:])
    boot.write(base, want[0][:HEAD])
    if diff(boot, image, info)[1]:
        raise IOError("verify failed after writing; the device is still in update mode, run flash again")
    log("written and verified")


@functools.lru_cache(maxsize=None)
def hidapi():
    lib = ctypes.CDLL(ctypes.util.find_library("hidapi") or "/opt/homebrew/lib/libhidapi.dylib")
    lib.hid_open.restype = ctypes.c_void_p
    lib.hid_open.argtypes = [ctypes.c_ushort, ctypes.c_ushort, ctypes.c_wchar_p]
    lib.hid_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.hid_read_timeout.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]
    lib.hid_init()
    return lib


class Hid:
    def __init__(self):
        self.lib = hidapi()
        self.h = self.lib.hid_open(VID, PID, None)
        if not self.h:
            raise IOError("no E2 in update mode (4c4a:4155 HID). Run: e2boot.py enter PORT --yes")

    def write(self, pkt):
        if self.lib.hid_write(self.h, b"\0" + pkt.ljust(64, b"\0"), 65) < 0:   # leading 0: no report ID
            raise IOError("HID write failed")

    def read(self):
        buf = ctypes.create_string_buffer(64)
        n = self.lib.hid_read_timeout(self.h, buf, 64, 3000)
        if n <= 0:
            raise IOError("no reply from the updater")
        return buf.raw[:n]


def enter(port):
    """Send JUMPBOOT to the normal-mode firmware. The first command after opening can be dropped: send gets first."""
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attr = termios.tcgetattr(fd)
        attr[0] = attr[1] = attr[3] = 0
        attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        termios.tcsetattr(fd, termios.TCSANOW, attr)
        fcntl.ioctl(fd, 0x80045402, (460800).to_bytes(4, sys.byteorder))   # IOSSIOSPEED (macOS)
        for msg in (b'{"o":"get","t":"model"}', b'{"o":"get","t":"model"}', b'{"o":"JUMPBOOT"}'):
            os.write(fd, msg)
            time.sleep(0.4)
    finally:
        os.close(fd)


def e2_on_usb():
    """True when a device with the E2's USB IDs is on the bus, in either mode (macOS)."""
    out = subprocess.run(["ioreg", "-p", "IOUSB", "-l", "-w0"], capture_output=True, text=True).stdout
    return f'"idVendor" = {VID}' in out and f'"idProduct" = {PID}' in out


def wait_for_updater():
    """Block until the HID updater is there; a unit that shows up in normal mode is sent JUMPBOOT at once.
    Serial ports are only written to while the E2's USB IDs are on the bus; another modem plugged in at the
    same time as the E2 would get the bytes too."""
    print("waiting for the E2 on USB (Ctrl-C to stop)")
    while True:
        ports = glob.glob("/dev/cu.usbmodem*")
        for port in ports if ports and e2_on_usb() else ():
            print(time.strftime("%T"), "serial port", port, "-> update command")
            try:
                enter(port)
            except OSError as e:               # the port can vanish again at once
                print("  ", e)
        try:
            return Hid()
        except IOError:
            time.sleep(0.5)


def main(argv):
    sys.stdout.reconfigure(line_buffering=True)     # show progress at once, also through make or a pipe
    op, args = (argv[1] if len(argv) > 1 else ""), [a for a in argv[2:] if a != "--yes"]
    if op not in WRITES | {"info", "verify"} or (op in ("enter", "verify", "flash", "rescue") and not args):
        sys.exit(__doc__)
    if op in WRITES and "--yes" not in argv:
        sys.exit(op + " changes the device: it needs the owner's go-ahead (AGENTS.md). Then add --yes")
    if op == "enter":
        return enter(args[0])
    image = open(args[0], "rb").read() if args else None        # before any waiting: a missing file fails now
    boot = Boot(wait_for_updater() if op == "rescue" else Hid())
    if op == "info":
        for name in ("app_dir_head", "uboot_zone"):
            print(name, {k: hex(v) for k, v in boot.init(name).items()})
    elif op == "verify":
        want, bad = diff(boot, image, boot.init())
        print(f"{len(want) - len(bad)} of {len(want)} blocks match" + (f"; first differing: {bad[:8]}" if bad else ""))
    elif op in ("flash", "rescue"):
        flash(boot, image)
    if op in ("reboot", "rescue"):
        boot.reboot()


if __name__ == "__main__":
    main(sys.argv)
