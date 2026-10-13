#!/usr/bin/env python3
"""Drive the running game from the PC (debug builds; platform/3ds/README.md, "Console link").

Talks to the game's own FTP server (OPTIONS -> DEVELOPER -> FTP SERVER, port 5000) with the SITE
commands of src/common/platform/3ds/debug_link_3ds.h. Standard library only.

    lod-link.py [--ip IP] COMMAND [ARGS]

    wait [SECONDS]            until the game answers (after an install or boot); default 120
    state                     poll, tic, game state, open menu and item, map, player, fps
    keys BUTTONS [POLLS]      hold A+B+LEFT... (3DS names) for N input polls (35 a second), default 1
    press BUTTONS [POLLS]     keys, then wait until those polls have passed
    stick CPAD|CSTICK X Y [POLLS]   hold a stick at X,Y in -1..1 (Y up), default 1 poll
    touch X Y                 tap the bottom screen (320x240)
    polls N                   wait until the game has run N more input polls
    sleep SECONDS             wait on the PC
    console COMMAND...        run a GZDoom console command (map E1M1, give all, save x...)
    cvar NAME [VALUE]         read a console variable, or set it
    dump                      quick diagnostic capture (as L+R+A), into dumps/
    perf SECONDS [DIR]        record frame times for SECONDS, fetch debug/perf-NNNN.csv and
                              summarise it (perf-analyze.py); default DIR /tmp/lod-perf
    shot [DIR]                screenshots of both screens as PNG, default /tmp/lod-shot
    peek WHAT LENGTH          hex bytes; WHAT is a hex address, a symbol of the ELF or SYMBOL+HEX
    poke WHAT HEXBYTES        write bytes there
    get REMOTE [LOCAL]        fetch a file; REMOTE relative to the data folder unless it starts with /
    run FILE                  one command per line ('#' comments), stops at the first error

The IP defaults to .3ds_ftp_ip in the repository (the build assistant's last).
Symbols come from the newest ELF in build-3ds/ through arm-none-eabi-nm.
"""
import ftplib
import glob
import io
import os
import re
import shlex
import struct
import subprocess
import sys
import time
import zlib

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
PLATFORM = os.path.join(REPO, 'src', 'common', 'platform', '3ds')
NM = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
PORT = 5000


def header_define(name, key):
    with open(os.path.join(PLATFORM, name)) as f:
        return re.search(r'#define %s "([^"]*)"' % key, f.read()).group(1)


def credentials():
    """The in-game FTP server's login, from ftp_credentials_3ds.h."""
    return header_define('ftp_credentials_3ds.h', 'FTP_USER'), header_define('ftp_credentials_3ds.h', 'FTP_PASS')


DATA = header_define('app_paths_3ds.h', 'LOD3DS_APP_SD_PATH')


class LinkError(Exception):
    pass


class Link:
    def __init__(self, ip):
        self.ip = ip
        self.ftp = None
        self.symbols = None

    def connect(self, timeout=10):
        if self.ftp is None:
            ftp = ftplib.FTP()
            ftp.connect(self.ip, PORT, timeout=timeout)
            ftp.login(*credentials())
            self.ftp = ftp
        return self.ftp

    def close(self):
        if self.ftp is not None:
            try:
                self.ftp.quit()
            except Exception:
                pass
            self.ftp = None

    def site(self, text):
        try:
            # A level load can keep the game thread busy a few seconds.
            self.connect().sock.settimeout(40)
            reply = self.connect().sendcmd('SITE ' + text)
        except ftplib.error_reply as e:
            reply = str(e)
        except ftplib.all_errors as e:
            self.ftp = None
            raise LinkError(str(e))
        if not reply.startswith('2'):
            raise LinkError(reply)
        return reply[4:]

    def state(self):
        return dict(kv.split('=', 1) for kv in self.site('STATE').split() if '=' in kv)

    def poll(self):
        return int(self.state()['poll'])

    def wait_polls(self, n, timeout=60):
        target = self.poll() + n
        end = time.time() + timeout
        while self.poll() < target:
            if time.time() > end:
                raise LinkError('the game stopped advancing')
            time.sleep(0.05)

    def retrieve(self, remote, local):
        with open(local, 'wb') as f:
            self.connect().retrbinary('RETR ' + remote, f.write)

    def elf(self):
        elves = glob.glob(os.path.join(REPO, 'build-3ds', '**', '*.elf'), recursive=True)
        if not elves:
            raise LinkError('no ELF under build-3ds/ for symbols (build first)')
        return max(elves, key=os.path.getmtime)

    def symbol(self, name):
        if self.symbols is None:
            out = subprocess.run([NM, '-C', self.elf()], capture_output=True, text=True, check=True).stdout
            self.symbols = {}
            for line in out.splitlines():
                parts = line.split(None, 2)
                if len(parts) == 3:
                    self.symbols[parts[2]] = int(parts[0], 16)
        if name not in self.symbols:
            raise LinkError('no symbol %s in %s' % (name, self.elf()))
        return self.symbols[name]

    def address(self, what):
        try:
            return int(what, 16)
        except ValueError:
            pass
        name, _, offset = what.partition('+')
        return self.symbol(name) + (int(offset, 16) if offset else 0)


def bmp_to_png(data):
    """24-bit bottom-up BMP (what the game writes) to PNG bytes."""
    offset, = struct.unpack_from('<I', data, 10)
    width, height = struct.unpack_from('<ii', data, 18)
    stride = (width * 3 + 3) & ~3
    raw = bytearray()
    for y in range(height):
        row = data[offset + (height - 1 - y) * stride:][:width * 3]
        raw.append(0)
        for x in range(width):
            b, g, r = row[x * 3:x * 3 + 3]
            raw += bytes((r, g, b))

    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def run(link, argv):
    cmd, args = argv[0], argv[1:]
    if cmd == 'wait':
        end = time.time() + (float(args[0]) if args else 120)
        while True:
            try:
                link.close()
                link.connect(timeout=3)
                print(link.site('STATE'))
                return
            except (LinkError, EOFError) + ftplib.all_errors:
                if time.time() > end:
                    raise LinkError('no answer from %s:%d' % (link.ip, PORT))
                time.sleep(2)
    if cmd == 'state':
        print(link.site('STATE'))
    elif cmd in ('keys', 'press'):
        polls = int(args[1]) if len(args) > 1 else 1
        print(link.site('KEYS %s %d' % (args[0], polls)))
        if cmd == 'press':
            link.wait_polls(polls + 1)
    elif cmd == 'stick':
        polls = int(args[3]) if len(args) > 3 else 1
        print(link.site('STICK %s %s %s %d' % (args[0], args[1], args[2], polls)))
    elif cmd == 'touch':
        print(link.site('TOUCH %d %d' % (int(args[0]), int(args[1]))))
    elif cmd == 'polls':
        link.wait_polls(int(args[0]))
    elif cmd == 'sleep':
        time.sleep(float(args[0]))
    elif cmd == 'console':
        print(link.site('CONSOLE ' + ' '.join(args)))
    elif cmd == 'cvar':
        print(link.site('CVAR ' + ' '.join(args)))
    elif cmd == 'perf':
        seconds = float(args[0]) if args else 10
        out = args[1] if len(args) > 1 else '/tmp/lod-perf'
        os.makedirs(out, exist_ok=True)
        link.site('CONSOLE lod3ds_perf start')
        time.sleep(seconds)
        link.site('CONSOLE lod3ds_perf stop')
        # The game writes the file on its own thread after the command; give it a moment.
        names = []
        for _ in range(20):
            time.sleep(0.5)
            names = sorted(n for n in link.connect().nlst(DATA + '/debug') if re.search(r'perf-\d+\.csv$', n))
            if names:
                break
        if not names:
            raise LinkError('no perf-NNNN.csv in debug/')
        name = os.path.basename(names[-1])
        path = os.path.join(out, name)
        link.retrieve('%s/debug/%s' % (DATA, name), path)
        print(path)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        analyze = __import__('perf-analyze')
        header, rows = analyze.load(path)
        if header:
            print(header)
        analyze.summarise(rows, by_state=True)
    elif cmd == 'dump':
        print(link.site('DUMP'))
    elif cmd == 'shot':
        out = args[0] if args else '/tmp/lod-shot'
        os.makedirs(out, exist_ok=True)
        link.site('SHOT')
        for screen in ('top', 'bottom'):
            buf = io.BytesIO()
            link.connect().retrbinary('RETR %s/debug/shot-%s.bmp' % (DATA, screen), buf.write)
            path = os.path.join(out, screen + '.png')
            with open(path, 'wb') as f:
                f.write(bmp_to_png(buf.getvalue()))
            print(path)
    elif cmd == 'peek':
        address, length = link.address(args[0]), int(args[1], 0)
        data = ''
        while len(data) < length * 2:
            n = min(256, length - len(data) // 2)
            data += link.site('PEEK %x %d' % (address + len(data) // 2, n))
        print(data)
    elif cmd == 'poke':
        print(link.site('POKE %x %s' % (link.address(args[0]), args[1])))
    elif cmd == 'get':
        remote = args[0] if args[0].startswith('/') else '%s/%s' % (DATA, args[0])
        local = args[1] if len(args) > 1 else os.path.basename(remote)
        link.retrieve(remote, local)
        print(local)
    elif cmd == 'run':
        with open(args[0]) as f:
            for line in f:
                line = line.split('#', 1)[0].strip()
                if line:
                    print('>', line)
                    run(link, shlex.split(line))
    else:
        raise LinkError('unknown command %s (see --help)' % cmd)


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    ip = None
    if argv[0] == '--ip':
        ip, argv = argv[1], argv[2:]
    if ip is None:
        try:
            with open(os.path.join(REPO, '.3ds_ftp_ip')) as f:
                ip = f.read().strip()
        except OSError:
            print('no --ip and no .3ds_ftp_ip', file=sys.stderr)
            return 2
    link = Link(ip)
    try:
        run(link, argv)
    except LinkError as e:
        print('lod-link: %s' % e, file=sys.stderr)
        return 1
    finally:
        link.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
