#!/usr/bin/env python3
# Copyright 2026 Dimitrix LLC
# SPDX-License-Identifier: GPL-2.0-or-later
"""Flash the mini_mighty dongle's nRF52805, and read its RTT log, over the dongle's own USB.

The dongle's AVR bridges SWD to the nRF over QMK raw HID (keyboards/mini_mighty/dongle/
swd_bridge.c, whose header comment is the protocol). Dongle PCB v7 and later only: on
earlier boards `info` says the wiring isn't there, and you need a J-Link on J1.

    mm_swd.py info                    what the dongle and its nRF report; no button needed
    mm_swd.py flash dongle.hex        page-erase what the image covers, program, verify
    mm_swd.py verify dongle.hex
    mm_swd.py rtt [--elf dongle.elf]  restart the nRF and print its RTT log until Ctrl-C
    mm_swd.py read ADDR [WORDS]       hex dump of nRF memory
    mm_swd.py reset                   pin-reset the nRF

Everything but `info` asks for a press of the dongle's button: the AVR won't open the nRF's
memory to the host without one. An nRF that stays locked (a new module, or firmware that
doesn't release APPROTECT) can only be erased whole; `flash` offers to do that, and then
programs it.

`flash` never touches the pairing pages [0x2E000, 0x30000), so pairings survive an update,
unless you ask for --wipe-pairings. After any erase-all it writes UICR itself:
APPROTECT = HwDisabled (0x5A), which the firmware needs in order to release APPROTECT at
boot, and PSELRESET = P0.21, so the AVR's reset line works.

Needs a hidapi binding: `pip install hidapi`, or `pip install hid` plus the hidapi library
(`brew install hidapi` on macOS). On Linux the raw HID node needs a udev rule, as for
`qmk console`.
"""

import argparse
import contextlib
import struct
import sys
import time
import zlib

try:
    import hid
except ImportError as e:
    sys.exit(f'mm_swd: no usable hidapi binding ({e}).\n'
             'Install one: pip install hidapi, or pip install hid plus the hidapi library (brew install hidapi).')

DONGLE_VID = 0x04D8
DONGLE_PID = 0xFFF2  # TEMP, as in dongle/keyboard.json
RAW_USAGE_PAGE = 0xFF60
RAW_USAGE = 0x61
REPORT_LEN = 32
PROTOCOL_VERSION = 1

# Commands, statuses, gate states and INFO flags, as in swd_bridge.c.
CMD_INFO, CMD_CONNECT, CMD_UNLOCK, CMD_RELEASE, CMD_MEM_READ, CMD_MEM_WRITE, CMD_MEM_CRC32, \
    CMD_ERASEALL, CMD_RESET, CMD_AP_READ, CMD_AP_WRITE = range(0xB0, 0xBB)

ST_OK, ST_PENDING, ST_DENIED, ST_BUSY, ST_BAD_ARGS, ST_NO_WIRING, ST_NOT_CONNECTED, ST_TIMEOUT, ST_LOCKED = range(9)
ST_SWD = 0x10
STATUS_TEXT = {
    ST_OK: 'ok',
    ST_PENDING: 'pending',
    ST_DENIED: 'not allowed until the dongle is unlocked',
    ST_BUSY: 'busy waiting for the button',
    ST_BAD_ARGS: 'bad arguments',
    ST_NO_WIRING: 'this dongle has no SWD wiring to its AVR (PCB v6 or earlier); use a J-Link on J1',
    ST_NOT_CONNECTED: 'not connected',
    ST_TIMEOUT: 'timed out (no button press within 30 s, or the erase never finished)',
    ST_LOCKED: "the nRF's debug port is locked",
    ST_SWD + 1: 'SWD: the nRF kept answering WAIT',
    ST_SWD + 2: 'SWD: the nRF answered FAULT',
    ST_SWD + 3: 'SWD: no answer from the nRF',
    ST_SWD + 4: 'SWD: parity error',
}

GATE_CLOSED, GATE_WAIT_PRESS, GATE_WAIT_RELEASE, GATE_ERASE_ONLY, GATE_OPEN = range(5)
GATE_TEXT = ['closed', 'waiting for a press', 'waiting for the release', 'erase only (nRF locked)', 'open']

INFO_WIRED, INFO_CONNECTED, INFO_TOUCHED, INFO_NO_BUTTON = 0x01, 0x02, 0x04, 0x08

MEM_READ_MAX, MEM_WRITE_MAX, CRC_MAX_WORDS = 7, 6, 256

# nRF52805
FLASH_SIZE = 0x30000
PAGE_SIZE = 0x1000
PAIRING_PAGES = range(0x2E000, 0x30000)  # mm_pair A/B, owned by the firmware at run time
RAM_START, RAM_END = 0x20000000, 0x20006000
UICR_START, UICR_END = 0x10001000, 0x10002000
UICR_PSELRESET0, UICR_PSELRESET1, UICR_APPROTECT = 0x10001200, 0x10001204, 0x10001208
RESET_PIN = 21  # P0.21, wired to the AVR's PD4
APPROTECT_HWDISABLED = 0x5A
FICR_INFO_PART = 0x10000100
NVMC_READY, NVMC_CONFIG, NVMC_ERASEPAGE = 0x4001E400, 0x4001E504, 0x4001E508
NVMC_REN, NVMC_WEN, NVMC_EEN = 0, 1, 2
DHCSR = 0xE000EDF0
DHCSR_DBGKEY = 0xA05F0000
DHCSR_HALT = DHCSR_DBGKEY | 0x3  # C_HALT | C_DEBUGEN
DHCSR_S_HALT = 1 << 17
DEMCR = 0xE000EDFC
AIRCR = 0xE000ED0C
AIRCR_SYSRESETREQ = 0x05FA0004  # VECTKEY | SYSRESETREQ
CTRL_AP, CTRL_AP_RESET = 1, 0x000


class BridgeError(Exception):
    def __init__(self, cmd, status):
        super().__init__(STATUS_TEXT.get(status, f'status 0x{status:02X}'))
        self.cmd, self.status = cmd, status


class Bridge:
    """One dongle's SWD bridge, over whichever hidapi binding is installed."""

    def __init__(self, path):
        if hasattr(hid, 'Device'):  # `hid` on PyPI (ctypes)
            dev = hid.Device(path=path)
            self._write, self._read = dev.write, lambda n, ms: dev.read(n, ms)
        else:  # `hidapi` on PyPI (cython)
            dev = hid.device()
            dev.open_path(path)
            self._write, self._read = dev.write, lambda n, ms: bytes(dev.read(n, ms))
        self._close = dev.close
        self._seq = 0

    def close(self):
        self._close()

    def cmd(self, cmd, payload=b'', timeout=5.0):
        """Sends one command and returns (status, reply)."""
        self._seq = (self._seq + 1) & 0xFF
        self._write(b'\0' + bytes([cmd, self._seq]) + payload.ljust(REPORT_LEN - 2, b'\0'))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            reply = self._read(REPORT_LEN, 100)
            if not reply:
                continue
            if reply[0] == 0xFF:
                sys.exit('mm_swd: the dongle answers raw HID but not the SWD bridge (older firmware, or a console build).')
            if reply[0] == cmd and reply[1] == self._seq:
                return reply[2], reply
        raise TimeoutError(f'no reply from the dongle to command 0x{cmd:02X}')

    def check(self, cmd, payload=b'', timeout=5.0):
        status, reply = self.cmd(cmd, payload, timeout)
        if status != ST_OK:
            raise BridgeError(cmd, status)
        return reply

    def info(self):
        r = self.check(CMD_INFO)
        return r[3], r[4], r[5], r[6]  # version, gate, flags, last result

    def connect(self):
        """Returns (DPIDR, whether the AHB-AP answers)."""
        r = self.check(CMD_CONNECT)
        return struct.unpack_from('<I', r, 3)[0], bool(r[7])

    def read_words(self, addr, n):
        out = []
        while n:
            k = min(n, MEM_READ_MAX)
            r = self.check(CMD_MEM_READ, struct.pack('<IB', addr, k))
            out += struct.unpack_from(f'<{k}I', r, 4)
            addr, n = addr + 4 * k, n - k
        return out

    def read32(self, addr):
        return self.read_words(addr, 1)[0]

    def read_bytes(self, addr, n):
        start, end = addr & ~3, (addr + n + 3) & ~3
        data = struct.pack(f'<{(end - start) // 4}I', *self.read_words(start, (end - start) // 4))
        return data[addr - start:addr - start + n]

    def write_words(self, addr, words):
        for i in range(0, len(words), MEM_WRITE_MAX):
            chunk = words[i:i + MEM_WRITE_MAX]
            self.check(CMD_MEM_WRITE, struct.pack(f'<IB{len(chunk)}I', addr + 4 * i, len(chunk), *chunk))

    def write32(self, addr, value):
        self.write_words(addr, [value])

    def crc32(self, addr, nbytes):
        """zlib.crc32() of nbytes (a multiple of 4) of nRF memory, computed on the AVR."""
        crc, words = 0, nbytes // 4
        while words:
            k = min(words, CRC_MAX_WORDS)
            r = self.check(CMD_MEM_CRC32, struct.pack('<IHI', addr, k, crc))
            crc = struct.unpack_from('<I', r, 3)[0]
            addr, words = addr + 4 * k, words - k
        return crc

    def ap_write(self, ap, reg, value):
        self.check(CMD_AP_WRITE, struct.pack('<BBI', ap, reg, value))

    def unlock(self):
        """Opens the gate. Returns 'open', or 'locked' when the nRF can only be erased."""
        status, _ = self.cmd(CMD_UNLOCK)
        if status == ST_OK:
            return 'open'
        if status == ST_LOCKED:
            return 'locked'
        if status != ST_PENDING:
            raise BridgeError(CMD_UNLOCK, status)
        print("Press and release the dongle's button (within 30 s)...", flush=True)
        while True:
            time.sleep(0.1)
            _, gate, _, result = self.info()
            if gate == GATE_OPEN:
                print('Unlocked.')
                return 'open'
            if gate == GATE_CLOSED:
                raise BridgeError(CMD_UNLOCK, result)

    def release(self):
        try:
            self.cmd(CMD_RELEASE)
        except (BridgeError, TimeoutError, OSError):
            pass


def find_dongle(args):
    found = [d for d in hid.enumerate(args.vid, args.pid) if d.get('usage_page') == RAW_USAGE_PAGE and d.get('usage') == RAW_USAGE]
    if not found:
        sys.exit(f'mm_swd: no dongle found (VID {args.vid:04X}, PID {args.pid:04X}, raw HID). '
                 'A dongle built with CONSOLE_ENABLE has no bridge.')
    if len(found) > 1:
        sys.exit('mm_swd: more than one dongle is plugged in; unplug all but one.')
    bridge = Bridge(found[0]['path'])
    version, _, flags, _ = bridge.info()
    if version != PROTOCOL_VERSION:
        sys.exit(f'mm_swd: the dongle speaks bridge protocol {version}, this tool {PROTOCOL_VERSION}.')
    if flags & INFO_NO_BUTTON:
        print('Note: bench firmware (MM_SWD_BRIDGE_NO_BUTTON); the button is not required.')
    return bridge


@contextlib.contextmanager
def session(bridge, allow_locked=False):
    """Unlocks, and always releases at the end, which pin-resets an nRF the session touched."""
    try:
        state = bridge.unlock()
        if state == 'locked' and not allow_locked:
            sys.exit("mm_swd: the nRF is locked (a new module, or firmware that doesn't release APPROTECT). "
                     'Only `flash` can do anything with it, by erasing it whole.')
        yield state
    finally:
        bridge.release()


def confirm(question, assume_yes):
    if assume_yes:
        return True
    return input(f'{question} [y/N] ').strip().lower() in ('y', 'yes')


def load_hex(path):
    """Intel HEX -> {address: byte}."""
    mem, base = {}, 0
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if line[0] != ':':
                sys.exit(f'mm_swd: {path}:{lineno}: not an Intel HEX record')
            rec = bytes.fromhex(line[1:])
            if sum(rec) & 0xFF:
                sys.exit(f'mm_swd: {path}:{lineno}: bad checksum')
            n, addr, kind, data = rec[0], rec[1] << 8 | rec[2], rec[3], rec[4:4 + rec[0]]
            if kind == 0:
                for i, b in enumerate(data):
                    mem[base + addr + i] = b
            elif kind == 1:
                break
            elif kind == 2:
                base = (data[0] << 8 | data[1]) << 4
            elif kind == 4:
                base = (data[0] << 8 | data[1]) << 16
    return mem


def check_image(mem):
    if not mem:
        sys.exit('mm_swd: the image is empty.')
    if any(a in PAIRING_PAGES for a in mem):
        sys.exit('mm_swd: the image writes the pairing pages [0x2E000, 0x30000), which the firmware owns. Refusing.')
    if any(UICR_START <= a < UICR_END for a in mem):
        sys.exit('mm_swd: the image writes UICR. This tool manages UICR itself; build the image without it.')
    if any(a >= FLASH_SIZE for a in mem):
        sys.exit(f'mm_swd: the image has data past the end of flash (0x{max(mem):X}).')
    if any(a not in mem for a in range(8)):
        sys.exit('mm_swd: the image has no vector table at 0x0. The dongle runs without a SoftDevice; '
                 'is this a keyboard or mouse image?')


def pages_of(mem):
    return sorted({a & ~(PAGE_SIZE - 1) for a in mem})


def page_bytes(mem, page):
    return bytes(mem.get(page + i, 0xFF) for i in range(PAGE_SIZE))


def nvmc_wait(bridge, timeout=1.0):
    deadline = time.monotonic() + timeout
    while not bridge.read32(NVMC_READY) & 1:
        if time.monotonic() > deadline:
            sys.exit('mm_swd: the NVMC never came ready.')
        time.sleep(0.005)


def prepare(bridge):
    """Connects, makes sure the AHB-AP answers, and halts the core."""
    _, open_ = bridge.connect()
    if not open_:
        # After an erase-all some parts want a CTRL-AP reset before the AHB-AP opens (pyOCD
        # does this after every mass erase).
        bridge.ap_write(CTRL_AP, CTRL_AP_RESET, 1)
        bridge.ap_write(CTRL_AP, CTRL_AP_RESET, 0)
        _, open_ = bridge.connect()
        if not open_:
            sys.exit("mm_swd: the nRF's memory port is still locked.")
    bridge.write32(DHCSR, DHCSR_HALT)
    if not bridge.read32(DHCSR) & DHCSR_S_HALT:
        sys.exit("mm_swd: the nRF's core didn't halt.")


def erase_all(bridge):
    print('Erasing the whole nRF...', flush=True)
    bridge.check(CMD_ERASEALL, timeout=10)


def write_uicr(bridge):
    """What a blank UICR needs: debug access released at boot, and the AVR's reset line."""
    nvmc_wait(bridge)
    bridge.write32(NVMC_CONFIG, NVMC_WEN)
    for addr, value in ((UICR_PSELRESET0, RESET_PIN), (UICR_PSELRESET1, RESET_PIN), (UICR_APPROTECT, APPROTECT_HWDISABLED)):
        bridge.write32(addr, value)
        nvmc_wait(bridge)
    bridge.write32(NVMC_CONFIG, NVMC_REN)
    got = bridge.read_words(UICR_PSELRESET0, 3)
    if got != [RESET_PIN, RESET_PIN, APPROTECT_HWDISABLED]:
        sys.exit('mm_swd: UICR reads back as ' + ' '.join(f'0x{w:08X}' for w in got))


def program(bridge, mem):
    pages = pages_of(mem)
    nvmc_wait(bridge)
    bridge.write32(NVMC_CONFIG, NVMC_EEN)
    for i, page in enumerate(pages):
        print(f'\rErasing page {i + 1}/{len(pages)} at 0x{page:05X}', end='', flush=True)
        bridge.write32(NVMC_ERASEPAGE, page)
        nvmc_wait(bridge)
    print()

    # Erased flash reads 0xFFFFFFFF, so only the other words need writing, in runs of
    # consecutive addresses.
    words = {}
    for page in pages:
        data = page_bytes(mem, page)
        for off in range(0, PAGE_SIZE, 4):
            w = struct.unpack_from('<I', data, off)[0]
            if w != 0xFFFFFFFF:
                words[page + off] = w
    runs, run = [], []
    for addr in sorted(words):
        if run and addr != run[0][0] + 4 * len(run):
            runs.append(run)
            run = []
        run.append((addr, words[addr]))
    if run:
        runs.append(run)

    bridge.write32(NVMC_CONFIG, NVMC_WEN)
    done, start = 0, time.monotonic()
    for run in runs:
        for i in range(0, len(run), MEM_WRITE_MAX * 8):
            part = run[i:i + MEM_WRITE_MAX * 8]
            bridge.write_words(part[0][0], [w for _, w in part])
            done += len(part)
            print(f'\rWriting {done * 4}/{len(words) * 4} bytes', end='', flush=True)
    nvmc_wait(bridge)
    bridge.write32(NVMC_CONFIG, NVMC_REN)
    print(f' ({time.monotonic() - start:.1f} s)')


def verify(bridge, mem):
    bad = []
    pages = pages_of(mem)
    for i, page in enumerate(pages):
        print(f'\rVerifying page {i + 1}/{len(pages)}', end='', flush=True)
        if bridge.crc32(page, PAGE_SIZE) != zlib.crc32(page_bytes(mem, page)):
            bad.append(page)
    print()
    if bad:
        sys.exit('mm_swd: verify FAILED at ' + ', '.join(f'0x{p:05X}' for p in bad))
    print('Verified.')


def start_image(bridge):
    """Lets the core go and soft-resets the nRF. Unlike the AVR's pin reset, this works
    straight after an erase-all: a new UICR.PSELRESET only takes effect at the next reset."""
    bridge.write32(DEMCR, 0)
    bridge.write32(DHCSR, DHCSR_DBGKEY)
    # The reset can cut the write's completion short, so its status means nothing.
    bridge.cmd(CMD_MEM_WRITE, struct.pack('<IBI', AIRCR, 1, AIRCR_SYSRESETREQ))


def do_info(bridge, args):
    version, gate, flags, result = bridge.info()
    print(f'Bridge protocol {version}, gate {GATE_TEXT[gate]}, last result: {STATUS_TEXT.get(result, hex(result))}')
    if not flags & INFO_WIRED:
        print(STATUS_TEXT[ST_NO_WIRING])
        return
    print('SWD is wired to the AVR (PCB v7 or later).')
    try:
        dpidr, open_ = bridge.connect()
        print(f'nRF debug port: DPIDR 0x{dpidr:08X}; memory port '
              + ('open (the firmware released APPROTECT)' if open_ else 'locked (new module, or firmware that keeps APPROTECT)'))
    finally:
        if gate == GATE_CLOSED:
            bridge.release()


def do_flash(bridge, args):
    mem = load_hex(args.hex)
    check_image(mem)
    pages = pages_of(mem)
    print(f'{args.hex}: {len(mem)} bytes in {len(pages)} pages, 0x{pages[0]:05X}..0x{pages[-1] + PAGE_SIZE:05X}')
    with session(bridge, allow_locked=True) as state:
        wipe = args.wipe_pairings
        if state == 'locked':
            print("The nRF is locked: a new module, or firmware that doesn't release APPROTECT.")
            if not confirm('Programming it means erasing all of it, its pairings included. Continue?', args.yes):
                return 1
            wipe = True
        elif wipe and not confirm('Erase the whole nRF, pairings included? Every keyboard and mouse will have to pair again.', args.yes):
            return 1
        if wipe:
            erase_all(bridge)
        prepare(bridge)
        if wipe:
            write_uicr(bridge)
        program(bridge, mem)
        verify(bridge, mem)
        start_image(bridge)
    print('Done; the nRF is running the new image.')
    return 0


def do_verify(bridge, args):
    mem = load_hex(args.hex)
    check_image(mem)
    with session(bridge):
        prepare(bridge)
        verify(bridge, mem)


def elf_symbol(path, name):
    """The address of a symbol in an ELF32 little-endian file, or None."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        sys.exit(f'mm_swd: {path} is not a 32-bit little-endian ELF')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2E)
    sections = [struct.unpack_from('<10I', data, shoff + i * shentsize) for i in range(shnum)]
    for sh in sections:
        if sh[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[sh[6]][4]
        for off in range(sh[4], sh[4] + sh[5], 16):
            st_name, st_value = struct.unpack_from('<II', data, off)
            end = data.index(b'\0', strtab + st_name)
            if data[strtab + st_name:end] == name.encode():
                return st_value
    return None


def find_rtt(bridge, args):
    if args.elf:
        addr = elf_symbol(args.elf, '_SEGGER_RTT')
        if addr is None:
            sys.exit(f'mm_swd: no _SEGGER_RTT in {args.elf}')
        return addr
    print('Looking for the RTT control block in RAM (--elf skips this)...', flush=True)
    tail, chunk = b'', 4 * MEM_READ_MAX * 8
    for addr in range(RAM_START, RAM_END, chunk):
        data = tail + bridge.read_bytes(addr, chunk)
        i = data.find(b'SEGGER RTT\0')
        if i >= 0:
            return addr - len(tail) + i
        tail = data[-15:]
    sys.exit('mm_swd: no RTT control block in RAM. Is RTT logging built into this nRF image?')


def do_rtt(bridge, args):
    with session(bridge):
        # Restart the nRF so the log starts at boot, then wait for it to release APPROTECT
        # and set RTT up.
        bridge.check(CMD_RESET)
        time.sleep(0.3)
        bridge.connect()
        cb = find_rtt(bridge, args)
        up = cb + 24  # past acID[16], MaxNumUpBuffers, MaxNumDownBuffers
        _, buf, size, _, rd, _ = bridge.read_words(up, 6)  # sName, pBuffer, SizeOfBuffer, WrOff, RdOff, Flags
        print(f'RTT at 0x{cb:08X}, up buffer 0: {size} bytes at 0x{buf:08X}. Ctrl-C to stop.\n', flush=True)
        out = sys.stdout.buffer
        try:
            while True:
                wr = bridge.read32(up + 12)
                if wr == rd:
                    time.sleep(0.05)
                    continue
                if wr > rd:
                    data = bridge.read_bytes(buf + rd, wr - rd)
                else:
                    data = bridge.read_bytes(buf + rd, size - rd) + bridge.read_bytes(buf, wr)
                bridge.write32(up + 16, wr)  # RdOff: RAM, so the session can end without a reset
                rd = wr
                out.write(data)
                out.flush()
        except KeyboardInterrupt:
            print()


def do_read(bridge, args):
    with session(bridge):
        bridge.connect()
        words = bridge.read_words(args.addr, args.words)
    for i in range(0, len(words), 4):
        print(f'{args.addr + 4 * i:08X}: ' + ' '.join(f'{w:08X}' for w in words[i:i + 4]))


def do_reset(bridge, args):
    with session(bridge):
        bridge.check(CMD_RESET)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('--vid', type=lambda s: int(s, 0), default=DONGLE_VID)
    p.add_argument('--pid', type=lambda s: int(s, 0), default=DONGLE_PID)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('info', help='what the dongle and its nRF report')
    f = sub.add_parser('flash', help='program an Intel HEX image')
    f.add_argument('hex')
    f.add_argument('--wipe-pairings', action='store_true', help='erase the whole nRF first, pairings included')
    f.add_argument('-y', '--yes', action='store_true', help="don't ask before erasing everything")
    v = sub.add_parser('verify', help='compare the nRF with an Intel HEX image')
    v.add_argument('hex')
    r = sub.add_parser('rtt', help="restart the nRF and print its RTT log")
    r.add_argument('--elf', help='the image ELF, to find the RTT control block without scanning RAM')
    rd = sub.add_parser('read', help='hex dump of nRF memory')
    rd.add_argument('addr', type=lambda s: int(s, 0))
    rd.add_argument('words', type=lambda s: int(s, 0), nargs='?', default=1)
    sub.add_parser('reset', help='pin-reset the nRF')
    args = p.parse_args()

    bridge = find_dongle(args)
    try:
        handler = {'info': do_info, 'flash': do_flash, 'verify': do_verify, 'rtt': do_rtt, 'read': do_read, 'reset': do_reset}
        return handler[args.command](bridge, args) or 0
    except BridgeError as e:
        sys.exit(f'mm_swd: {e}')
    except KeyboardInterrupt:
        sys.exit(130)
    finally:
        bridge.close()


if __name__ == '__main__':
    sys.exit(main())
