#!/usr/bin/env python3
"""
Capture the serial logs of two or more devices at once into one merged log, and summarize a device log.

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
  summary [--mem-all] LOG [LOG ...]
              per device, from a raw device log or the merged log: connects and losses, each `VM stopped` with its
              game, dropped touches and hand-offs per game, the `[MEM]` figures (device-run-packet.md L1 to L3), and
              a nearby match's lines: the radio bring-ups, each `LINK` session's `STATE`s, resends, silences, `ABORT`s,
              leaves and `GameLink` task stack, the `PeerGone` entries and their reasons (epic-play-nearby R13)

Exit 0: done. 1: not used (no rule is checked). 2: the command could not run (a bad argument, an output directory
that cannot be made, an unreadable log).

Local run (a computer with two USB ports; docs/crosshatch/fork-scripts.md has the walkthrough):
    python3 scripts/device_log.py capture --port A=/dev/serial/by-id/usb-A --port B=/dev/serial/by-id/usb-B --out logs
    python3 scripts/device_log.py summary logs/merged.log
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
        return
    millis = MILLIS_PREFIX.match(text)
    when = when_stamp or (f'[{millis.group(1)}]' if millis else '?')
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
        stops = ', '.join(f'{free} B free at {when}' for when, free in session.high_water) or 'not stopped'
        # A guest's task starts before it knows the host's session id (00000000) and stops under the real one, so a
        # session with no `task started` line of its own shows the device's.
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
    for device in devices.values():
        lines.append(f'== {device.name} ({", ".join(device.sources)}) ==')
        if device.merged:
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
    summary_parser = sub.add_parser('summary', help='list the device-run packet\'s figures from device logs')
    summary_parser.add_argument('--mem-all', action='store_true', help='print every [MEM] line')
    summary_parser.add_argument('logs', nargs='*', metavar='LOG')
    args = parser.parse_args(argv)

    def step():
        if args.command == 'summary':
            return summary(args.logs, args.mem_all)
        ports = parse_ports(args.port)
        if args.duration is not None and not args.duration > 0:  # also rejects nan
            raise SetupError('--duration must be above 0')
        stop = threading.Event()
        with stop_on_signals(stop):
            return capture(ports, args.out, args.baud, stop, duration=args.duration)

    return fork_common.exit_code(step)


if __name__ == '__main__':
    sys.exit(main())
