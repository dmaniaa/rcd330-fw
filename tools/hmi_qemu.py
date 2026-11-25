#!/usr/bin/env python3
"""Headless QEMU harness for the RCD 330 (-M rcd330) used by the hmi tests.

    with Rcd330Qemu(hmi="build/hmi/hmi") as qemu:
        qemu.wait_serial(r"\\[HMI_START\\]HMI main start", 180)
        qemu.vip("BTN APP 1")
        qemu.screendump("/tmp/screen.ppm")

Boots through tools/run_rcd330.sh --headless (stock firmware tar, cached
rootfs). hmi=None boots the stock /opt/hmi; otherwise the given binary is
staged over /opt/hmi in a separate cpio. env entries are appended to the
staged /etc/profile as exports, which the variant /opt/launch.sh sources so
pwr_agent and the hmi it spawns inherit them. hmi's log_simple(" ==== HMI ==== ...") lines
reach the serial console through the libhal_gpio_shim.so LD_PRELOAD hook.
"""
import json
import os
import pathlib
import re
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
RUN_SCRIPT = ROOT / "tools/run_rcd330.sh"
# AF_UNIX paths are limited to 104 bytes on macOS (108 on Linux).
MAX_UNIX_PATH = 100


def _free_tcp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _short_tmpdir():
    tmp = tempfile.mkdtemp(prefix="rcd330-")
    if len(os.path.join(tmp, "monitor.sock")) <= MAX_UNIX_PATH:
        return tmp
    os.rmdir(tmp)
    return tempfile.mkdtemp(prefix="rcd330-", dir="/tmp")


class Rcd330Qemu:
    def __init__(self, hmi=None, env=None):
        self.hmi = None if hmi is None else str((ROOT / hmi).resolve())
        self.env = dict(env or {})
        self._proc = None
        self._serial = bytearray()
        self._serial_lock = threading.Lock()
        self._reader = None
        self._vip_sock = None
        self._vip_drain = None
        self._tmpdir = None
        self.vip_port = None
        self.monitor_path = None
        self.qmp_path = None

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def start(self):
        if self.hmi is not None and not os.path.isfile(self.hmi):
            raise FileNotFoundError(f"hmi binary not found: {self.hmi} (build it with tools/build_hmi.sh)")
        self._tmpdir = _short_tmpdir()
        self.monitor_path = os.path.join(self._tmpdir, "monitor.sock")
        self.qmp_path = os.path.join(self._tmpdir, "qmp.sock")
        self.vip_port = _free_tcp_port()

        cmd = [str(RUN_SCRIPT), "--headless", "--monitor", self.monitor_path, "--qmp", self.qmp_path]
        if self.hmi is not None:
            cmd += ["--hmi", self.hmi]
        for key, val in self.env.items():
            cmd += ["--profile-env", f"{key}={val}"]
        proc_env = dict(os.environ, QEMU_VIP_PORT=str(self.vip_port))
        self._proc = subprocess.Popen(
            cmd, cwd=ROOT, env=proc_env, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        self._reader = threading.Thread(target=self._read_serial, daemon=True)
        self._reader.start()

    def _read_serial(self):
        for chunk in iter(lambda: self._proc.stdout.read1(4096), b""):
            with self._serial_lock:
                self._serial += chunk

    def serial_text(self):
        with self._serial_lock:
            return self._serial.decode("utf-8", errors="replace")

    def mark(self):
        """Current serial length, for wait_serial(since=...)."""
        return len(self.serial_text())

    def wait_serial(self, pattern, timeout, since=0):
        """Wait for pattern in the serial output from offset since (a mark())."""
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while True:
            text = self.serial_text()
            match = regex.search(text, since)
            if match:
                return match.group(0)
            if self._proc.poll() is not None:
                raise AssertionError(
                    f"QEMU exited (rc={self._proc.returncode}) before /{pattern}/ appeared; "
                    f"serial tail:\n{text[-3000:]}")
            if time.monotonic() > deadline:
                raise AssertionError(f"/{pattern}/ not seen on serial within {timeout}s; serial tail:\n{text[-3000:]}")
            time.sleep(0.5)

    def wait_hmi_ready(self, banner_timeout=240, page_timeout=120):
        """Waits until the hmi takes input, stock or recreated.

        Ready = the boot page (tuner, screen 0xf9) is on screen and pwr_agent's
        WS_IDLE -> WS_RUNNING cycle is done. WS_RUNNING clears VPowerProxy's
        hmi-run flag; the hmi asks the VIP for its audio mode, and the RUN that
        follows recovers the main page (VPowerProxy RecoverSetupMainPage logs
        "wangqin cur main = ..." and re-inserts it if another page is up). Stock
        gets the cycle before its boot page; the recreated hmi starts faster
        and may get it afterwards, when a page opened in between would be
        replaced by the main page again.
        """
        self.wait_serial(r"\[HMI_START\]HMI main start", banner_timeout)
        self.wait_serial(r"VPageManager::ShowPage 249\b[\s\S]*gl_fb_shim: eglSwapBuffers #1\b", page_timeout)
        self.wait_serial(r"VPowerProxy::WS_RUNNING[\s\S]*wangqin cur main = ", page_timeout)

    def vip(self, cmd, timeout=10.0):
        """Send one text line to the VIP control socket (see vip_process_cmd in rcd330_vip.c).

        The connection stays open: the VIP releases all held keys when it closes.
        """
        if self._vip_sock is None:
            self._vip_sock = self._connect_vip(timeout)
            self._vip_drain = threading.Thread(target=self._drain_vip, args=(self._vip_sock,), daemon=True)
            self._vip_drain.start()
        self._vip_sock.sendall(cmd.encode("ascii") + b"\n")

    def _connect_vip(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            try:
                return socket.create_connection(("127.0.0.1", self.vip_port), timeout=2)
            except OSError as err:
                if time.monotonic() > deadline:
                    raise ConnectionError(f"VIP control port 127.0.0.1:{self.vip_port} unreachable: {err}") from err
                time.sleep(0.2)

    @staticmethod
    def _drain_vip(sock):
        # VIP notifications ("OK BTN", ...) are not needed; read them so the socket never backs up.
        sock.settimeout(None)
        try:
            while sock.recv(4096):
                pass
        except OSError:
            pass

    def knob(self, steps):
        """Rotate the TUNE rotary encoder by steps (+ clockwise, - counter-clockwise)."""
        self.vip(f"ENC TUNE {int(steps)}")

    def touch(self, x, y, hold_s=0.15):
        """Tap screen coordinate (x, y) of the 800x480 display.

        QMP input-send-event (absolute axes 0..32767) reaches the atmel_mxt
        touchscreen model, which maps it through the factory /etc/pointercal
        so tslib in the guest reports (x, y) again.
        """
        if not (0 <= x <= 799 and 0 <= y <= 479):
            raise ValueError(f"touch ({x}, {y}) outside the 800x480 display")
        ax = int(round(x * 32767 / 799.0))
        ay = int(round(y * 32767 / 479.0))
        self._qmp_cmd({"execute": "input-send-event", "arguments": {"events": [
            {"type": "abs", "data": {"axis": "x", "value": ax}},
            {"type": "abs", "data": {"axis": "y", "value": ay}},
            {"type": "btn", "data": {"down": True, "button": "left"}},
        ]}})
        time.sleep(hold_s)
        self._qmp_cmd({"execute": "input-send-event", "arguments": {"events": [
            {"type": "btn", "data": {"down": False, "button": "left"}},
        ]}})

    def _qmp_cmd(self, payload, timeout=10.0):
        """Run one QMP command on a fresh connection; returns its reply."""
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(timeout)
            s.connect(self.qmp_path)
            f = s.makefile("rwb", buffering=0)
            self._qmp_reply(f, "greeting", key="QMP")
            f.write(b'{"execute":"qmp_capabilities"}\n')
            self._qmp_reply(f, "qmp_capabilities")
            f.write(json.dumps(payload).encode("ascii") + b"\n")
            return self._qmp_reply(f, payload)

    @staticmethod
    def _qmp_reply(f, what, key="return"):
        # Asynchronous {"event": ...} lines may come before the reply.
        while True:
            line = f.readline()
            if not line:
                raise ConnectionError(f"QMP closed while waiting for the reply to {what}")
            reply = json.loads(line.decode("utf-8"))
            if "error" in reply:
                raise RuntimeError(f"QMP error for {what}: {reply['error']}")
            if key in reply:
                return reply

    def screendump(self, path, timeout=10.0):
        """Write the current display as PPM via the QEMU monitor."""
        path = os.path.abspath(path)
        if os.path.exists(path):
            os.remove(path)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as mon:
            mon.settimeout(timeout)
            mon.connect(self.monitor_path)
            self._read_until_prompt(mon)
            mon.sendall(f"screendump {path}\n".encode())
            reply = self._read_until_prompt(mon)
        if not os.path.isfile(path):
            raise RuntimeError(f"screendump did not write {path}; monitor said: {reply!r}")

    def screendump_stable(self, path, timeout=30.0, interval=1.0, stable_frames=3):
        """Dump until `stable_frames` consecutive dumps are byte-identical.

        A single dump can catch the stock UI mid-transition (e.g. a page
        redraw or language switch after the first frame). Raises
        RuntimeError if the display keeps changing for `timeout` seconds.
        """
        path = os.path.abspath(path)
        deadline = time.monotonic() + timeout
        previous, same = None, 1
        while True:
            self.screendump(path)
            with open(path, "rb") as f:
                current = f.read()
            same = same + 1 if current == previous else 1
            if same >= stable_frames:
                return
            if time.monotonic() >= deadline:
                raise RuntimeError(f"display did not stabilize within {timeout}s "
                                   f"({stable_frames} identical frames {interval}s apart needed)")
            previous = current
            time.sleep(interval)

    @staticmethod
    def _read_until_prompt(mon):
        buf = b""
        while not buf.endswith(b"(qemu) "):
            chunk = mon.recv(4096)
            if not chunk:
                raise ConnectionError(f"QEMU monitor closed; got {buf!r}")
            buf += chunk
        return buf.decode(errors="replace")

    def close(self):
        if self._vip_sock is not None:
            self._vip_sock.close()
            self._vip_sock = None
        if self._proc is not None:
            if self._proc.poll() is None:
                os.killpg(self._proc.pid, signal.SIGTERM)
                try:
                    self._proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(self._proc.pid, signal.SIGKILL)
                    self._proc.wait()
            if self._proc.stdin is not None:
                self._proc.stdin.close()
            self._reader.join(timeout=5)
            self._proc.stdout.close()
            self._proc = None
        if self._tmpdir is not None:
            shutil.rmtree(self._tmpdir, ignore_errors=True)
            self._tmpdir = None
