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
        self.vip_port = _free_tcp_port()

        cmd = [str(RUN_SCRIPT), "--headless", "--monitor", self.monitor_path]
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

    def wait_serial(self, pattern, timeout):
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while True:
            text = self.serial_text()
            match = regex.search(text)
            if match:
                return match.group(0)
            if self._proc.poll() is not None:
                raise AssertionError(
                    f"QEMU exited (rc={self._proc.returncode}) before /{pattern}/ appeared; "
                    f"serial tail:\n{text[-3000:]}")
            if time.monotonic() > deadline:
                raise AssertionError(f"/{pattern}/ not seen on serial within {timeout}s; serial tail:\n{text[-3000:]}")
            time.sleep(0.5)

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
