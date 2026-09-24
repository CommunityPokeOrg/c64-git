#!/usr/bin/env python3
"""vicemon.py - drive VICE's remote monitor to test the C64 git client.

Model: ONE persistent connection. Connecting halts the CPU; we send 'x'
to resume and keep polling 'm 9ffe 9fff'. While the CPU runs, monitor
responses lag behind the command stream, so we accumulate everything
received and regex-search the stream for the marker line.

The program signals completion by writing 0xC6 0x64 to $9FFE/$9FFF
after appending the whole report to a capture buffer at $8000.

usage:
  vicemon.py --out report_c64.txt [--screen screen.txt] \
             [--shot shot.png] [--timeout 240] [--port 6510] \
             [--preroll 15]
"""
import argparse
import re
import socket
import sys
import time

# C64 screen code -> ASCII, for the lowercase charset the program selects.
def screen_to_text(mem):
    out = []
    for i, c in enumerate(mem):
        if c & 0x80:
            c &= 0x7F            # reverse video: same glyph
        if c == 0:
            ch = '@'
        elif 1 <= c <= 26:
            ch = chr(ord('a') + c - 1)      # lowercase charset: 1-26 = a-z
        elif 32 <= c <= 63:
            ch = chr(c)
        elif 65 <= c <= 90:
            ch = chr(c)                     # A-Z
        elif 91 <= c <= 95:
            ch = chr(c)
        else:
            ch = ' '
        out.append(ch)
    lines = []
    for row in range(25):
        lines.append(''.join(out[row * 40:(row + 1) * 40]).rstrip())
    return '\n'.join(lines)

# The C64 side compiles with an identity ASCII charmap (see
# src/ascii_charmap.h), so capture-buffer bytes are already ASCII;
# only CR newlines need mapping back to LF.
def petscii_to_ascii(b):
    return b.replace(b'\x0d', b'\x0a')

MEM_LINE = re.compile(r'>\s*C:\s*([0-9a-f]+)\s+(.*)')


def parse_mem(stream, start):
    """parse `m` dump lines from the accumulated stream -> bytes starting
    at `start` (last dump wins)"""
    data = {}
    for m in MEM_LINE.finditer(stream):
        addr = int(m.group(1), 16)
        vals = []
        for chunk in re.split(r' {2,}', m.group(2)):
            for tok in chunk.split():
                if re.fullmatch(r'[0-9a-fA-F]{2}', tok):
                    vals.append(int(tok, 16))
                else:
                    break
        for i, v in enumerate(vals):
            data[addr + i] = v
    return bytes(data.get(a, 0) for a in range(start, start + len(data)))


class ViceMon:
    def __init__(self, port):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=10)
        self.s.settimeout(0.5)
        self.stream = b''
        self._collect(2.0)

    def _collect(self, dur):
        """accumulate everything the monitor sends for `dur` seconds"""
        end = time.time() + dur
        while time.time() < end:
            try:
                d = self.s.recv(65536)
                if not d:
                    break
                self.stream += d
            except socket.timeout:
                pass
        return self.stream

    def send(self, c):
        self.s.sendall((c + '\n').encode())

    def ask(self, c, dur=2.0):
        self.send(c)
        return self._collect(dur)

    def quit(self):
        try:
            self.send('quit')
            self._collect(1.0)
        except OSError:
            pass
        try:
            self.s.close()
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=6510)
    ap.add_argument('--timeout', type=float, default=240)
    ap.add_argument('--preroll', type=float, default=15,
                    help='seconds to let the machine run before polling')
    ap.add_argument('--out')
    ap.add_argument('--screen')
    ap.add_argument('--shot')
    args = ap.parse_args()

    if args.preroll:
        time.sleep(args.preroll)

    mon = ViceMon(args.port)
    mon.send('x')            # resume CPU halted by the connect
    mon._collect(1.0)

    # poll the done marker; monitor replies lag while the CPU runs,
    # so search the cumulative stream for the last 9ffe dump line
    deadline = time.time() + args.timeout
    done = False
    while time.time() < deadline:
        mon.ask('m 9ffe 9fff', dur=2.0)
        stream = mon.stream.decode('latin-1')
        dumps = re.findall(r'>C:9ffe\s+([0-9a-f]{2})\s+([0-9a-f]{2})', stream)
        if dumps and dumps[-1] == ('c6', '64'):
            done = True
            break
        time.sleep(2.0)
    if not done:
        print('vicemon: TIMEOUT waiting for done marker', file=sys.stderr)
    else:
        print('vicemon: done marker seen')

    # dump the capture buffer (send twice: replies lag one command)
    mon.ask('m 8000 9eff', dur=3.0)
    mon.ask('m 8000 9eff', dur=max(6.0, (0x9F00 - 0x8000) / 4096))
    stream = mon.stream.decode('latin-1')
    buf = parse_mem(stream, 0x8000)
    text = petscii_to_ascii(buf)
    end = text.find(b'*END*\n')
    if end >= 0:
        report = text[:end + len(b'*END*\n')]
    else:
        report = text.split(b'\x00')[0]
    if args.out:
        with open(args.out, 'wb') as f:
            f.write(report)
        print('vicemon: wrote %s (%d bytes)' % (args.out, len(report)))
    else:
        sys.stdout.write(report.decode('latin-1'))

    if args.screen:
        mon.ask('m 400 7e7', dur=3.0)
        mon.ask('m 400 7e7', dur=3.0)
        scr = parse_mem(mon.stream.decode('latin-1'), 0x0400)[:0x3E8]
        with open(args.screen, 'w') as f:
            f.write(screen_to_text(scr))
        print('vicemon: wrote %s' % args.screen)

    if args.shot:
        mon.ask('screenshot "%s"' % args.shot, dur=3.0)

    mon.quit()
    sys.exit(0 if done else 2)


if __name__ == '__main__':
    main()
