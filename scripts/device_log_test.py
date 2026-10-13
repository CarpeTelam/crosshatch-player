#!/usr/bin/env python3
"""
Tests for device_log.py. Standard library only, no serial device:

    python3 scripts/device_log_test.py [-v]

The ports are pseudo-terminal pairs behind symlinks in a temp directory: the symlink is the port path, removing it and
closing the master is the port vanishing, and a new pair swapped in by os.replace is the port returning. The tests wait
for what they expect with wait_for (a polled condition with a timeout), never a fixed sleep.

What stands in for what, and how far it can be trusted: the pty stands in for the device's USB CDC port, and the sample
log for its output. A test below pins that every format string the summary parses is still literally in its firmware
source (read from the working tree). The pty is more permissive than the device: its loss is an immediate EIO and it
has no sleep timing, so the reconnect against a real X4 Pro's port is unmeasured until a device run.
"""

import contextlib
import errno
import io
import os
import pathlib
import re
import signal
import subprocess
import sys
import tempfile
import termios
import threading
import time
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
SCRIPT = HERE / 'device_log.py'
sys.path.insert(0, str(HERE))

import device_log  # noqa: E402

WAIT_SECONDS = 15
STAMP = r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z'

# The firmware's format strings the summary parses, and the file each is in. The sample logs are made by filling these in,
# so a reworded firmware line shows up in test_firmware_formats_are_still_in_the_sources and not only on a device.
FORMATS = {
    'prefix': ('lib/Logging/Logging.cpp', '[%lu] [%s] [%s] '),
    'lifecycle': ('src/activities/games/GameMatchActivity.cpp', '%s: %s -> %s on %s'),
    'stopped': ('src/games/GameVM.cpp',
                'VM stopped; arena peak %u bytes, stack high-water %u bytes free, least at a hook %u bytes'),
    'drop_hand_off': ('src/activities/games/GameMatchActivity.cpp',
                      '%s: dropped a touch that began %d ms before the hand-off passed'),
    'drop_display': ('src/activities/games/GameMatchActivity.cpp', '%s: dropped a touch before the frame was on the panel'),
    'hand_off': ('src/activities/games/GameMatchActivity.cpp', '%s: hand-off screen pushed in %u ms'),
    'mem': ('src/main.cpp', 'Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes'),
    'psram': ('src/main.cpp', 'PSRAM: Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes'),
    'web_mem': ('src/network/CrossPointWebServer.cpp', '[MEM] Free heap before begin: %d bytes'),
    # A nearby match (epic-play-nearby): GameLinkLog.h's one function per LINK line, the radio's, and the match's.
    'link_state_sent': ('src/games/GameLinkLog.h', 'session=%08x state sent ver=%u'),
    'link_state_recv': ('src/games/GameLinkLog.h', 'session=%08x state recv ver=%u seat=%u'),
    'link_state_adopt': ('src/games/GameLinkLog.h', 'session=%08x state adopt ver=%u seat=%u result=%s'),
    'link_queue_full': ('src/games/GameLinkLog.h', 'session=%08x queue full ver=%u overflows=%u'),
    'link_abort_recv': ('src/games/GameLinkLog.h', 'session=%08x abort recv reason=%s seat=%u'),
    'link_leave': ('src/games/GameLinkLog.h', 'session=%08x leave reason=%s waited=%ums unacked=%u copies=%u'),
    'link_resend': ('src/games/GameLinkLog.h', 'session=%08x resend n=%u total=%u'),
    'link_silence': ('src/games/GameLinkLog.h', 'session=%08x silence n=%u total=%u'),
    'link_abort_sent': ('src/games/GameLinkLog.h', 'session=%08x abort sent n=%u total=%u'),
    'link_refused': ('src/games/GameLinkLog.h', 'session=%08x link refused n=%u total=%u'),
    'link_task_started': ('src/games/GameLinkLog.h', 'session=%08x task started; stack %u bytes'),
    'link_task_stopped': ('src/games/GameLinkLog.h', 'session=%08x task stopped; stack high-water %u bytes free'),
    'link_bring_up_failed': ('src/games/GameLinkLog.h', 'session=%08x bring-up failed: %s'),
    'radio_up': ('src/games/EspNowLink.h', 'radio up on channel %u as %02X:%02X:%02X:%02X:%02X:%02X'),
    'radio_failed': ('src/games/EspNowLink.h', 'bring-up failed at %s'),
    'udp_up': ('src/games/UdpLink.cpp', 'udp link up on %u'),
    'peer_gone_reason': ('src/activities/games/GameMatchActivity.cpp', '%s: the other player is gone: %s'),
    # The lobby's own failures, logged at LOG_ERR under GAME (there are six, all `Nearby: ...`; the radio's is pinned).
    'lobby_radio_failed': ('src/activities/games/GameLobbyActivity.cpp', 'Nearby: the radio did not start for %s'),
}
# The LINK formats that are logged at LOG_INF (the other two, bring-up failures, at LOG_ERR).
LINK_INF_KINDS = ('link_state_sent', 'link_state_recv', 'link_state_adopt', 'link_queue_full', 'link_abort_recv',
                  'link_leave', 'link_resend', 'link_silence', 'link_abort_sent', 'link_refused', 'link_task_started',
                  'link_task_stopped', 'radio_up', 'udp_up')
LINK_ERR_KINDS = ('link_bring_up_failed', 'radio_failed')


def device_line(millis, level, origin, kind, *values):
    """A raw device line as the firmware would print it: its prefix, then the message of one of FORMATS."""
    message = FORMATS[kind][1].replace('%zu', '%d').replace('%u', '%d').replace('%lu', '%d')
    return '[%d] [%s] [%s] ' % (millis, level, origin) + message % values


def wait_for(predicate, timeout=WAIT_SECONDS, what='the condition'):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.005)
    raise AssertionError(f'timed out waiting for {what}')


class Pty:
    """A pseudo-terminal pair; the test writes to the master, the slave is the device's port."""

    def __init__(self, link):
        self.master, self.slave = os.openpty()
        self.real = os.ttyname(self.slave)
        self.link = pathlib.Path(link)
        self.place()

    def place(self):
        """Point the port path at this pty, replacing any symlink there at once."""
        spare = self.link.with_name(self.link.name + '.new')
        if spare.is_symlink():
            spare.unlink()
        os.symlink(self.real, spare)
        os.replace(spare, self.link)

    def send(self, data):
        os.write(self.master, data)

    def vanish(self):
        """The port goes away: the path disappears and the device end closes."""
        self.link.unlink()
        self.close()

    def close(self):
        for fd in (self.master, self.slave):
            with contextlib.suppress(OSError):
                os.close(fd)


class Run:
    """capture() in a thread with a stop event, over an output directory."""

    def __init__(self, test, ports, **kwargs):
        self.out = test.out
        self.err = kwargs.pop('err', None) or io.StringIO()
        self.stop_event = threading.Event()
        self.result = []
        self.error = []

        def target():
            try:
                self.result.append(device_log.capture(ports, self.out, stop=self.stop_event, err=self.err, **kwargs))
            except BaseException as exc:  # reported by stop()
                self.error.append(exc)

        self.thread = threading.Thread(target=target, daemon=True)
        self.thread.start()
        test.addCleanup(self.stop)

    def stop(self):
        self.stop_event.set()
        self.thread.join(WAIT_SECONDS)
        if self.thread.is_alive():
            raise AssertionError('capture did not stop')
        if self.error:
            raise self.error.pop()
        return self.result[-1] if self.result else None

    def merged(self):
        path = self.out / 'merged.log'
        return path.read_bytes() if path.exists() else b''

    def raw(self, name):
        path = self.out / f'{name}.raw.log'
        return path.read_bytes() if path.exists() else b''


class CaptureBase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.out = self.root / 'out'
        self.ptys = []
        # A port that does not open is tried again at once, so a test sees several tries in a moment.
        retry = mock.patch.object(device_log, 'RETRY_SECONDS', 0.02)
        retry.start()
        self.addCleanup(retry.stop)

    def tearDown(self):
        for pty in self.ptys:
            pty.close()

    def pty(self, name):
        pty = Pty(self.root / name)
        self.ptys.append(pty)
        return pty

    def start(self, *names, **kwargs):
        ptys = [self.pty(name) for name in names]
        run = Run(self, [(name, str(pty.link)) for name, pty in zip(names, ptys)], **kwargs)
        for name, pty in zip(names, ptys):
            wait_for(lambda: f' {name} [device_log] connected {pty.link}'.encode() in run.merged(), what=f'{name} connected')
        return run, ptys

    def say(self, run, name, pty, text):
        """Send text from a device and wait until its last complete line is in the merged log."""
        pty.send(text)
        last = text.rstrip(b'\r\n').split(b'\n')[-1].rstrip(b'\r')
        wait_for(lambda: f' {name} '.encode() + last + b'\n' in run.merged(), what=f'{name} line {last!r}')

    def merged_lines(self, run):
        return run.merged().split(b'\n')[:-1]


class CaptureTest(CaptureBase):
    def test_two_ports_interleave_in_host_time_order_and_raw_logs_are_unchanged(self):
        run, (a, b) = self.start('A', 'B')
        sent = {'A': b'', 'B': b''}
        for name, pty, text in [('A', a, b'a1\n'), ('B', b, b'b1\n'), ('A', a, b'a2\n'), ('B', b, b'b2\n'),
                                ('A', a, b'a3\n')]:
            self.say(run, name, pty, text)
            sent[name] += text
        lines = [line for line in self.merged_lines(run) if b'[device_log]' not in line]
        self.assertEqual(len(lines), 5)
        stamps = []
        for line, (name, text) in zip(lines, [('A', 'a1'), ('B', 'b1'), ('A', 'a2'), ('B', 'b2'), ('A', 'a3')]):
            match = re.fullmatch(rb'(' + STAMP.encode() + rb') ' + name.encode() + b' ' + text.encode(), line)
            self.assertTrue(match, line)
            stamps.append(match.group(1))
        self.assertEqual(stamps, sorted(stamps))
        run.stop()
        self.assertEqual(run.raw('A'), sent['A'])
        self.assertEqual(run.raw('B'), sent['B'])
        self.assertEqual(run.merged().count(b'[device_log] connected'), 2)

    def test_raw_log_keeps_every_byte_and_merged_log_drops_one_cr(self):
        run, (a,) = self.start('A')
        body = b'one\r\n' + b'\xff\x00bin\xfe\n' + b'two\r\r\n'
        a.send(body)
        wait_for(lambda: b' A two\r\n' in run.merged(), what='the last line')
        tail = b'no newline at the end'
        a.send(tail)
        wait_for(lambda: run.raw('A') == body + tail, what='the tail in the raw log')
        run.stop()
        self.assertEqual(run.raw('A'), body + tail)
        lines = [re.sub(rb'^' + STAMP.encode() + rb' A ', b'', line) for line in self.merged_lines(run)]
        self.assertEqual(lines[1:], [b'one', b'\xff\x00bin\xfe', b'two\r', tail])  # lines[0] is the connected event

    def test_the_stamp_never_goes_below_the_previous_one(self):
        ticks = iter([1_000_000.5, 1_000_000.2, 1_000_000.9])
        run, (a,) = self.start('A', clock=lambda: next(ticks, 1_000_001.0))
        self.say(run, 'A', a, b'x\n')
        run.stop()
        stamps = [line.split(b' ')[0] for line in self.merged_lines(run)]
        self.assertEqual(stamps, sorted(stamps))
        self.assertEqual(stamps[0], b'1970-01-12T13:46:40.500Z')  # the connected event took the first tick
        self.assertEqual(stamps[1], b'1970-01-12T13:46:40.500Z')  # the clock went back; the stamp held

    def test_a_port_vanishes_and_returns_while_the_other_keeps_flowing(self):
        run, (a, b) = self.start('A', 'B')
        self.say(run, 'A', a, b'a1\n')
        self.say(run, 'B', b, b'b1\n')
        a.send(b'half')
        wait_for(lambda: run.raw('A') == b'a1\nhalf', what='the half line in the raw log')
        path = str(a.link)
        a.vanish()
        wait_for(lambda: b' A [device_log] lost ' in run.merged(), what='the loss')
        self.say(run, 'B', b, b'b2\n')  # B keeps flowing while A is away
        a2 = self.pty('A')
        wait_for(lambda: run.merged().count(b' A [device_log] connected ') == 2, what='A back')
        self.say(run, 'A', a2, b'a2\n')
        self.say(run, 'B', b, b'b3\n')
        run.stop()
        text = run.merged().decode()
        order = [text.index(marker) for marker in (' A a1\n', ' A half\n', ' A [device_log] lost ' + path,
                                                  ' B b2\n', ' A [device_log] connected ', ' A a2\n')]
        self.assertEqual(order[:3], sorted(order[:3]))
        self.assertLess(order[2], order[3])  # B's line while A was lost comes after the loss line
        self.assertRegex(text, re.escape(path) + r': (read failed|the port closed)[^\n]*; waiting for it to come back\n')
        self.assertEqual(text.count(' A [device_log] lost '), 1)
        self.assertEqual(text.count(' B [device_log] '), 1)
        stamps = [line.split(' ')[0] for line in text.splitlines()]
        self.assertEqual(stamps, sorted(stamps))
        # The raw log is only what the port sent: the half line and the next line run together, nothing is added.
        self.assertEqual(run.raw('A'), b'a1\nhalfa2\n')
        self.assertEqual(run.raw('B'), b'b1\nb2\nb3\n')
        self.assertIn(' A [device_log] lost ', run.err.getvalue())

    def test_a_port_that_appears_only_later(self):
        link = self.root / 'late'
        calls = []
        real_open = device_log.open_port

        def counting_open(path, speed):
            calls.append(path)
            return real_open(path, speed)

        with mock.patch.object(device_log, 'open_port', counting_open):
            run = Run(self, [('A', str(link))])
            wait_for(lambda: len(calls) >= 4, what='several tries')
            self.assertEqual(run.err.getvalue().count('cannot open'), 1)  # one note for the one reason
            self.assertEqual(run.merged(), b'')
            late = Pty(link)
            self.ptys.append(late)
            wait_for(lambda: b' A [device_log] connected ' in run.merged(), what='A connected')
            self.say(run, 'A', late, b'hello\n')
            run.stop()
        self.assertEqual(run.raw('A'), b'hello\n')

    def test_a_path_that_is_not_a_tty_waits_and_retries_without_crashing(self):
        calls = []
        real_open = device_log.open_port

        def counting_open(path, speed):
            calls.append(path)
            return real_open(path, speed)

        regular = self.root / 'regular'
        regular.write_text('not a port\n')
        with mock.patch.object(device_log, 'open_port', counting_open):
            run = Run(self, [('N', '/dev/null'), ('F', str(regular))])
            wait_for(lambda: len(calls) >= 8, what='several tries')
            self.assertEqual(run.stop(), 0)
        err = run.err.getvalue()
        self.assertEqual(err.count('N /dev/null: not a serial port'), 1)
        self.assertEqual(err.count(f'F {regular}: not a serial port'), 1)
        self.assertEqual(run.merged(), b'')

    def test_duration_ends_the_capture_by_itself(self):
        started = time.monotonic()
        run = Run(self, [('A', str(self.root / 'never'))], duration=0.2)
        run.thread.join(WAIT_SECONDS)
        self.assertFalse(run.thread.is_alive())
        self.assertGreaterEqual(time.monotonic() - started, 0.2)
        self.assertEqual(run.result, [0])

    def test_a_half_line_over_the_limit_is_flushed_as_a_line(self):
        run, (a,) = self.start('A')
        a.send(b'x' * (device_log.MAX_PARTIAL_LINE + 10))
        wait_for(lambda: len(run.raw('A')) == device_log.MAX_PARTIAL_LINE + 10, what='the bytes')
        wait_for(lambda: b'x' * device_log.MAX_PARTIAL_LINE in run.merged(), what='the flushed line')
        run.stop()
        self.assertEqual(run.raw('A'), b'x' * (device_log.MAX_PARTIAL_LINE + 10))

    def test_capture_appends_to_logs_already_there(self):
        self.out.mkdir()
        (self.out / 'A.raw.log').write_bytes(b'earlier\n')
        run, (a,) = self.start('A')
        self.say(run, 'A', a, b'now\n')
        run.stop()
        self.assertEqual(run.raw('A'), b'earlier\nnow\n')

    def test_unwritable_output_directory_is_setup_error(self):
        blocker = self.root / 'file'
        blocker.write_text('x')
        with self.assertRaises(device_log.SetupError):
            device_log.capture([('A', '/dev/null')], blocker / 'sub', stop=threading.Event(), err=io.StringIO())


class BrokenStream:
    """A stderr that is gone (closed pipe)."""

    def write(self, text):
        raise BrokenPipeError(errno.EPIPE, 'Broken pipe')

    def flush(self):
        raise BrokenPipeError(errno.EPIPE, 'Broken pipe')


class ShortWriter:
    """A log file that takes at most `limit` bytes per write, as an unbuffered file may."""

    def __init__(self, limit):
        self.limit = limit
        self.data = b''

    def write(self, view):
        taken = bytes(view[:self.limit])
        self.data += taken
        return len(taken)


class FailingWriter:
    name = 'full.log'

    def write(self, view):
        raise OSError(errno.ENOSPC, 'No space left on device')


class WriteFailureTest(CaptureBase):
    """Log writes that come up short or fail, and notes that go nowhere."""

    def test_write_all_finishes_a_short_write_and_refuses_a_zero_write(self):
        stream = ShortWriter(3)
        device_log.write_all(stream, b'0123456789')
        self.assertEqual(stream.data, b'0123456789')
        with self.assertRaises(OSError):
            device_log.write_all(ShortWriter(0), b'x')

    def test_the_logs_take_short_writes_whole(self):
        raw, merged = ShortWriter(2), ShortWriter(5)
        device = device_log.Device('A', '/dev/x', raw)
        capture = device_log.Capture([device], merged, device_log.Stamper(lambda: 1.0), io.StringIO())
        capture.feed(device, b'hello\nworld\n')
        self.assertEqual(raw.data, b'hello\nworld\n')
        self.assertEqual(merged.data.count(b'\n'), 2)
        self.assertIn(b' A hello\n', merged.data)
        self.assertIn(b' A world\n', merged.data)

    def test_a_full_disk_is_a_setup_error_and_the_final_flush_does_not_mask_it(self):
        device = device_log.Device('A', '/dev/x', ShortWriter(100))
        capture = device_log.Capture([device], FailingWriter(), device_log.Stamper(lambda: 1.0), io.StringIO())
        with self.assertRaisesRegex(device_log.SetupError, r'cannot write the logs \(full.log\).*No space left'):
            capture.feed(device, b'a line\n')
        device.partial += b'half'
        capture.close(quiet=True)  # raises nothing: the error being raised stays the one the user sees
        device.partial += b'half'
        with self.assertRaises(device_log.SetupError):
            capture.close()

    def test_capture_with_an_unwritable_log_exits_2_through_main(self):
        pty = self.pty('A')
        with mock.patch.object(device_log, 'write_all', side_effect=OSError(errno.ENOSPC, 'No space left on device')):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                code = device_log.main(['capture', '--port', f'A={pty.link}', '--out', str(self.out), '--duration', '5'])
        self.assertEqual(code, 2)
        self.assertIn('error: cannot write the logs', err.getvalue())

    def test_a_broken_stderr_does_not_stop_the_capture(self):
        run, (a,) = self.start('A', err=BrokenStream())
        a.vanish()
        wait_for(lambda: b' A [device_log] lost ' in run.merged(), what='the loss')
        b = self.pty('A')
        wait_for(lambda: run.merged().count(b' A [device_log] connected ') == 2, what='A back')
        self.say(run, 'A', b, b'still here\n')
        self.assertEqual(run.stop(), 0)

    def test_a_broken_stderr_does_not_stop_the_notes_about_a_port_that_does_not_open(self):
        calls = []
        real_open = device_log.open_port

        def counting_open(path, speed):
            calls.append(path)
            return real_open(path, speed)

        with mock.patch.object(device_log, 'open_port', counting_open):
            run = Run(self, [('N', '/dev/null')], err=BrokenStream())
            wait_for(lambda: len(calls) >= 4, what='several tries')
            self.assertEqual(run.stop(), 0)


class PortSettingsTest(CaptureBase):
    def test_the_port_is_raw_8n1_at_the_baud_with_hangup_off(self):
        pty = self.pty('A')
        attrs = termios.tcgetattr(pty.slave)
        attrs[2] |= termios.HUPCL
        attrs[3] |= termios.ICANON | termios.ECHO
        termios.tcsetattr(pty.slave, termios.TCSANOW, attrs)
        before = termios.tcgetattr(pty.slave)
        self.assertTrue(before[2] & termios.HUPCL and before[3] & termios.ICANON and before[3] & termios.ECHO)
        run = Run(self, [('A', str(pty.link))], baud=9600)
        wait_for(lambda: b' A [device_log] connected ' in run.merged(), what='A connected')
        iflag, oflag, cflag, lflag, ispeed, ospeed, cc = termios.tcgetattr(pty.slave)
        speed = device_log.baud_constant(9600)
        self.assertNotEqual(speed, device_log.baud_constant(device_log.DEFAULT_BAUD))
        self.assertEqual((ispeed, ospeed), (speed, speed))
        self.assertFalse(cflag & termios.HUPCL)
        self.assertEqual(cflag & termios.CSIZE, termios.CS8)
        self.assertFalse(cflag & (termios.PARENB | termios.CSTOPB))
        self.assertTrue(cflag & termios.CLOCAL)
        self.assertFalse(lflag & (termios.ICANON | termios.ECHO | termios.ISIG))
        self.assertFalse(iflag & (termios.ICRNL | termios.INLCR | termios.IGNCR | termios.IXON))
        self.assertFalse(oflag & termios.OPOST)
        self.assertEqual((cc[termios.VMIN], cc[termios.VTIME]), (0, 0))


class SignalTest(unittest.TestCase):
    def test_the_first_signal_stops_and_puts_the_old_handlers_back(self):
        old = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
        stop = threading.Event()
        with device_log.stop_on_signals(stop):
            self.assertNotEqual(signal.getsignal(signal.SIGINT), old[signal.SIGINT])
            os.kill(os.getpid(), signal.SIGINT)
            wait_for(stop.is_set, what='the stop request')
            # A second Ctrl-C now acts as it did before the capture.
            self.assertEqual(signal.getsignal(signal.SIGINT), old[signal.SIGINT])
            self.assertEqual(signal.getsignal(signal.SIGTERM), old[signal.SIGTERM])
        self.assertEqual({sig: signal.getsignal(sig) for sig in old}, old)

    def test_handlers_are_restored_when_no_signal_comes(self):
        old = signal.getsignal(signal.SIGTERM)
        with device_log.stop_on_signals(threading.Event()):
            pass
        self.assertEqual(signal.getsignal(signal.SIGTERM), old)


class ArgumentTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = pathlib.Path(self.tmp.name) / 'out'

    def run_main(self, *args):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = device_log.main(list(args))
        return code, err.getvalue()

    def capture_args(self, *ports, extra=()):
        argv = ['capture', '--out', str(self.out)]
        for port in ports:
            argv += ['--port', port]
        return argv + list(extra)

    def test_bad_arguments_exit_2_with_a_message_and_write_nothing(self):
        cases = {
            'no equals': (['A'], 'is not NAME=PATH'),
            'bad name': (['a b=/dev/null'], 'is not NAME=PATH'),
            'empty path': (['A='], 'is not NAME=PATH'),
            'empty name': (['=/dev/null'], 'is not NAME=PATH'),
            'duplicate name': (['A=/dev/null', 'A=/dev/zero'], 'used twice'),
            'duplicate path': (['A=/dev/null', 'B=/dev/null'], 'same port'),
            'no port': ([], 'at least one --port'),
        }
        for label, (ports, message) in cases.items():
            with self.subTest(label):
                code, err = self.run_main(*self.capture_args(*ports))
                self.assertEqual(code, 2)
                self.assertIn(message, err)
        self.assertFalse(self.out.exists())

    def test_symlinks_to_one_port_are_one_port(self):
        link = pathlib.Path(self.tmp.name) / 'alias'
        os.symlink('/dev/null', link)
        code, err = self.run_main(*self.capture_args('A=/dev/null', f'B={link}'))
        self.assertEqual(code, 2)
        self.assertIn('same port', err)

    def test_unknown_baud_and_bad_duration_exit_2(self):
        code, err = self.run_main(*self.capture_args('A=/dev/null', extra=['--baud', '12345']))
        self.assertEqual(code, 2)
        self.assertIn('unknown baud rate 12345', err)
        for duration in ('0', '-1', 'nan'):
            with self.subTest(duration=duration):
                code, err = self.run_main(*self.capture_args('A=/dev/null', extra=['--duration', duration]))
                self.assertEqual(code, 2)
                self.assertIn('--duration', err)
        self.assertFalse(self.out.exists())

    def test_known_bauds_resolve_to_termios_constants(self):
        import termios
        self.assertEqual(device_log.baud_constant(115200), termios.B115200)
        self.assertEqual(device_log.baud_constant(9600), termios.B9600)
        for bad in (0, -1, 12345):
            with self.assertRaises(device_log.SetupError):
                device_log.baud_constant(bad)

    def test_missing_out_is_an_argparse_error_with_exit_2(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            device_log.main(['capture', '--port', 'A=/dev/null'])
        self.assertEqual(raised.exception.code, 2)

    def test_summary_without_a_log_or_with_an_unreadable_one_exits_2(self):
        code, err = self.run_main('summary')
        self.assertEqual(code, 2)
        self.assertIn('at least one log', err)
        code, err = self.run_main('summary', str(pathlib.Path(self.tmp.name) / 'missing.log'))
        self.assertEqual(code, 2)
        self.assertIn('cannot read', err)


class CommandLineTest(unittest.TestCase):
    def run_cli_until(self, sig):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            out = root / 'logs'
            pty = Pty(root / 'port')
            proc = subprocess.Popen(
                [sys.executable, str(SCRIPT), 'capture', '--port', f'A={pty.link}', '--port', 'N=/dev/null',
                 '--out', str(out)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                merged = out / 'merged.log'
                wait_for(lambda: merged.exists() and b' A [device_log] connected ' in merged.read_bytes(),
                         what='the CLI to connect')
                pty.send(b'from the device\n')
                wait_for(lambda: b' A from the device\n' in merged.read_bytes(), what='the line')
                pty.send(b'half')
                wait_for(lambda: (out / 'A.raw.log').read_bytes().endswith(b'half'), what='the half line')
                proc.send_signal(sig)
                _, stderr = proc.communicate(timeout=WAIT_SECONDS)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.communicate()
                pty.close()
            self.assertEqual(proc.returncode, 0, stderr.decode())
            self.assertEqual((out / 'A.raw.log').read_bytes(), b'from the device\nhalf')
            self.assertTrue((out / 'N.raw.log').exists())
            self.assertEqual((out / 'N.raw.log').read_bytes(), b'')
            self.assertIn(b' A half\n', (out / 'merged.log').read_bytes())
            self.assertIn(b'N /dev/null: not a serial port', stderr)

    def test_sigint_ends_cleanly_with_both_logs_written(self):
        self.run_cli_until(signal.SIGINT)

    def test_sigterm_ends_cleanly_with_both_logs_written(self):
        self.run_cli_until(signal.SIGTERM)


def sample_raw_log():
    """What one X4 Pro prints in a run, built from the firmware's own format strings (figures: packet rows U3, U5, I3, S5)."""
    lines = [
        device_line(10040, 'INF', 'MEM', 'mem', 193248, 8388608, 188880, 147444),
        device_line(10041, 'INF', 'MEM', 'psram', 8273868, 8388608, 8200000, 8100000),
        device_line(10042, 'DBG', 'WEB', 'web_mem', 190000),
        device_line(20000, 'INF', 'GAME', 'lifecycle', 'ultimate-tic-tac-toe', 'Playing', 'Leaving', 'ForcedExit'),
        device_line(20010, 'INF', 'GAME', 'stopped', 95704, 12196, 14072),
        device_line(30000, 'INF', 'GAME', 'drop_display', 'battleship'),
        device_line(30010, 'INF', 'GAME', 'drop_display', 'battleship'),
        device_line(30020, 'INF', 'GAME', 'drop_hand_off', 'battleship', 40),
        device_line(30030, 'INF', 'GAME', 'hand_off', 'battleship', 675),
        device_line(30040, 'INF', 'GAME', 'hand_off', 'battleship', 120),
        device_line(40000, 'INF', 'GAME', 'lifecycle', 'ultimate-tic-tac-toe', 'Over', 'Leaving', 'Leave'),
        device_line(40010, 'INF', 'GAME', 'stopped', 109616, 12280, 14072),
        device_line(50000, 'INF', 'MEM', 'mem', 193092, 8388608, 187000, 140000),
        device_line(50001, 'INF', 'MEM', 'psram', 8261000, 8388608, 8190000, 8000000),
        device_line(60000, 'INF', 'GAME', 'lifecycle', 'sudoku', 'Over', 'Leaving', 'Leave'),
        device_line(60010, 'INF', 'GAME', 'stopped', 302792, 10080, 13864),
        device_line(70000, 'INF', 'MEM', 'mem', 193500, 8388608, 189500, 147444),
    ]
    return '\n'.join(lines) + '\n'


# Recorded 2026-10-13 from two simulator instances of this worktree (sim.sh, SIM_INSTANCE=1 and 2, the `nearby` fixture):
# the lines of the host's and the guest's logs that name the link or the match's lifecycle, in the order they were
# printed, unedited, from lobby to Start, two moves and one tap on a taken square, the host's Leave, "The other player
# left" on the guest, and the guest's Back. The simulator compiles the same GameLinkLog.h and GameMatchActivity.cpp the
# device does, over UdpLink instead of EspNowLink, so the lines have the device's format and none of its timing: its
# stack high-water is the simulator's constant 2048, and no radio-up line is printed (the radio-up, queue-full, and
# link-refused lines are made from FORMATS below). The device's own capture replaces this at entry 11's smoke run.
RECORDED_HOST = '''\
[38907] [INF] [LINK] udp link up on 23872
[38908] [INF] [LINK] session=dc1b97fb task started; stack 4096 bytes
[58381] [INF] [GAME] nearby: Starting -> Playing on Started
[58384] [INF] [LINK] session=dc1b97fb state sent ver=1
[58789] [INF] [LINK] session=dc1b97fb resend n=1 total=1
[64503] [INF] [LINK] session=dc1b97fb state sent ver=2
[70700] [INF] [LINK] session=dc1b97fb state recv ver=3 seat=2
[70708] [INF] [LINK] session=dc1b97fb state adopt ver=3 seat=2 result=Adopted
[73796] [INF] [LINK] session=dc1b97fb state sent ver=4
[77019] [INF] [GAME] nearby: Playing -> Paused on Back
[77021] [INF] [GAME] nearby: Paused screen pushed in 1 ms
[82227] [INF] [GAME] nearby: Paused -> Leaving on Leave
[82227] [INF] [LINK] session=dc1b97fb leave reason=left waited=0ms unacked=0 copies=3
[82227] [INF] [LINK] session=dc1b97fb abort sent n=3 total=3
[82239] [INF] [LINK] session=dc1b97fb task stopped; stack high-water 2048 bytes free
'''

RECORDED_GUEST = '''\
[40265] [INF] [LINK] udp link up on 23873
[40266] [INF] [LINK] session=00000000 task started; stack 4096 bytes
[56711] [INF] [GAME] nearby: Starting -> Playing on Started
[57056] [INF] [LINK] session=dc1b97fb state recv ver=1 seat=1
[57065] [INF] [LINK] session=dc1b97fb state adopt ver=1 seat=1 result=RoundStarted
[62772] [INF] [LINK] session=dc1b97fb state recv ver=2 seat=1
[62776] [INF] [LINK] session=dc1b97fb state adopt ver=2 seat=1 result=Adopted
[68954] [INF] [LINK] session=dc1b97fb state sent ver=3
[72057] [INF] [LINK] session=dc1b97fb state recv ver=4 seat=1
[72059] [INF] [LINK] session=dc1b97fb state adopt ver=4 seat=1 result=Adopted
[80491] [INF] [LINK] session=dc1b97fb abort recv reason=left seat=1
[80491] [INF] [LINK] session=dc1b97fb abort recv reason=left seat=1
[80491] [INF] [LINK] session=dc1b97fb abort recv reason=left seat=1
[80497] [INF] [GAME] nearby: Playing -> PeerGone on PeerGone
[80497] [INF] [GAME] nearby: the other player is gone: left
[80500] [INF] [GAME] nearby: PeerGone screen pushed in 3 ms
[90499] [INF] [LINK] session=dc1b97fb silence n=1 total=1
[97797] [INF] [GAME] nearby: PeerGone -> Leaving on Back
[97797] [INF] [LINK] session=dc1b97fb leave reason=left waited=0ms unacked=0 copies=3
[97797] [INF] [LINK] session=dc1b97fb abort sent n=3 total=3
[97809] [INF] [LINK] session=dc1b97fb task stopped; stack high-water 2048 bytes free
'''


def section(text, name):
    """The part of a summary under one device's heading."""
    match = re.search(r'^== ' + re.escape(name) + r' \(.*?(?=^== |\Z)', text, re.DOTALL | re.MULTILINE)
    if not match:
        raise AssertionError(f'no section for {name} in:\n{text}')
    return match.group(0)


class SummaryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)

    def summary(self, *paths, mem_all=False):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(device_log.summary([str(p) for p in paths], mem_all), 0)
        return out.getvalue()

    def test_raw_log_summary_lists_every_figure_under_the_file_stem(self):
        path = self.root / 'A.raw.log'
        path.write_text(sample_raw_log())
        text = self.summary(path)
        self.assertEqual(text.count('== '), 1)
        a = section(text, 'A')
        self.assertNotIn('connected', a)  # a raw log has no connect events
        self.assertIn('VM stopped: 3', a)
        self.assertIn('[20010] ultimate-tic-tac-toe: arena peak 95704 B, stack high-water 12196 B free, '
                      'least at a hook 14072 B', a)
        self.assertIn('[40010] ultimate-tic-tac-toe: arena peak 109616 B, stack high-water 12280 B free, '
                      'least at a hook 14072 B', a)
        self.assertIn('[60010] sudoku: arena peak 302792 B, stack high-water 10080 B free, least at a hook 13864 B', a)
        self.assertIn('battleship: dropped before the frame was on the panel 2, dropped before the hand-off passed 1, '
                      'hand-off screens pushed 2, largest 675 ms', a)
        self.assertIn('[MEM] heap: 3 lines', a)
        self.assertIn('first: Free 193248 B, Total 8388608 B, Min Free 188880 B, MaxAlloc 147444 B at [10040]', a)
        self.assertIn('last: Free 193500 B, Total 8388608 B, Min Free 189500 B, MaxAlloc 147444 B at [70000]', a)
        self.assertIn('smallest Min Free: 187000 B at [50000]', a)
        self.assertIn('smallest MaxAlloc: 140000 B at [50000]', a)
        self.assertIn('[MEM] PSRAM: 2 lines', a)
        self.assertIn('first: Free 8273868 B, Total 8388608 B, Min Free 8200000 B, MaxAlloc 8100000 B at [10041]', a)
        self.assertIn('smallest Min Free: 8190000 B at [50001]', a)
        self.assertIn('smallest MaxAlloc: 8000000 B at [50001]', a)
        self.assertIn('[MEM] other: 1 lines', a)
        self.assertIn('[MEM] Free heap before begin: 190000 bytes at [10042]', a)

    def test_the_stopped_game_is_the_last_lifecycle_line_before_it(self):
        path = self.root / 'B.raw.log'
        path.write_text(device_line(1, 'INF', 'GAME', 'stopped', 1, 2, 3) + '\n' + sample_raw_log())
        b = section(self.summary(path), 'B')
        self.assertIn('[1] (unknown): arena peak 1 B', b)
        self.assertIn('] sudoku: arena peak 302792 B', b)

    def test_mem_all_adds_every_mem_line_and_nothing_else_does(self):
        path = self.root / 'A.raw.log'
        path.write_text(sample_raw_log())
        brief = self.summary(path)
        every = self.summary(path, mem_all=True)
        self.assertNotIn('every [MEM] line', brief)
        self.assertNotIn('193092', brief)  # the middle heap line is in no figure
        self.assertIn('every [MEM] line: 6', every)  # three heap, two PSRAM, one other
        for line in (device_line(10040, 'INF', 'MEM', 'mem', 193248, 8388608, 188880, 147444),
                     device_line(50000, 'INF', 'MEM', 'mem', 193092, 8388608, 187000, 140000),
                     device_line(10042, 'DBG', 'WEB', 'web_mem', 190000)):
            self.assertIn('    ' + line.split(' ', 1)[0] + ' ' + line.split(' ', 1)[1], every)
        self.assertEqual(every.replace(brief.rstrip('\n'), '', 1).count('\n    ['), 6)

    def test_merged_log_summary_keeps_each_device_apart_with_connects_and_host_times(self):
        merged = self.root / 'merged.log'
        raw_a = sample_raw_log().splitlines()
        stamp = '2026-10-12T09:00:00.000Z'
        lines = [f'{stamp} A [device_log] connected /dev/ttyA', f'{stamp} B [device_log] connected /dev/ttyB']
        lines += [f'2026-10-12T09:01:{i:02d}.250Z A {line}' for i, line in enumerate(raw_a[:5])]
        lines += [f'2026-10-12T09:02:00.000Z B {device_line(500, "INF", "GAME", "drop_display", "sudoku")}']
        lines += [f'2026-10-12T09:03:00.000Z A [device_log] lost /dev/ttyA: read failed: gone; waiting for it to come back',
                  f'2026-10-12T09:03:05.000Z A [device_log] connected /dev/ttyA',
                  f'2026-10-12T09:03:06.000Z A {device_line(900, "INF", "GAME", "stopped", 7, 8, 9)}',
                  f'2026-10-12T09:04:00.000Z B {device_line(600, "INF", "GAME", "stopped", 11, 12, 13)}']
        merged.write_text('\n'.join(lines) + '\n')
        text = self.summary(merged)
        self.assertEqual([m for m in re.findall(r'^== (\S+)', text, re.MULTILINE)], ['A', 'B'])
        a, b = section(text, 'A'), section(text, 'B')
        self.assertIn('connected 2, lost 1', a)
        self.assertIn('connected 1, lost 0', b)
        self.assertIn('2026-10-12T09:01:04.250Z ultimate-tic-tac-toe: arena peak 95704 B, stack high-water 12196 B free, '
                      'least at a hook 14072 B', a)
        self.assertIn('2026-10-12T09:03:06.000Z (unknown): arena peak 7 B, stack high-water 8 B free, '
                      'least at a hook 9 B', a)  # the game is forgotten when the device went away
        self.assertIn('VM stopped: 2', a)
        self.assertNotIn('arena peak 11 B', a)
        self.assertIn('2026-10-12T09:04:00.000Z (unknown): arena peak 11 B, stack high-water 12 B free, '
                      'least at a hook 13 B', b)
        self.assertIn('sudoku: dropped before the frame was on the panel 1', b)
        self.assertNotIn('battleship', b)
        self.assertIn('[MEM] heap: 1 lines', a)
        self.assertIn('Min Free 188880 B', a)
        self.assertIn('[MEM] heap: none', b)

    def test_two_raw_logs_are_two_devices_in_first_seen_order(self):
        (self.root / 'B.raw.log').write_text(device_line(5, 'INF', 'GAME', 'stopped', 5, 6, 7) + '\n')
        (self.root / 'A.raw.log').write_text(device_line(6, 'INF', 'GAME', 'stopped', 8, 9, 10) + '\n')
        text = self.summary(self.root / 'B.raw.log', self.root / 'A.raw.log')
        self.assertEqual(re.findall(r'^== (\S+)', text, re.MULTILINE), ['B', 'A'])
        self.assertIn('arena peak 5 B', section(text, 'B'))
        self.assertIn('arena peak 8 B', section(text, 'A'))

    def test_a_device_in_two_logs_is_an_error_naming_it_and_both_files(self):
        raw = self.root / 'A.raw.log'
        raw.write_text(sample_raw_log())
        merged = self.root / 'merged.log'
        merged.write_text('2026-10-12T09:00:00.000Z A [device_log] connected /dev/ttyA\n')
        other = self.root / 'sub'
        other.mkdir()
        twin = other / 'A.raw.log'
        twin.write_text('')
        for first, second in ((raw, merged), (merged, raw), (raw, twin)):
            with self.subTest(first=first.name, second=str(second)):
                with self.assertRaises(device_log.SetupError) as raised:
                    self.summary(first, second)
                message = str(raised.exception)
                self.assertIn('device A', message)
                self.assertIn(str(first), message)
                self.assertIn(str(second), message)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(device_log.main(['summary', str(raw), str(merged)]), 2)
        self.assertIn('one log per device', err.getvalue())
        self.assertEqual(self.summary(raw).count('== '), 1)  # one log per device still works
        self.assertEqual(self.summary(merged).count('== '), 1)

    def test_an_empty_log_is_a_device_with_nothing(self):
        path = self.root / 'E.raw.log'
        path.write_text('')
        text = self.summary(path)
        self.assertIn('VM stopped: 0', section(text, 'E'))

    def test_a_recorded_nearby_match_gives_each_device_its_link_lines_and_peer_gone(self):
        host, guest = self.root / 'A.raw.log', self.root / 'B.raw.log'
        host.write_text(RECORDED_HOST)
        guest.write_text(RECORDED_GUEST)
        text = self.summary(host, guest)
        a, b = section(text, 'A'), section(text, 'B')
        self.assertIn('nearby: radio up 0, LINK sessions 1, PeerGone 0, LINK errors 0', a)
        self.assertIn('session dc1b97fb:', a)
        self.assertIn('STATE sent 3 (last ver 4), received 1 (last ver 3)', a)
        self.assertIn('adopted: Adopted 1 (last ver 3)', a)
        self.assertIn('queue full 0 (overflows -)', a)
        self.assertIn('resend 1, silence 0, abort sent 3, link refused 0', a)
        self.assertIn('ABORT received (one line per copy the device heard): none', a)
        self.assertIn('leave at [82227]: reason left, waited 0 ms, unacked 0, ABORT copies 3', a)
        self.assertIn('GameLink task: stack 4096 B, high-water 2048 B free at [82239]', a)
        self.assertIn('GameLink task stack: least high-water 2048 B free of 4096 B over 1 stop(s)', a)
        self.assertIn('LINK at [38907]: udp link up on 23872', a)
        self.assertNotIn('the other player is gone', a)
        self.assertIn('nearby: radio up 0, LINK sessions 2, PeerGone 1, LINK errors 0', b)
        self.assertIn('session 00000000:', b)  # a guest's link before it joined: the id is not known yet
        self.assertIn('STATE sent 1 (last ver 3), received 3 (last ver 4)', b)
        self.assertIn('adopted: RoundStarted 1, Adopted 2 (last ver 4)', b)
        self.assertIn('resend 0, silence 1, abort sent 3, link refused 0', b)
        self.assertIn('ABORT received (one line per copy the device heard): left from seat 1 x3', b)
        self.assertIn('leave at [97797]: reason left, waited 0 ms, unacked 0, ABORT copies 3', b)
        self.assertIn('GameLink task: stack 4096 B, high-water 2048 B free at [97809]', b)  # the stop's session has no start
        self.assertIn('GameLink task: stack 4096 B, high-water no stop under this id', b)  # session 00000000's own
        self.assertIn('PeerGone at [80497]: nearby on PeerGone', b)
        self.assertIn('the other player is gone: left x1', b)

    def test_every_recorded_link_line_is_read_by_a_pattern_except_the_link_up_line(self):
        for name, log in (('A', RECORDED_HOST), ('B', RECORDED_GUEST)):
            path = self.root / f'{name}.raw.log'
            path.write_text(log)
            device = device_log.read_logs([str(path)])[name]
            self.assertEqual(len(device.link_other), 1, device.link_other)
            self.assertTrue(device.link_other[0][1].startswith('udp link up on '))
            for session in device.link_sessions.values():
                self.assertEqual(session.other, [], f'device {name} session {session.session}')
            self.assertEqual(device.link_errors, [])

    def test_the_lines_no_recording_has_the_radio_up_a_full_queue_refusals_and_errors_are_read(self):
        lines = [
            device_line(100, 'INF', 'LINK', 'radio_up', 1, 0x24, 0x0A, 0xC4, 0x3F, 0x2A, 0x91),
            device_line(110, 'INF', 'LINK', 'link_task_started', 0xAB12CD34, 4096),
            device_line(120, 'INF', 'LINK', 'link_queue_full', 0xAB12CD34, 7, 1),
            device_line(130, 'INF', 'LINK', 'link_queue_full', 0xAB12CD34, 9, 2),
            device_line(140, 'INF', 'LINK', 'link_refused', 0xAB12CD34, 2, 5),
            device_line(150, 'INF', 'LINK', 'link_resend', 0xAB12CD34, 1, 1),
            device_line(160, 'INF', 'LINK', 'link_resend', 0xAB12CD34, 3, 4),
            device_line(165, 'INF', 'LINK', 'link_abort_recv', 0xAB12CD34, 'script_error', 2),
            device_line(170, 'INF', 'LINK', 'link_task_stopped', 0xAB12CD34, 1700),
            device_line(180, 'INF', 'GAME', 'peer_gone_reason', 'nearby', 'timeout'),
            device_line(185, 'INF', 'GAME', 'lifecycle', 'nearby', 'Playing', 'PeerGone', 'PeerGone'),
            device_line(190, 'INF', 'LINK', 'radio_up', 1, 0x24, 0x0A, 0xC4, 0x3F, 0x2A, 0x91),
            device_line(200, 'ERR', 'LINK', 'radio_failed', 'initEspNow'),
            device_line(210, 'ERR', 'LINK', 'link_bring_up_failed', 0, 'no memory for the session'),
            device_line(215, 'ERR', 'GAME', 'lobby_radio_failed', 'nearby'),
            '[220] [INF] [LINK] some line a later firmware adds',
            '[230] [INF] [LINK] session=ab12cd34 a session line a later firmware adds',
        ]
        path = self.root / 'C.raw.log'
        path.write_text('\n'.join(lines) + '\n')
        c = section(self.summary(path), 'C')
        self.assertIn('nearby: radio up 2, LINK sessions 1, PeerGone 1, LINK errors 3', c)
        self.assertIn('[100] radio up on channel 1 as 24:0A:C4:3F:2A:91', c)
        self.assertIn('queue full 2 (overflows 2)', c)
        self.assertIn('resend 4, silence 0, abort sent 0, link refused 5', c)  # the running totals, not the steps
        self.assertIn('ABORT received (one line per copy the device heard): script_error from seat 2 x1', c)
        self.assertIn('GameLink task: stack 4096 B, high-water 1700 B free at [170]', c)
        self.assertIn('PeerGone at [185]: nearby on PeerGone', c)
        self.assertIn('the other player is gone: timeout x1', c)
        self.assertIn('LINK error at [200]: bring-up failed at initEspNow', c)
        self.assertIn('LINK error at [210]: session=00000000 bring-up failed: no memory for the session', c)
        self.assertIn('LINK error at [215]: Nearby: the radio did not start for nearby', c)
        self.assertIn('LINK at [220]: some line a later firmware adds', c)
        self.assertIn('other: a session line a later firmware adds at [230]', c)

    def test_a_device_with_no_nearby_line_says_none(self):
        path = self.root / 'A.raw.log'
        path.write_text(sample_raw_log())
        self.assertIn('  nearby: none', section(self.summary(path), 'A'))

    def test_the_merged_log_carries_the_nearby_lines_per_device_with_host_times(self):
        merged = self.root / 'merged.log'
        lines = [f'2026-10-13T10:00:{i:02d}.000Z A {line}' for i, line in enumerate(RECORDED_HOST.splitlines())]
        lines += [f'2026-10-13T10:01:{i:02d}.000Z B {line}' for i, line in enumerate(RECORDED_GUEST.splitlines())]
        merged.write_text('\n'.join(lines) + '\n')
        text = self.summary(merged)
        a, b = section(text, 'A'), section(text, 'B')
        self.assertRegex(a, r'leave at 2026-10-13T10:00:\d\d\.000Z: reason left')
        self.assertRegex(b, r'PeerGone at 2026-10-13T10:01:\d\d\.000Z: nearby on PeerGone')
        self.assertNotIn('PeerGone at', a)

    def test_capture_then_summary_end_to_end(self):
        out = self.root / 'out'
        link = self.root / 'port'
        pty = Pty(link)
        self.addCleanup(pty.close)
        stop = threading.Event()
        thread = threading.Thread(target=device_log.capture, args=([('A', str(link))], out),
                                  kwargs={'stop': stop, 'err': io.StringIO()}, daemon=True)
        thread.start()
        merged = out / 'merged.log'
        wait_for(lambda: merged.exists() and b'connected' in merged.read_bytes(), what='connected')
        for line in (device_line(1, 'INF', 'GAME', 'lifecycle', 'sudoku', 'Playing', 'Leaving', 'ForcedExit'),
                     device_line(2, 'INF', 'GAME', 'stopped', 301568, 10068, 13864)):
            pty.send(line.encode() + b'\n')
        wait_for(lambda: b'arena peak 301568' in merged.read_bytes(), what='the stop line')
        stop.set()
        thread.join(WAIT_SECONDS)
        self.assertIn('sudoku: arena peak 301568 B, stack high-water 10068 B free', section(self.summary(merged), 'A'))


class FirmwareFormatsTest(unittest.TestCase):
    def test_firmware_formats_are_still_in_the_sources(self):
        # The sample logs above fill in these strings; the summary's patterns parse what they print.
        for kind, (path, text) in FORMATS.items():
            with self.subTest(kind):
                source = (ROOT / path).read_text()
                self.assertIn(text, source, f'{path} no longer holds the {kind} format; update device_log.py')

    def test_the_mem_formats_keep_their_origin_and_the_vm_line_its_text(self):
        main_cpp = (ROOT / 'src/main.cpp').read_text()
        self.assertIn('LOG_INF("MEM", "Free: %zu bytes', main_cpp)
        self.assertIn('LOG_INF("MEM", "PSRAM: Free: %zu bytes', main_cpp)
        self.assertIn('LOG_INF("GAME", "VM stopped;', (ROOT / 'src/games/GameVM.cpp').read_text())
        match_cpp = (ROOT / 'src/activities/games/GameMatchActivity.cpp').read_text()
        for kind in ('lifecycle', 'drop_hand_off', 'drop_display', 'hand_off'):
            self.assertIn(f'LOG_INF("GAME", "{FORMATS[kind][1]}"', match_cpp)

    def test_the_link_formats_keep_their_origin_and_level(self):
        for kind in LINK_INF_KINDS + LINK_ERR_KINDS:
            level = 'LOG_INF' if kind in LINK_INF_KINDS else 'LOG_ERR'
            path, text = FORMATS[kind]
            with self.subTest(kind):
                self.assertIn(f'{level}("LINK", "{text}"', (ROOT / path).read_text())
        self.assertIn('LOG_INF("GAME", "%s: the other player is gone: %s"',
                      (ROOT / FORMATS['peer_gone_reason'][0]).read_text())
        path, text = FORMATS['lobby_radio_failed']
        self.assertIn(f'LOG_ERR("GAME", "{text}"', (ROOT / path).read_text())

    def test_the_parsers_read_what_the_formats_print(self):
        cases = [
            (device_log.LIFECYCLE, device_line(1, 'INF', 'GAME', 'lifecycle', 'g-1', 'A', 'B', 'C')),
            (device_log.VM_STOPPED, device_line(1, 'INF', 'GAME', 'stopped', 1, 2, 3)),
            (device_log.DROPPED_BEFORE_HAND_OFF, device_line(1, 'INF', 'GAME', 'drop_hand_off', 'g-1', 5)),
            (device_log.DROPPED_BEFORE_DISPLAY, device_line(1, 'INF', 'GAME', 'drop_display', 'g-1')),
            (device_log.HAND_OFF_PUSHED, device_line(1, 'INF', 'GAME', 'hand_off', 'g-1', 5)),
            (device_log.MEM_LINE, device_line(1, 'INF', 'MEM', 'mem', 1, 2, 3, 4)),
            (device_log.MEM_LINE, device_line(1, 'INF', 'MEM', 'psram', 1, 2, 3, 4)),
            (device_log.MILLIS_PREFIX, device_line(1, 'INF', 'MEM', 'mem', 1, 2, 3, 4)),
            (device_log.PEER_GONE_REASON, device_line(1, 'INF', 'GAME', 'peer_gone_reason', 'g-1', 'left')),
            (device_log.LINK_LINE, device_line(1, 'INF', 'LINK', 'udp_up', 23872)),
            (device_log.LINK_ERROR_LINE, device_line(1, 'ERR', 'LINK', 'radio_failed', 'setChannel')),
            (device_log.NEARBY_ERROR, device_line(1, 'ERR', 'GAME', 'lobby_radio_failed', 'nearby')),
            (device_log.RADIO_UP, device_line(1, 'INF', 'LINK', 'radio_up', 1, 1, 2, 3, 4, 5, 6)),
            (device_log.LINK_SESSION, device_line(1, 'INF', 'LINK', 'link_resend', 7, 1, 1)),
        ]
        sessions = [
            (device_log.LINK_STATE_SENT, 'link_state_sent', (1, 5)),
            (device_log.LINK_STATE_RECV, 'link_state_recv', (1, 5, 2)),
            (device_log.LINK_STATE_ADOPT, 'link_state_adopt', (1, 5, 2, 'Adopted')),
            (device_log.LINK_QUEUE_FULL, 'link_queue_full', (1, 5, 2)),
            (device_log.LINK_ABORT_RECV, 'link_abort_recv', (1, 'version_mismatch', 2)),
            (device_log.LINK_LEAVE, 'link_leave', (1, 'left', 800, 1, 3)),
            (device_log.LINK_COUNTER, 'link_resend', (1, 1, 1)),
            (device_log.LINK_COUNTER, 'link_silence', (1, 1, 1)),
            (device_log.LINK_COUNTER, 'link_abort_sent', (1, 3, 3)),
            (device_log.LINK_COUNTER, 'link_refused', (1, 1, 1)),
            (device_log.LINK_TASK_STARTED, 'link_task_started', (1, 4096)),
            (device_log.LINK_TASK_STOPPED, 'link_task_stopped', (1, 1800)),
        ]
        for pattern, kind, values in sessions:
            line = device_line(1, 'INF', 'LINK', kind, *values)
            body = device_log.LINK_SESSION.search(line)
            with self.subTest(kind):
                self.assertTrue(body, line)
                self.assertTrue(pattern.match(body.group(2)), line)
        for pattern, line in cases:
            with self.subTest(line=line):
                self.assertTrue(pattern.search(line))


if __name__ == '__main__':
    unittest.main()
