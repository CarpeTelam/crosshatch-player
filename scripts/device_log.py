#!/usr/bin/env python3
"""
Capture the serial logs of two or more devices at once into one merged log, merge the logs two devices wrote to their
SD cards, and summarize a device log.

A two-device run (epic-play-nearby) needs both devices' logs on one clock. `capture` reads the ports with `termios`
and `select` (no pyserial), reconnects by itself when a port goes away and comes back (the X4 Pro renumbers its USB
port when it sleeps), and writes two kinds of file into one output directory:

  <NAME>.raw.log   every byte the port gave, in order, unchanged, across reconnects: nothing is added
  merged.log       one line per device line, `<UTC stamp> <NAME> <line>`, in host-time order; the stamp is taken when
                   the line completes, and never goes below the previous line's, so file order is time order

A merged line looks like `2026-10-12T09:15:02.481Z A [41230] [INF] [GAME] sudoku: Playing -> Leaving on ForcedExit`.
The device's own line is kept as bytes (one trailing CR dropped), never decoded. When a port is lost or comes back,
the merged log (and stderr) gets `<stamp> <NAME> [device_log] lost <path>: <reason>; waiting for it to come back` or
`... connected <path>`; the raw log gets nothing. DTR and RTS are cleared on connect, best effort, so connecting does
not reset a board that wires them to its reset line.

  capture --port NAME=PATH [--port NAME=PATH ...] --out DIR [--baud 115200] [--duration SECONDS]
              SIGINT, SIGTERM, and --duration end it cleanly, with a half line flushed to the merged log
  merge --out DIR CARD [CARD ...]
              the no-computer run: a CARD is a log file a diagnostics build (`-DCROSSHATCH_SD_LOG=1`) wrote to a
              device's SD card, or the folder `crosshatch-log` holding them (a card's root also works), optionally
              `NAME=` before it (`A=/media/cardA`); the files of one device are read in boot order. The devices share
              no clock, so it lines them up by the `LINK` lines: a `STATE` a device sent at `ver` is received by the
              other at the same `session` and `ver`, and the fastest pairs in each direction give the clock offset.
              It writes the same merged.log and <NAME>.raw.log as `capture` (the raw log is the device's files joined,
              unchanged) plus summary.txt, and prints the alignment and the summary. Stamps are the run clock (time
              since the earliest logged line or file-open time of any device, on the 1970 epoch), not wall-clock time.
  summary [--mem-all] LOG [LOG ...]
              per device, from a raw device log or the merged log: connects and losses, each `VM stopped` with its
              game, dropped touches and hand-offs per game, the `[MEM]` figures (device-run-packet.md L1 to L3), and
              a nearby match's lines: the radio bring-ups, each `LINK` session's `STATE`s, resends, silences, `ABORT`s,
              leaves and `GameLink` task stack, the `PeerGone` entries and their reasons (epic-play-nearby R13)

Exit 0: done. 1: not used (no rule is checked). 2: the command could not run (a bad argument, an output directory
that cannot be made, an unreadable log, a card file that is not a diagnostics log).

Local run (a computer with two USB ports; docs/crosshatch/fork-scripts.md has the walkthrough):
    python3 scripts/device_log.py capture --port A=/dev/serial/by-id/usb-A --port B=/dev/serial/by-id/usb-B --out logs
    python3 scripts/device_log.py summary logs/merged.log
    python3 scripts/device_log.py merge --out run-logs A=/media/cardA B=/media/cardB   # SD cards, no computer needed
    python3 scripts/device_log_test.py
"""

import argparse
import collections
import contextlib
import errno
import fcntl
import os
import pathlib
import re
import select
import signal
import struct
import sys
import termios
import threading
import time

import fork_common
from fork_common import SetupError

DEFAULT_BAUD = 115200
RETRY_SECONDS = 0.5  # how often a port that is not open is tried again
TICK_SECONDS = 0.1  # the longest the loop waits in select, so a stop request is seen quickly
READ_SIZE = 65536
MAX_PARTIAL_LINE = 64 * 1024  # a line this long without a newline is flushed as a line
NAME_PATTERN = re.compile(r'[A-Za-z0-9_-]+')
MERGED_LOG = 'merged.log'
RAW_SUFFIX = '.raw.log'
EVENT_TAG = '[device_log]'


def parse_port(spec):
    """(name, path) from a NAME=PATH argument; anything else is a SetupError."""
    name, separator, path = spec.partition('=')
    if not separator or not NAME_PATTERN.fullmatch(name) or not path:
        raise SetupError(f'--port {spec!r} is not NAME=PATH (a name of letters, digits, _ and -, then the port path)')
    return name, path


def parse_ports(specs):
    """The (name, path) list of --port arguments; at least one, names unique, paths unique after symlinks."""
    if not specs:
        raise SetupError('give at least one --port NAME=PATH')
    ports = [parse_port(spec) for spec in specs]
    names, paths = {}, {}
    for name, path in ports:
        if name in names:
            raise SetupError(f'the port name {name} is used twice')
        names[name] = path
        real = os.path.realpath(path)
        if real in paths:
            raise SetupError(f'{path} is the same port as {paths[real]}')
        paths[real] = path
    return ports


def baud_constant(baud):
    """The termios speed constant of a baud rate, or a SetupError when termios has none."""
    speed = getattr(termios, f'B{baud}', None) if isinstance(baud, int) and baud > 0 else None
    if not isinstance(speed, int):
        raise SetupError(f'unknown baud rate {baud}: termios has no B{baud}')
    return speed


def open_port(path, speed):
    """Open path as a raw 8N1 reader and return its fd; raises OSError, or termios.error when it is not a tty."""
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        iflag, oflag, cflag, lflag, _ispeed, _ospeed, cc = termios.tcgetattr(fd)
        # What tty.cfmakeraw does (that function is Python 3.12+; the floor here is 3.11), then 8N1 without HUPCL.
        iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP | termios.INLCR | termios.IGNCR
                   | termios.ICRNL | termios.IXON | termios.IXOFF | termios.IXANY)
        oflag &= ~termios.OPOST
        lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
        cflag &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.HUPCL | getattr(termios, 'CRTSCTS', 0))
        cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, speed, speed, cc])
        # Best effort: a port without modem lines (a pty) refuses, and that is fine.
        bits = termios.TIOCM_DTR | termios.TIOCM_RTS
        with contextlib.suppress(OSError):
            fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack('I', bits))
    except BaseException:
        os.close(fd)
        raise
    return fd


def format_stamp(milliseconds):
    return time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime(milliseconds // 1000)) + f'.{milliseconds % 1000:03d}Z'


class Stamper:
    """UTC millisecond stamps from a clock (seconds since the epoch) that never go below the previous stamp."""

    def __init__(self, clock):
        self.clock = clock
        self.last = 0

    def next(self):
        self.last = max(int(self.clock() * 1000), self.last)
        return format_stamp(self.last)


class Device:
    """One port: its name, path, open fd (None while it is away), partial line, and raw log."""

    def __init__(self, name, path, raw):
        self.name = name
        self.path = path
        self.raw = raw
        self.fd = None
        self.partial = bytearray()
        self.next_try = 0.0
        self.noted = set()  # open-failure reasons already printed since the last connect


def write_all(stream, data):
    """Write all of data to an unbuffered stream, which may take fewer bytes than it is given; OSError if it takes 0."""
    view = memoryview(data)
    while view:
        count = stream.write(view)
        if not count:
            raise OSError(errno.EIO, 'the log file took no bytes')
        view = view[count:]


class Capture:
    """The state of one capture run: devices, the merged log, the stamper, and where notes go."""

    def __init__(self, devices, merged, stamper, err):
        self.devices = devices
        self.merged = merged
        self.stamper = stamper
        self.err = err

    @staticmethod
    def put(stream, data):
        """Write all of data to a log; a failing log (disk full, directory gone) is a SetupError, so exit 2."""
        try:
            write_all(stream, data)
        except OSError as exc:
            raise SetupError(f'cannot write the logs ({getattr(stream, "name", stream)}): {exc}')

    def note(self, text):
        """Print a note on stderr; a closed or broken stderr never stops the capture."""
        with contextlib.suppress(OSError, ValueError):
            print(text, file=self.err, flush=True)

    def line(self, device, text):
        """Write one merged line (bytes) of a device, stamped now."""
        self.put(self.merged, self.stamper.next().encode() + b' ' + device.name.encode() + b' ' + text + b'\n')

    def event(self, device, text):
        stamp = self.stamper.next()
        line = f'{stamp} {device.name} {EVENT_TAG} {text}'
        self.put(self.merged, line.encode(errors='replace') + b'\n')
        self.note(line)

    def feed(self, device, data):
        """Take bytes from a port: raw log first, unchanged, then every line they complete."""
        self.put(device.raw, data)
        device.partial += data
        start = 0
        while True:
            end = device.partial.find(b'\n', start)
            if end < 0:
                break
            text = bytes(device.partial[start:end])
            self.line(device, text[:-1] if text.endswith(b'\r') else text)
            start = end + 1
        del device.partial[:start]
        if len(device.partial) > MAX_PARTIAL_LINE:
            self.flush(device)

    def flush(self, device):
        """Write a half line to the merged log (never to the raw log, which already has its bytes)."""
        if device.partial:
            text = bytes(device.partial)
            self.line(device, text[:-1] if text.endswith(b'\r') else text)
            device.partial.clear()

    def lose(self, device, reason, now):
        self.flush(device)
        if device.fd is not None:
            with contextlib.suppress(OSError):
                os.close(device.fd)
            device.fd = None
        device.next_try = now + RETRY_SECONDS
        device.noted.clear()
        self.event(device, f'lost {device.path}: {reason}; waiting for it to come back')

    def try_open(self, device, speed, now):
        device.next_try = now + RETRY_SECONDS
        try:
            device.fd = open_port(device.path, speed)
        except termios.error as exc:
            reason = f'not a serial port ({exc.args[-1] if exc.args else exc})'
        except OSError as exc:
            reason = f'cannot open ({exc})'
        else:
            device.noted.clear()
            self.event(device, f'connected {device.path}')
            return
        if reason not in device.noted:
            device.noted.add(reason)
            self.note(f'device_log: {device.name} {device.path}: {reason}; trying again every {RETRY_SECONDS} s')

    def read(self, device, now):
        try:
            data = os.read(device.fd, READ_SIZE)
        except BlockingIOError:
            return
        except OSError as exc:
            self.lose(device, f'read failed: {exc}', now)
            return
        if not data:
            self.lose(device, 'the port closed (end of file)', now)
            return
        self.feed(device, data)

    def run(self, speed, stop, deadline):
        while not stop.is_set():
            now = time.monotonic()
            if deadline is not None and now >= deadline:
                break
            for device in self.devices:
                if device.fd is None and now >= device.next_try:
                    self.try_open(device, speed, now)
            by_fd = {device.fd: device for device in self.devices if device.fd is not None}
            wait = TICK_SECONDS if deadline is None else max(0.0, min(TICK_SECONDS, deadline - now))
            if not by_fd:
                stop.wait(wait)
                continue
            ready, _, _ = select.select(list(by_fd), [], [], wait)
            now = time.monotonic()
            for fd in ready:
                self.read(by_fd[fd], now)

    def close(self, quiet=False):
        """Flush the half lines and close the ports; quiet drops a SetupError so it cannot mask the one being raised."""
        failure = None
        for device in self.devices:
            try:
                self.flush(device)
            except SetupError as exc:
                failure = failure or exc
            if device.fd is not None:
                with contextlib.suppress(OSError):
                    os.close(device.fd)
                device.fd = None
        if failure and not quiet:
            raise failure


def capture(ports, out_dir, baud=DEFAULT_BAUD, stop=None, clock=time.time, duration=None, err=None):
    """Read every (name, path) port until stop is set or duration seconds pass; return 0.

    Writes <name>.raw.log for each port and merged.log into out_dir (appending to files already there). clock
    returns seconds since the epoch and sets the merged log's stamps; err (default stderr) gets the events and the
    notes about a port that does not open.
    """
    ports = parse_ports([f'{name}={path}' for name, path in ports])
    speed = baud_constant(baud)
    stop = threading.Event() if stop is None else stop
    err = sys.stderr if err is None else err
    out = pathlib.Path(out_dir)
    with contextlib.ExitStack() as files:
        try:
            out.mkdir(parents=True, exist_ok=True)
            merged = files.enter_context(open(out / MERGED_LOG, 'ab', buffering=0))
            devices = [
                Device(name, path, files.enter_context(open(out / f'{name}{RAW_SUFFIX}', 'ab', buffering=0)))
                for name, path in ports
            ]
        except OSError as exc:
            raise SetupError(f'cannot write the logs in {out}: {exc}')
        run = Capture(devices, merged, Stamper(clock), err)
        deadline = None if duration is None else time.monotonic() + duration
        try:
            run.run(speed, stop, deadline)
        except BaseException:
            run.close(quiet=True)
            raise
        run.close()
    return fork_common.PASS


# --- summary ---------------------------------------------------------------------------------------------------

MERGED_PREFIX = re.compile(r'(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z) ([A-Za-z0-9_-]+) (.*)', re.DOTALL)
MILLIS_PREFIX = re.compile(r'\[(\d+)\] ')
# The device's line formats; scripts/device_log_test.py pins each against its firmware source.
LIFECYCLE = re.compile(r'\[GAME\] ([^\s:]+): ([A-Za-z]+) -> ([A-Za-z]+) on ([A-Za-z]+)\s*$')
VM_STOPPED = re.compile(
    r'VM stopped; arena peak (\d+) bytes, stack high-water (\d+) bytes free, least at a hook (\d+) bytes')
DROPPED_BEFORE_HAND_OFF = re.compile(r'\[GAME\] ([^\s:]+): dropped a touch that began (-?\d+) ms before the hand-off')
DROPPED_BEFORE_DISPLAY = re.compile(r'\[GAME\] ([^\s:]+): dropped a touch before the frame was on the panel')
HAND_OFF_PUSHED = re.compile(r'\[GAME\] ([^\s:]+): hand-off screen pushed in (\d+) ms')
MEM_LINE = re.compile(
    r'\[MEM\] (PSRAM: )?Free: (\d+) bytes, Total: (\d+) bytes, Min Free: (\d+) bytes, MaxAlloc: (\d+) bytes')
# A nearby match (src/games/GameLinkLog.h writes the `session=` lines, EspNowLink.h the radio's, GameMatchActivity.cpp the
# PeerGone ones). The prefix is `[ms] [LEVEL] [LINK] `; a LINK line the patterns below do not know is kept as `other`.
LINK_LINE = re.compile(r'\[(?:INF|ERR|DBG)\] \[LINK\] (.*)$')
LINK_ERROR_LINE = re.compile(r'\[ERR\] \[LINK\] (.*)$')
RADIO_UP = re.compile(r'radio up on channel (\d+) as ((?:[0-9A-F]{2}:){5}[0-9A-F]{2})\s*$')
LINK_SESSION = re.compile(r'session=([0-9a-f]{8}) (.*)$')
LINK_STATE_SENT = re.compile(r'state sent ver=(\d+)\s*$')
LINK_STATE_RECV = re.compile(r'state recv ver=(\d+) seat=(\d+)\s*$')
LINK_STATE_ADOPT = re.compile(r'state adopt ver=(\d+) seat=(\d+) result=([A-Za-z]+)\s*$')
LINK_QUEUE_FULL = re.compile(r'queue full ver=(\d+) overflows=(\d+)\s*$')
LINK_ABORT_RECV = re.compile(r'abort recv reason=([a-z_]+) seat=(\d+)\s*$')
LINK_LEAVE = re.compile(r'leave reason=([a-z_]+) waited=(\d+)ms unacked=(\d+) copies=(\d+)\s*$')
LINK_COUNTER = re.compile(r'(resend|silence|abort sent|link refused) n=(\d+) total=(\d+)\s*$')
LINK_TASK_STARTED = re.compile(r'task started; stack (\d+) bytes\s*$')
LINK_TASK_STOPPED = re.compile(r'task stopped; stack high-water (\d+) bytes free\s*$')
# The diagnostics log's own lines (lib/DiagLog/DiagLogWriter.h writes them into the card file; the test pins the formats).
DIAG_DROPPED = re.compile(r'\[ERR\] \[DIAG\] dropped (\d+) lines')
DIAG_LOST = re.compile(r'\[ERR\] \[DIAG\] lost (\d+) bytes')
# What `merge` writes to merged.log for a card file; summary reads the firmware and the boots back from these.
CARD_EVENT = re.compile(r'card file (\S+): boot (\d+), segment (\d+), firmware (.*?), board (.*)$')
NEARBY_ERROR = re.compile(r'\[ERR\] \[GAME\] (Nearby: .*)$')  # the lobby's own failures (GameLobbyActivity.cpp)
PEER_GONE_REASON = re.compile(r'\[GAME\] ([^\s:]+): the other player is gone: ([a-z_]+)\s*$')

MemFigures = collections.namedtuple('MemFigures', 'when free total min_free max_alloc')
VmStop = collections.namedtuple('VmStop', 'when game arena stack hook')


class GameStats:
    def __init__(self):
        self.dropped_before_hand_off = 0
        self.dropped_before_display = 0
        self.hand_offs = 0
        self.largest_hand_off_ms = None


class LinkSession:
    """What the LINK lines of one `session=` id say (GameLinkLog.h); the host's id, or 00000000 before a guest joined."""

    def __init__(self, session):
        self.session = session
        self.state_sent = 0
        self.last_sent_ver = None
        self.state_recv = 0
        self.last_recv_ver = None
        self.adopted = collections.OrderedDict()  # result -> count
        self.last_adopt_ver = None
        self.queue_full = 0
        self.last_overflows = None
        self.abort_recv = collections.OrderedDict()  # (reason, seat) -> count
        self.leaves = []  # (when, reason, waited ms, unacked, copies)
        self.counters = collections.OrderedDict((name, 0) for name in ('resend', 'silence', 'abort sent', 'link refused'))
        self.stack_bytes = None
        self.high_water = []  # (when, bytes free) of each `task stopped`
        self.other = []  # (when, text) of each LINK line of this session the patterns do not know


class MemSeries:
    """The [MEM] lines of one kind (heap or PSRAM) of a device."""

    def __init__(self):
        self.count = 0
        self.first = None
        self.last = None
        self.least_free = None
        self.least_max_alloc = None

    def add(self, figures):
        self.count += 1
        self.first = self.first or figures
        self.last = figures
        if self.least_free is None or figures.min_free < self.least_free.min_free:
            self.least_free = figures
        if self.least_max_alloc is None or figures.max_alloc < self.least_max_alloc.max_alloc:
            self.least_max_alloc = figures


class DeviceStats:
    def __init__(self, name):
        self.name = name
        self.sources = []
        self.connected = 0
        self.lost = 0
        self.merged = False
        self.cards = False  # the log came from `merge`: card files, not a serial capture
        self.card_files = 0
        self.boots = set()
        self.firmware = []  # distinct firmware ids of the card files, first seen first
        self.boards = []
        self.diag_dropped = 0  # lines the device's log buffer dropped when full (the [DIAG] dropped lines)
        self.diag_lost = 0  # bytes its writer lost to a failed card write
        self.game = None
        self.stops = []
        self.games = collections.OrderedDict()
        self.heap = MemSeries()
        self.psram = MemSeries()
        self.other_mem = []  # (when, text) of each other [MEM] line
        self.mem_lines = []  # (when, text) of every [MEM] line, for --mem-all
        self.radio_up = []  # (when, text) of each `radio up` line: one per bring-up
        self.link_sessions = collections.OrderedDict()  # session id -> LinkSession, first seen first
        self.link_errors = []  # (when, text) of each [ERR] [LINK] line
        self.link_other = []  # (when, text) of each other LINK line
        self.peer_gone = []  # (when, game, event) of each lifecycle entry into PeerGone
        self.peer_gone_reasons = collections.OrderedDict()  # reason -> count, from `the other player is gone`

    def game_stats(self, game):
        return self.games.setdefault(game, GameStats())

    def link_session(self, session):
        return self.link_sessions.setdefault(session, LinkSession(session))

    def has_nearby(self):
        return bool(self.radio_up or self.link_sessions or self.link_errors or self.link_other or self.peer_gone
                    or self.peer_gone_reasons)


def read_lines(path):
    try:
        data = pathlib.Path(path).read_bytes()
    except OSError as exc:
        raise SetupError(f'cannot read {path}: {exc}')
    lines = data.split(b'\n')
    if lines and lines[-1] == b'':
        lines.pop()
    return [line.rstrip(b'\r').decode('utf-8', errors='replace') for line in lines]


def add_line(device, when_stamp, text):
    """Fold one device line (its text without the merged prefix) into the device's figures."""
    if text.startswith(EVENT_TAG + ' '):
        event = text[len(EVENT_TAG) + 1:]
        if event.startswith('connected '):
            device.connected += 1
            device.game = None
        elif event.startswith('lost '):
            device.lost += 1
            device.game = None
        else:
            card = CARD_EVENT.match(event)
            if card:
                device.cards = True
                device.card_files += 1
                device.boots.add(int(card.group(2)))
                if card.group(4) not in device.firmware:
                    device.firmware.append(card.group(4))
                if card.group(5) not in device.boards:
                    device.boards.append(card.group(5))
        return
    millis = MILLIS_PREFIX.match(text)
    when = when_stamp or (f'[{millis.group(1)}]' if millis else '?')
    match = DIAG_DROPPED.search(text)
    if match:
        device.diag_dropped += int(match.group(1))
        return
    match = DIAG_LOST.search(text)
    if match:
        device.diag_lost += int(match.group(1))
        return
    match = LIFECYCLE.search(text)
    if match:
        device.game = match.group(1)
        if match.group(3) == 'PeerGone':
            device.peer_gone.append((when, match.group(1), match.group(4)))
        return
    match = PEER_GONE_REASON.search(text)
    if match:
        reason = match.group(2)
        device.peer_gone_reasons[reason] = device.peer_gone_reasons.get(reason, 0) + 1
        return
    match = LINK_LINE.search(text)
    if match:
        add_link_line(device, when, match.group(1), bool(LINK_ERROR_LINE.search(text)))
        return
    match = NEARBY_ERROR.search(text)
    if match:
        device.link_errors.append((when, match.group(1)))
        return
    match = VM_STOPPED.search(text)
    if match:
        arena, stack, hook = (int(value) for value in match.groups())
        device.stops.append(VmStop(when, device.game or '(unknown)', arena, stack, hook))
        return
    match = DROPPED_BEFORE_HAND_OFF.search(text)
    if match:
        device.game_stats(match.group(1)).dropped_before_hand_off += 1
        return
    match = DROPPED_BEFORE_DISPLAY.search(text)
    if match:
        device.game_stats(match.group(1)).dropped_before_display += 1
        return
    match = HAND_OFF_PUSHED.search(text)
    if match:
        stats = device.game_stats(match.group(1))
        stats.hand_offs += 1
        ms = int(match.group(2))
        stats.largest_hand_off_ms = ms if stats.largest_hand_off_ms is None else max(stats.largest_hand_off_ms, ms)
        return
    if '[MEM]' in text:
        match = MEM_LINE.search(text)
        message = text[millis.end():] if millis else text
        device.mem_lines.append((when, message))
        if match:
            figures = MemFigures(when, *(int(match.group(n)) for n in range(2, 6)))
            (device.psram if match.group(1) else device.heap).add(figures)
        else:
            device.other_mem.append((when, message))


def add_link_line(device, when, message, is_error):
    """Fold one LINK line (its message after the `[LINK] ` tag) into the device's nearby figures."""
    if is_error:
        device.link_errors.append((when, message))
        return
    match = RADIO_UP.search(message)
    if match:
        device.radio_up.append((when, message))
        return
    match = LINK_SESSION.match(message)
    if not match:
        device.link_other.append((when, message))
        return
    link = device.link_session(match.group(1))
    body = match.group(2)
    if (found := LINK_STATE_SENT.match(body)):
        link.state_sent += 1
        link.last_sent_ver = int(found.group(1))
    elif (found := LINK_STATE_RECV.match(body)):
        link.state_recv += 1
        link.last_recv_ver = int(found.group(1))
    elif (found := LINK_STATE_ADOPT.match(body)):
        link.adopted[found.group(3)] = link.adopted.get(found.group(3), 0) + 1
        link.last_adopt_ver = int(found.group(1))
    elif (found := LINK_QUEUE_FULL.match(body)):
        link.queue_full += 1
        link.last_overflows = int(found.group(2))
    elif (found := LINK_ABORT_RECV.match(body)):
        key = (found.group(1), int(found.group(2)))
        link.abort_recv[key] = link.abort_recv.get(key, 0) + 1
    elif (found := LINK_LEAVE.match(body)):
        link.leaves.append((when, found.group(1), *(int(found.group(n)) for n in (2, 3, 4))))
    elif (found := LINK_COUNTER.match(body)):
        link.counters[found.group(1)] = int(found.group(3))  # the running total as the firmware last logged it
    elif (found := LINK_TASK_STARTED.match(body)):
        link.stack_bytes = int(found.group(1))
    elif (found := LINK_TASK_STOPPED.match(body)):
        link.high_water.append((when, int(found.group(1))))
    else:
        link.other.append((when, body))


def read_logs(paths):
    """DeviceStats of every device in the logs, in first-seen order."""
    devices = collections.OrderedDict()

    def device(name):
        if name not in devices:
            devices[name] = DeviceStats(name)
        return devices[name]

    def claim(current, path):
        """One log per device: a name found in a second file would count every figure twice."""
        if current.sources and str(path) not in current.sources:
            raise SetupError(f'device {current.name} is in both {current.sources[0]} and {path}; '
                             'give one log per device (its raw log or merged.log, not both)')
        if not current.sources:
            current.sources.append(str(path))

    for path in paths:
        stem = pathlib.Path(path).name
        stem = stem[:-len(RAW_SUFFIX)] if stem.endswith(RAW_SUFFIX) else pathlib.Path(path).stem
        lines = read_lines(path)
        parsed = [(MERGED_PREFIX.fullmatch(line), line) for line in lines]
        if not any(match for match, _ in parsed):
            claim(device(stem), path)
        for match, line in parsed:
            if match:
                stamp, name, text = match.groups()
                current = device(name)
                current.merged = True
                claim(current, path)
                add_line(current, stamp, text)
            else:
                current = device(stem)
                claim(current, path)
                add_line(current, None, line)
    return devices


def mem_text(figures):
    return (f'Free {figures.free} B, Total {figures.total} B, Min Free {figures.min_free} B, '
            f'MaxAlloc {figures.max_alloc} B at {figures.when}')


def mem_report(label, series):
    if not series.count:
        return [f'  [MEM] {label}: none']
    return [
        f'  [MEM] {label}: {series.count} lines',
        f'    first: {mem_text(series.first)}',
        f'    last: {mem_text(series.last)}',
        f'    smallest Min Free: {series.least_free.min_free} B at {series.least_free.when}',
        f'    smallest MaxAlloc: {series.least_max_alloc.max_alloc} B at {series.least_max_alloc.when}',
    ]


def nearby_report(device):
    """The summary lines of a nearby match: the radio, each LINK session, the PeerGone entries."""
    if not device.has_nearby():
        return ['  nearby: none']
    device_stack = next((link.stack_bytes for link in device.link_sessions.values() if link.stack_bytes), None)
    lines = [f'  nearby: radio up {len(device.radio_up)}, LINK sessions {len(device.link_sessions)}, '
             f'PeerGone {len(device.peer_gone)}, LINK errors {len(device.link_errors)}']
    for when, message in device.radio_up:
        lines.append(f'    {when} {message}')
    for session in device.link_sessions.values():
        lines.append(f'    session {session.session}:')
        lines.append(f'      STATE sent {session.state_sent} (last ver {fmt(session.last_sent_ver)}), '
                     f'received {session.state_recv} (last ver {fmt(session.last_recv_ver)})')
        adopted = ', '.join(f'{result} {count}' for result, count in session.adopted.items()) or 'none'
        lines.append(f'      adopted: {adopted} (last ver {fmt(session.last_adopt_ver)})')
        lines.append(f'      queue full {session.queue_full} (overflows {fmt(session.last_overflows)})')
        lines.append('      ' + ', '.join(f'{name} {total}' for name, total in session.counters.items())
                     + ' (the firmware\'s running totals)')
        aborts = ', '.join(f'{reason} from seat {seat} x{count}' for (reason, seat), count in session.abort_recv.items())
        lines.append(f'      ABORT received (one line per copy the device heard): {aborts or "none"}')
        for when, reason, waited, unacked, copies in session.leaves:
            lines.append(f'      leave at {when}: reason {reason}, waited {waited} ms, unacked {unacked}, '
                         f'ABORT copies {copies}')
        if not session.leaves:
            lines.append('      leave: none')
        # A guest's task starts before it knows the host's session id (00000000) and stops under the real one, so a
        # session with no `task started` line of its own shows the device's, and the 00000000 one has no stop line: the
        # task did stop, under the other id.
        stops = ', '.join(f'{free} B free at {when}' for when, free in session.high_water) or 'no stop under this id'
        stack = session.stack_bytes or device_stack
        lines.append(f'      GameLink task: stack {fmt(stack, " B")}, high-water {stops}')
        for when, text in session.other:
            lines.append(f'      other: {text} at {when}')
    frees = [free for session in device.link_sessions.values() for _, free in session.high_water]
    if frees:
        lines.append(f'    GameLink task stack: least high-water {min(frees)} B free of {fmt(device_stack, " B")} '
                     f'over {len(frees)} stop(s)')
    for when, game, event in device.peer_gone:
        lines.append(f'    PeerGone at {when}: {game} on {event}')
    for reason, count in device.peer_gone_reasons.items():
        lines.append(f'    the other player is gone: {reason} x{count}')
    for when, message in device.link_errors:
        lines.append(f'    LINK error at {when}: {message}')
    for when, message in device.link_other:
        lines.append(f'    LINK at {when}: {message}')
    return lines


def fmt(value, unit=''):
    return '-' if value is None else f'{value}{unit}'


def summarize(devices, mem_all=False):
    """The summary text of the devices from read_logs."""
    lines = []
    firmware = {device.name: ', '.join(device.firmware) for device in devices.values() if device.cards}
    if len(set(firmware.values())) > 1:
        lines.append('WARNING: the devices ran different firmware: '
                     + '; '.join(f'{name} {text}' for name, text in firmware.items()))
        lines.append('')
    for device in devices.values():
        lines.append(f'== {device.name} ({", ".join(device.sources)}) ==')
        if device.cards:
            lines.append(f'  card: {device.card_files} file(s), boots {len(device.boots)}, '
                         f'firmware {", ".join(device.firmware)}, board {", ".join(device.boards)}')
            lines.append(f'  diagnostics log: {device.diag_dropped} lines dropped (buffer full), '
                         f'{device.diag_lost} bytes lost (card write failed)')
        elif device.merged:
            lines.append(f'  connected {device.connected}, lost {device.lost}')
        lines.append(f'  VM stopped: {len(device.stops)}')
        for stop in device.stops:
            lines.append(f'    {stop.when} {stop.game}: arena peak {stop.arena} B, '
                         f'stack high-water {stop.stack} B free, least at a hook {stop.hook} B')
        lines.append('  touches and hand-offs per game:' + ('' if device.games else ' none'))
        for game, stats in device.games.items():
            largest = '-' if stats.largest_hand_off_ms is None else f'{stats.largest_hand_off_ms} ms'
            lines.append(f'    {game}: dropped before the frame was on the panel {stats.dropped_before_display}, '
                         f'dropped before the hand-off passed {stats.dropped_before_hand_off}, '
                         f'hand-off screens pushed {stats.hand_offs}, largest {largest}')
        lines += nearby_report(device)
        lines += mem_report('heap', device.heap)
        lines += mem_report('PSRAM', device.psram)
        lines.append(f'  [MEM] other: {len(device.other_mem)} lines')
        if device.other_mem:
            lines.append(f'    first: {device.other_mem[0][1]} at {device.other_mem[0][0]}')
            lines.append(f'    last: {device.other_mem[-1][1]} at {device.other_mem[-1][0]}')
        if mem_all:
            lines.append(f'  every [MEM] line: {len(device.mem_lines)}')
            lines += [f'    {when} {message}' for when, message in device.mem_lines]
        lines.append('')
    return '\n'.join(lines)


def summary(paths, mem_all=False):
    if not paths:
        raise SetupError('give at least one log file')
    print(summarize(read_logs(paths), mem_all), end='')
    return fork_common.PASS


# --- merge: the logs two devices wrote to their SD cards -------------------------------------------------------------

SD_MAGIC = '# crosshatch-diag-log 1'  # the first line of every card file (lib/DiagLog/DiagLogWriter.h writes the header)
SD_DIR_NAME = 'crosshatch-log'  # the folder on the card
SD_HEADER = re.compile(r'# ([a-z_]+) (.*)')
SD_FILE_NAME = re.compile(r'b(\d+)-s(\d+)-')
NO_SESSION = '00000000'  # a guest's link before it knows the host's session id: not an anchor

Anchor = collections.namedtuple('Anchor', 'node millis')


class CardFile:
    """One file the diagnostics log wrote: its header, its raw bytes, and its log lines as (millis or None, bytes)."""

    def __init__(self, path):
        self.path = pathlib.Path(path)
        try:
            self.raw = self.path.read_bytes()
        except OSError as exc:
            raise SetupError(f'cannot read {path}: {exc}')
        lines = self.raw.split(b'\n')
        if lines and lines[-1] == b'':
            lines.pop()
        lines = [line.rstrip(b'\r') for line in lines]
        if not lines or lines[0].decode('utf-8', errors='replace') != SD_MAGIC:
            raise SetupError(f'{path} is not a diagnostics log: its first line is not "{SD_MAGIC}"')
        self.headers = {}
        self.lines = []
        for line in lines[1:]:
            text = line.decode('utf-8', errors='replace')
            header = SD_HEADER.fullmatch(text)
            if header and not self.lines:
                self.headers[header.group(1)] = header.group(2)
            elif text.strip():
                millis = MILLIS_PREFIX.match(text)
                self.lines.append((int(millis.group(1)) if millis else None, line))
        named = SD_FILE_NAME.match(self.path.name)
        self.boot = self.number('boot', int(named.group(1)) if named else 0)
        self.segment = self.number('segment', int(named.group(2)) if named else 0)
        self.device = self.headers.get('device', '')
        if not self.device:
            raise SetupError(f'{path} has no "# device" line in its header')

    def number(self, key, default):
        value = self.headers.get(key, '')
        return int(value) if value.isdigit() else default

    @property
    def opened_ms(self):
        return self.number('opened_ms', self.lines[0][0] if self.lines and self.lines[0][0] is not None else 0)


def card_paths(spec_path):
    """The log files of one CARD argument: a file, or the .log files of a folder (a card's root or its crosshatch-log)."""
    path = pathlib.Path(spec_path)
    try:
        if path.is_dir():
            folder = path / SD_DIR_NAME if (path / SD_DIR_NAME).is_dir() else path
            files = sorted(entry for entry in folder.iterdir() if entry.is_file() and entry.suffix == '.log')
            if not files:
                raise SetupError(f'no .log files in {folder}')
            return files
    except OSError as exc:
        raise SetupError(f'cannot read {spec_path}: {exc}')
    if not path.is_file():
        raise SetupError(f'{spec_path} is neither a log file nor a folder of them')
    return [path]


def read_cards(specs):
    """OrderedDict of device name -> its CardFiles in boot order, from CARD arguments ([NAME=]PATH)."""
    if not specs:
        raise SetupError('give at least one CARD (a log file, or a folder of them)')
    devices = collections.OrderedDict()
    seen = {}
    for spec in specs:
        name, separator, path = spec.partition('=')
        if separator and NAME_PATTERN.fullmatch(name) and path:
            explicit = name
        elif separator and not os.path.exists(spec):
            raise SetupError(f'{spec!r} is not [NAME=]PATH (a name of letters, digits, _ and -, then the path)')
        else:
            explicit, path = None, spec
        for file_path in card_paths(path):
            real = os.path.realpath(file_path)
            if real in seen:
                raise SetupError(f'{file_path} is given twice (also as {seen[real]})')
            seen[real] = file_path
            card = CardFile(file_path)
            name = explicit or re.sub(r'[^A-Za-z0-9_-]', '_', card.device)
            devices.setdefault(name, []).append(card)
    by_device = {}
    for name, cards in devices.items():
        ids = {card.device for card in cards}
        if len(ids) > 1:
            raise SetupError(f'the files named {name} come from devices {", ".join(sorted(ids))}; '
                             'name each card with NAME=PATH')
        (device_id,) = ids
        if device_id in by_device:
            raise SetupError(f'device {device_id} is on both {by_device[device_id]} and {name}; give one card per device')
        by_device[device_id] = name
        cards.sort(key=lambda card: (card.boot, card.segment, str(card.path)))
        keys = [(card.boot, card.segment) for card in cards]
        if len(set(keys)) != len(keys):
            raise SetupError(f'device {name} has two files for the same boot and segment')
    return devices


def state_anchor(text):
    """('sent' or 'recv', session, ver) for a `STATE` LINK line of a known session, else None."""
    link = LINK_LINE.search(text)
    session = LINK_SESSION.match(link.group(1)) if link else None
    if not session or session.group(1) == NO_SESSION:
        return None
    for kind, pattern in (('sent', LINK_STATE_SENT), ('recv', LINK_STATE_RECV)):
        found = pattern.match(session.group(2))
        if found:
            return kind, session.group(1), int(found.group(1))
    return None


def state_sends(nodes, events):
    """({(session, ver): Anchor of the first `state sent`}, set of keys more than one device sent).

    The key names a STATE only if one device sent it; two devices that both logged a send at one (session, ver) make the
    pairing ambiguous, so such a key is left out of the first result and a receive of it is paired with nothing.
    """
    sent, senders = {}, collections.defaultdict(set)
    for node in nodes:
        for millis, text in events[node]:
            anchor = state_anchor(text)
            if anchor and anchor[0] == 'sent':
                key = (anchor[1], anchor[2])
                senders[key].add(node[0])
                sent.setdefault(key, Anchor(node, millis))
    ambiguous = {key for key, names in senders.items() if len(names) > 1}
    return {key: send for key, send in sent.items() if key not in ambiguous}, ambiguous


def align_clocks(nodes, events):
    """Offsets (ms, added to a node's millis) that put every boot's clock on the first device's, and the notes saying how.

    A node is one boot of one device: nodes is [(name, boot)] in device order; events maps a node to its
    (millis, text) lines. The devices share no clock, so the offset comes from the STATEs: the one a device sent at
    `ver` in a `session` is the one the other received. With d_uv the fastest (receive minus send) over the STATEs u sent
    and v received, and d_vu the same the other way, the offset of v over u is (d_vu - d_uv) / 2 and the link latency
    (d_uv + d_vu) / 2; with one direction only the latency is taken as 0. A (session, ver) that two devices both
    logged a send for is ambiguous and anchors nothing.

    A boot is aligned when it shares a STATE pair with another device's boot. The aligned boots are placed first and
    keep their offsets: the reference's component, then each component not reached from it, rooted at its first node,
    which goes after its device's earlier boots (their lengths chained forward from the last one placed, or from 0).
    Then each device's unaligned boots, each saying so: a run of them followed by an aligned boot is chained backwards,
    each to end when the next starts, and a note says by how much one overlaps the aligned boot before the run, whose
    times are kept (the merge's clamp holds the overlapping lines at that boot's last stamp); a run with no aligned boot
    after it is chained forward from the device's previous boot's end, or, for a device's first boot, starts when the
    first device's did. A boot starts at its millis 0 and ends at its last line.
    """
    sent, _ = state_sends(nodes, events)
    received = collections.defaultdict(dict)
    for node in nodes:
        for millis, text in events[node]:
            anchor = state_anchor(text)
            if anchor and anchor[0] == 'recv':
                received[(anchor[1], anchor[2])].setdefault(node, millis)
    fastest = collections.defaultdict(lambda: None)  # (sender node, receiver node) -> least receive - send
    pairs = collections.Counter()
    for key, send in sent.items():
        for node, millis in received.get(key, {}).items():
            if node[0] != send.node[0]:
                edge = (send.node, node)
                delta = millis - send.millis
                fastest[edge] = delta if fastest[edge] is None else min(fastest[edge], delta)
                pairs[edge] += 1
    neighbours = collections.defaultdict(set)
    for sender, receiver in fastest:
        neighbours[sender].add(receiver)
        neighbours[receiver].add(sender)

    offsets, notes = {}, {}

    def walk(root):
        queue = collections.deque([root])
        while queue:
            node = queue.popleft()
            for other in sorted(neighbours[node]):
                if other in offsets:
                    continue
                forward, back = fastest.get((node, other)), fastest.get((other, node))
                if forward is not None and back is not None:
                    shift, latency = (back - forward) / 2, (forward + back) / 2
                    how = f'link latency about {latency:g} ms'
                elif forward is not None:
                    shift, how = -forward, 'one direction only, latency taken as 0'
                else:
                    shift, how = back, 'one direction only, latency taken as 0'
                offsets[other] = offsets[node] + shift
                count = pairs.get((node, other), 0) + pairs.get((other, node), 0)
                notes[other] = (f'clock: offset {offsets[other] - offsets[node]:+g} ms from {node[0]} boot {node[1]}, '
                                f'{count} STATE pair(s), {how}')
                queue.append(other)

    def end(node):
        return max((m for m, _ in events[node]), default=0)

    def boots(node):
        """The device's boots before node and those after it, in boot order."""
        own = [n for n in nodes if n[0] == node[0]]
        return [n for n in own if n[1] < node[1]], [n for n in own if n[1] > node[1]]

    # The aligned components, each walked from its root; every boot in one keeps the offset it gives.
    for node in nodes:
        if node in offsets:
            continue
        earlier = boots(node)[0]
        if node == nodes[0]:
            offsets[node], notes[node] = 0.0, 'clock: reference (the first device\'s first boot)'
        elif not neighbours[node]:
            continue  # unaligned: placed below, once every aligned boot has its offset
        elif earlier:
            # A root the reference does not reach goes after its device's earlier boots: from the end of the last one
            # placed (an earlier aligned boot comes first in node order), through the lengths of the unaligned ones
            # after it, which the backward chain below then puts exactly between.
            start = 0.0
            for before in earlier:
                start = offsets[before] + end(before) if before in offsets else start + end(before)
            offsets[node] = start
            notes[node] = f'clock: not aligned by STATEs; placed after boot {earlier[-1][1]} of the same device'
        else:
            offsets[node] = 0.0
            notes[node] = 'clock: not aligned (no STATE pair with another device); its boot is taken to start with the first'
        walk(node)
    aligned = set(offsets)
    # An unaligned boot followed by a placed one ends when that one starts, chained backwards. When the run is longer
    # than the gap it overlaps the aligned boot before it, and the note says so; that boot's times are kept.
    for node in reversed(nodes):
        if node in offsets:
            continue
        earlier, later = boots(node)
        if not later or later[0] not in offsets:
            continue
        after = later[0]
        offsets[node] = offsets[after] - end(node)
        notes[node] = f'clock: not aligned by STATEs; placed to end when boot {after[1]} of the same device starts'
        kept = [n for n in earlier if n in aligned]
        if kept:
            overlap = offsets[kept[-1]] + end(kept[-1]) - offsets[node]
            if overlap > 0:
                notes[node] += f'; it overlaps boot {kept[-1][1]} by {overlap:.10g} ms, whose times are kept'
    # The unaligned boots with no aligned boot after them: after the device's previous boot, as before.
    for node in nodes:
        if node in offsets:
            continue
        previous = [n for n in boots(node)[0] if n in offsets]
        if previous:
            before = previous[-1]
            offsets[node] = offsets[before] + end(before)
            notes[node] = f'clock: not aligned by STATEs; placed after boot {before[1]} of the same device'
        else:
            offsets[node] = 0.0
            notes[node] = 'clock: not aligned (no STATE pair with another device); its boot is taken to start with the first'
    return offsets, notes


def place(lines, moved):
    """{device: [ms]}: each device's aligned times with the monotone clamp, a line in `moved` ((device, index) -> least ms)
    no earlier than that."""
    times = {}
    for name, items in lines.items():
        latest, placed = 0, []
        for index, (raw, _, _) in enumerate(items):
            latest = max(latest, raw, moved.get((name, index), 0))
            placed.append(latest)
        times[name] = placed
    return times


def merge_cards(specs, out_dir, out=None):
    """Merge the card files into out_dir (merged.log, <NAME>.raw.log, summary.txt) and print the alignment and summary."""
    devices = read_cards(specs)
    out = sys.stdout if out is None else out
    nodes, events, file_marks = [], {}, []
    for name, cards in devices.items():
        for card in cards:
            node = (name, card.boot)
            if node not in events:
                nodes.append(node)
                events[node] = []
            last = events[node][-1][0] if events[node] else card.opened_ms
            for millis, line in card.lines:
                last = last if millis is None else millis
                events[node].append((last, line.decode('utf-8', errors='replace')))
            file_marks.append((node, card, card.opened_ms))
    offsets, notes = align_clocks(nodes, events)

    floor = min((millis + offsets[node] for node in nodes for millis, _ in events[node]), default=0)
    floor = min([floor] + [opened + offsets[node] for node, _, opened in file_marks])
    names = list(devices)
    sends, _ = state_sends(nodes, events)
    # Each device's lines in file order: (aligned ms, node, text). A device's time never goes back.
    lines = {name: [] for name in names}
    send_at, receives = {}, []  # (key -> (device, index) of its send); [(device, index, key)] of the receives
    for name in names:
        for node in (n for n in nodes if n[0] == name):
            for millis, text in events[node]:
                anchor = state_anchor(text)
                if anchor:
                    key = (anchor[1], anchor[2])
                    if anchor[0] == 'sent' and key in sends and sends[key].node[0] == name:
                        send_at.setdefault(key, (name, len(lines[name])))
                    elif anchor[0] == 'recv':
                        receives.append((name, len(lines[name]), key))
                lines[name].append((round(millis + offsets[node] - floor), node, text))
    # A STATE's receive comes after its send: where the estimate puts it at or before, the receive moves to the send's
    # time plus 1 ms and the clamp carries the receiver's later lines along. Repeats until nothing moves (a receive that
    # moves can push a send of its own device later); the bound only stops a log that contradicts itself.
    moved = {}
    for _ in range(len(receives) + 2):
        times = place(lines, moved)
        changed = False
        for name, index, key in receives:
            send = send_at.get(key)
            if send and send[0] != name and times[name][index] <= times[send[0]][send[1]]:
                moved[(name, index)] = times[send[0]][send[1]] + 1
                changed = True
        if not changed:
            break
    times = place(lines, moved)
    entries = []  # (aligned ms, device index, sequence, bytes); a card file's marks have sequences below 0
    first_time = {}
    for index, name in enumerate(names):
        for sequence, ((_, node, text), when) in enumerate(zip(lines[name], times[name])):
            first_time.setdefault(node, when)
            entries.append((when, index, sequence, text.encode()))
    for node, card, opened in file_marks:
        index = names.index(node[0])
        text = (f'{EVENT_TAG} card file {card.path.name}: boot {card.boot}, segment {card.segment}, '
                f'firmware {card.headers.get("firmware", "unknown")}, board {card.headers.get("board", "unknown")}')
        entries.append((max(0, round(opened + offsets[node] - floor)), index, -1, text.encode()))
    for node in nodes:
        entries.append((first_time.get(node, 0), names.index(node[0]), -2, f'{EVENT_TAG} {notes[node]}'.encode()))
    entries.sort(key=lambda entry: entry[:3])

    out_path = pathlib.Path(out_dir)
    try:
        out_path.mkdir(parents=True, exist_ok=True)
        merged = b''.join(format_stamp(ms).encode() + b' ' + names[index].encode() + b' ' + text + b'\n'
                          for ms, index, _, text in entries)
        (out_path / MERGED_LOG).write_bytes(merged)
        for name, cards in devices.items():
            (out_path / f'{name}{RAW_SUFFIX}').write_bytes(b''.join(card.raw for card in cards))
        report = '\n'.join(f'{node[0]} boot {node[1]}: {notes[node]}' for node in nodes) + '\n\n'
        report += summarize(read_logs([str(out_path / MERGED_LOG)]))
        (out_path / 'summary.txt').write_text(report)
    except OSError as exc:
        raise SetupError(f'cannot write the logs in {out_path}: {exc}')
    print(report, end='', file=out)
    return fork_common.PASS


# --- command line ----------------------------------------------------------------------------------------------

@contextlib.contextmanager
def stop_on_signals(stop):
    """Set stop on SIGINT and SIGTERM while inside; a no-op off the main thread (signals only reach that one)."""
    if threading.current_thread() is not threading.main_thread():
        yield
        return
    previous = {}

    def restore():
        for sig, handler in previous.items():
            if handler is not None:
                signal.signal(sig, handler)

    def on_signal(*_):
        # The first signal asks for a clean stop; the next one acts as it did before, so a stuck loop can be killed.
        stop.set()
        restore()

    for sig in (signal.SIGINT, signal.SIGTERM):
        previous[sig] = signal.signal(sig, on_signal)
    try:
        yield
    finally:
        restore()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    capture_parser = sub.add_parser('capture', help='read the ports into a merged log and a raw log per device')
    capture_parser.add_argument('--port', action='append', default=[], metavar='NAME=PATH',
                                help='a device name and its serial port path; give it once per device')
    capture_parser.add_argument('--out', required=True, metavar='DIR', help='the directory for the logs')
    capture_parser.add_argument('--baud', type=int, default=DEFAULT_BAUD, help=f'default {DEFAULT_BAUD}')
    capture_parser.add_argument('--duration', type=float, metavar='SECONDS', help='stop after this long')
    merge_parser = sub.add_parser('merge', help='merge the logs two devices wrote to their SD cards')
    merge_parser.add_argument('--out', required=True, metavar='DIR', help='the directory for merged.log and the rest')
    merge_parser.add_argument('cards', nargs='+', metavar='[NAME=]CARD',
                              help='a log file a diagnostics build wrote, or a folder of them; one per device')
    summary_parser = sub.add_parser('summary', help='list the device-run packet\'s figures from device logs')
    summary_parser.add_argument('--mem-all', action='store_true', help='print every [MEM] line')
    summary_parser.add_argument('logs', nargs='*', metavar='LOG')
    args = parser.parse_args(argv)

    def step():
        if args.command == 'summary':
            return summary(args.logs, args.mem_all)
        if args.command == 'merge':
            return merge_cards(args.cards, args.out)
        ports = parse_ports(args.port)
        if args.duration is not None and not args.duration > 0:  # also rejects nan
            raise SetupError('--duration must be above 0')
        stop = threading.Event()
        with stop_on_signals(stop):
            return capture(ports, args.out, args.baud, stop, duration=args.duration)

    return fork_common.exit_code(step)


if __name__ == '__main__':
    sys.exit(main())
