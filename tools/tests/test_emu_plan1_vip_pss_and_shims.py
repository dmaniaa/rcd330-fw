#!/usr/bin/env python3
import json
import os
import pathlib
import select
import shutil
import struct
import subprocess
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
VIP_C = ROOT / "qemu/hw/misc/rcd330_vip.c"
QEMU_BIN = ROOT / "qemu/build/qemu-system-arm"

VIP_MMIO_BASE = 0x021FC000
VIP_MMIO_TX_RESET = VIP_MMIO_BASE + 0x00
VIP_MMIO_TX_DATA = VIP_MMIO_BASE + 0x04
VIP_MMIO_TX_COMMIT = VIP_MMIO_BASE + 0x08
VIP_MMIO_RX_POP = VIP_MMIO_BASE + 0x10
VIP_MMIO_RX_DATA = VIP_MMIO_BASE + 0x14


def ipcl_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_ipcl_frame(grp: int, sub: int, payload: bytes) -> bytes:
    sub_len = 4 + len(payload)
    total = 2 + sub_len + 2
    body = struct.pack(">HHBB", total, sub_len, grp, sub) + payload
    return body + struct.pack(">H", ipcl_crc16(body))


def parse_ipcl_frame(frame: bytes) -> tuple[int, int, bytes]:
    if len(frame) < 8:
        raise AssertionError(f"IPCL frame too short: {frame.hex()}")
    total, sub_len, grp, sub = struct.unpack(">HHBB", frame[:6])
    if total != len(frame) or sub_len != len(frame) - 4:
        raise AssertionError(
            f"Malformed IPCL lengths: total={total}, sub_len={sub_len}, actual={len(frame)}"
        )
    expected_crc = ipcl_crc16(frame[:-2])
    actual_crc = struct.unpack(">H", frame[-2:])[0]
    if expected_crc != actual_crc:
        raise AssertionError(
            f"IPCL CRC mismatch: expected=0x{expected_crc:04x}, actual=0x{actual_crc:04x}"
        )
    return grp, sub, frame[6:-2]


class _FifoPair:
    def __init__(self, prefix: pathlib.Path) -> None:
        self.in_path = pathlib.Path(f"{prefix}.in")
        self.out_path = pathlib.Path(f"{prefix}.out")
        os.mkfifo(self.in_path)
        os.mkfifo(self.out_path)
        # Open both FIFOs R/W non-blocking BEFORE spawning QEMU so neither side blocks.
        self.wr_fd = os.open(self.in_path, os.O_RDWR | os.O_NONBLOCK)
        self.rd_fd = os.open(self.out_path, os.O_RDWR | os.O_NONBLOCK)
        self.buf = bytearray()

    def write(self, data: bytes) -> None:
        view = memoryview(data)
        sent = 0
        deadline = time.monotonic() + 5.0
        while sent < len(data):
            _, writable, _ = select.select([], [self.wr_fd], [], 0.2)
            if writable:
                sent += os.write(self.wr_fd, view[sent:])
            elif time.monotonic() > deadline:
                raise TimeoutError(f"Timed out writing to {self.in_path}")

    def read_some(self, timeout: float = 0.1) -> bytes:
        readable, _, _ = select.select([self.rd_fd], [], [], timeout)
        if not readable:
            return b""
        try:
            return os.read(self.rd_fd, 4096)
        except BlockingIOError:
            return b""

    def close(self) -> None:
        for fd in (self.wr_fd, self.rd_fd):
            try:
                os.close(fd)
            except OSError:
                pass


class QemuVipHarness:
    """Reusable sandbox-safe QEMU -M rcd330 FIFO harness (VIP chardev + QMP + GDB MMIO)."""

    def __init__(self, extra_args: list[str] | tuple[str, ...] | None = None) -> None:
        self.extra_args = list(extra_args) if extra_args else []
        self.tmpdir = pathlib.Path(tempfile.mkdtemp(prefix="rcd330_vip_test_"))
        self.vip_fifo = _FifoPair(self.tmpdir / "vip")
        self.qmp_fifo = _FifoPair(self.tmpdir / "qmp")
        self.gdb_fifo = _FifoPair(self.tmpdir / "gdb")
        self.proc: subprocess.Popen[bytes] | None = None

    def __enter__(self) -> "QemuVipHarness":
        cmd = [
            str(QEMU_BIN),
            "-M", "rcd330",
            "-S",
            "-display", "none",
            "-serial", "none",
            "-chardev", f"pipe,id=vipctl,path={self.tmpdir / 'vip'}",
            "-global", "rcd330-vip.chardev=vipctl",
            "-chardev", f"pipe,id=qmp0,path={self.tmpdir / 'qmp'}",
            "-qmp", "chardev:qmp0",
            "-chardev", f"pipe,id=gdb0,path={self.tmpdir / 'gdb'}",
            "-gdb", "chardev:gdb0",
            *self.extra_args,
        ]
        try:
            self.proc = subprocess.Popen(
                cmd,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            # Wait for QMP greeting and enter command mode.
            greeting = self._read_qmp_json()
            if "QMP" not in greeting:
                raise AssertionError(f"Unexpected QMP greeting: {greeting!r}")
            resp = self.qmp_cmd("qmp_capabilities")
            if "return" not in resp:
                raise AssertionError(f"qmp_capabilities failed: {resp!r}")
        except Exception:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.proc is not None:
            if self.proc.poll() is None:
                try:
                    self.qmp_cmd("quit")
                except Exception:
                    pass
                try:
                    self.proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=2.0)
            if self.proc.stdout:
                self.proc.stdout.close()
            if self.proc.stderr:
                self.proc.stderr.close()
        self.vip_fifo.close()
        self.qmp_fifo.close()
        self.gdb_fifo.close()
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _read_qmp_json(self, timeout: float = 5.0) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            nl = self.qmp_fifo.buf.find(b"\n")
            if nl >= 0:
                line = bytes(self.qmp_fifo.buf[:nl]).decode("utf-8").strip()
                del self.qmp_fifo.buf[: nl + 1]
                if line:
                    return json.loads(line)
            rem = deadline - time.monotonic()
            if rem <= 0:
                raise TimeoutError("Timed out waiting for QMP JSON message")
            chunk = self.qmp_fifo.read_some(min(rem, 0.2))
            if chunk:
                self.qmp_fifo.buf.extend(chunk)

    def qmp_cmd(self, execute: str, timeout: float = 5.0) -> dict:
        payload = json.dumps({"execute": execute}).encode("utf-8") + b"\n"
        self.qmp_fifo.write(payload)
        deadline = time.monotonic() + timeout
        while True:
            rem = deadline - time.monotonic()
            if rem <= 0:
                raise TimeoutError(f"Timed out waiting for QMP return for {execute}")
            msg = self._read_qmp_json(timeout=rem)
            if "event" in msg:
                continue
            return msg

    def _gdb_packet(self, body: str, timeout: float = 5.0) -> str:
        csum = sum(body.encode("ascii")) & 0xFF
        pkt = f"${body}#{csum:02x}".encode("ascii")
        self.gdb_fifo.write(pkt)
        deadline = time.monotonic() + timeout
        while True:
            start = self.gdb_fifo.buf.find(b"$")
            if start >= 0:
                end = self.gdb_fifo.buf.find(b"#", start + 1)
                if end >= 0 and len(self.gdb_fifo.buf) >= end + 3:
                    reply = bytes(self.gdb_fifo.buf[start + 1 : end]).decode("ascii")
                    del self.gdb_fifo.buf[: end + 3]
                    self.gdb_fifo.write(b"+")
                    return reply
            rem = deadline - time.monotonic()
            if rem <= 0:
                raise TimeoutError(f"Timed out waiting for GDB RSP reply to {body}")
            chunk = self.gdb_fifo.read_some(min(rem, 0.2))
            if chunk:
                self.gdb_fifo.buf.extend(chunk)

    def write_u32(self, addr: int, val: int) -> None:
        hex_le = struct.pack("<I", val & 0xFFFFFFFF).hex()
        reply = self._gdb_packet(f"M{addr:08x},4:{hex_le}")
        if reply != "OK":
            raise AssertionError(f"GDB write M{addr:08x} failed: {reply!r}")

    def read_u32(self, addr: int) -> int:
        reply = self._gdb_packet(f"m{addr:08x},4")
        if reply.startswith("E") or len(reply) != 8:
            raise AssertionError(f"GDB read m{addr:08x} failed: {reply!r}")
        return struct.unpack("<I", bytes.fromhex(reply))[0]

    def send_ipcl(self, grp: int, sub: int, payload: bytes) -> None:
        frame = build_ipcl_frame(grp, sub, payload)
        self.write_u32(VIP_MMIO_TX_RESET, 0)
        for byte in frame:
            self.write_u32(VIP_MMIO_TX_DATA, byte)
        self.write_u32(VIP_MMIO_TX_COMMIT, 1)

    def pop_ipcl(self) -> tuple[int, int, bytes] | None:
        flen = self.read_u32(VIP_MMIO_RX_POP)
        if flen == 0:
            return None
        raw = bytes(self.read_u32(VIP_MMIO_RX_DATA) & 0xFF for _ in range(flen))
        return parse_ipcl_frame(raw)

    def drain_ipcl(self) -> list[tuple[int, int, bytes]]:
        frames: list[tuple[int, int, bytes]] = []
        while True:
            item = self.pop_ipcl()
            if item is None:
                break
            frames.append(item)
        return frames

    def drain_vip_ctl(self, wait: float = 0.05) -> str:
        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            chunk = self.vip_fifo.read_some(0.02)
            if chunk:
                self.vip_fifo.buf.extend(chunk)
            else:
                break
        text = bytes(self.vip_fifo.buf).decode("utf-8", errors="replace")
        self.vip_fifo.buf.clear()
        return text

    def send_vip_cmd(self, cmd: str, timeout: float = 2.0) -> str:
        self.vip_fifo.write(cmd.encode("ascii") + b"\n")
        deadline = time.monotonic() + timeout
        while True:
            nl = self.vip_fifo.buf.find(b"\n")
            if nl >= 0:
                line = bytes(self.vip_fifo.buf[: nl + 1]).decode("utf-8", errors="replace")
                del self.vip_fifo.buf[: nl + 1]
                return line
            rem = deadline - time.monotonic()
            if rem <= 0:
                raise TimeoutError(f"Timed out waiting for VIP reply to {cmd[:64]!r}")
            chunk = self.vip_fifo.read_some(min(rem, 0.1))
            if chunk:
                self.vip_fifo.buf.extend(chunk)


class TestEmuPlan1VipPssAndShims(unittest.TestCase):
    def test_vip_handles_pss_warm_ram_backup_and_persists_across_reset(self):
        src = VIP_C.read_text()
        self.assertIn("uint8_t pss_warm_ram[512];", src)
        self.assertIn("grp == 0x0a && sub == 0x04 && plen >= 4", src)
        self.assertIn("(uint32_t)addr < sizeof(s->pss_warm_ram)", src)
        self.assertIn("(uint32_t)addr + rlen > sizeof(s->pss_warm_ram)", src)
        self.assertIn("IPCL_MAX_FRAME - 16", src)
        self.assertIn("vip_enqueue_ipcl(s, 0x0a, 0x02", src)
        self.assertIn("grp == 0x0a && sub == 0x05 && plen >= 5", src)
        self.assertIn("wlen <= plen - 5", src)
        self.assertIn("(uint32_t)addr + wlen <= sizeof(s->pss_warm_ram)", src)
        self.assertIn("grp == 0x0a && sub == 0x06", src)
        self.assertIn("sizeof(s->pss_warm_ram)", src)
        # Non-volatile: initialized in vip_realize, NOT cleared in vip_reset_hold
        realize_body = src.split("vip_realize(")[1].split("vip_reset_hold(")[0]
        self.assertIn("memset(s->pss_warm_ram, 0, sizeof(s->pss_warm_ram));", realize_body)
        reset_body = src.split("vip_reset_hold(")[1].split("vip_reset_exit(")[0]
        self.assertNotIn("pss_warm_ram", reset_body)

    def test_vip_pss_warm_ram_runtime_protocol_bounds_and_reset_persistence(self):
        if not QEMU_BIN.exists():
            self.fail(f"Built QEMU binary not found: {QEMU_BIN}")

        with QemuVipHarness() as qemu:
            # 1. Initial warm RAM is zeroed
            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 0, 16))
            frames = qemu.drain_ipcl()
            self.assertEqual(len(frames), 1)
            grp, sub, payload = frames[0]
            self.assertEqual((grp, sub), (0x0A, 0x02))
            self.assertEqual(payload, struct.pack(">HHB", 0, 16, 0) + b"\x00" * 16)

            # 2. Write + read-back at addr=0x20 and near end addr=500, len=12
            slice_mid = b"\x11\x22\x33\x44\x55\x66\x77\x88"
            slice_end = b"\xa1\xb2\xc3\xd4\xe5\xf6\x07\x18\x29\x3a\x4b\x5c"
            qemu.send_ipcl(0x0A, 0x05, struct.pack(">HHB", 0x20, len(slice_mid), 2) + slice_mid)
            qemu.send_ipcl(0x0A, 0x05, struct.pack(">HHB", 500, len(slice_end), 8) + slice_end)
            self.assertEqual(qemu.drain_ipcl(), [])

            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 0x20, len(slice_mid)))
            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 500, len(slice_end)))
            frames = qemu.drain_ipcl()
            self.assertEqual(len(frames), 2)
            self.assertEqual(
                frames[0],
                (0x0A, 0x02, struct.pack(">HHB", 0x20, len(slice_mid), 0) + slice_mid),
            )
            self.assertEqual(
                frames[1],
                (0x0A, 0x02, struct.pack(">HHB", 500, len(slice_end), 0) + slice_end),
            )

            # 3. Bounds clamping and malformed-frame rejection
            # 3a. 0x0A:0x04 with plen < 4 -> no reply enqueued
            qemu.send_ipcl(0x0A, 0x04, b"\x00\x00\x08")
            self.assertEqual(qemu.drain_ipcl(), [])

            # 3b. 0x0A:0x04 with addr=512, len=8 -> no reply enqueued
            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 512, 8))
            self.assertEqual(qemu.drain_ipcl(), [])

            # 3c. 0x0A:0x04 with addr=508, len=20 -> clamped to rlen=4 (bytes 508..511)
            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 508, 20))
            frames = qemu.drain_ipcl()
            self.assertEqual(len(frames), 1)
            self.assertEqual(
                frames[0],
                (0x0A, 0x02, struct.pack(">HHB", 508, 4, 0) + slice_end[8:12]),
            )

            # 3d. Zero-length read at boundary and zero-length write
            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 511, 0))
            frames = qemu.drain_ipcl()
            self.assertEqual(frames, [(0x0A, 0x02, struct.pack(">HHB", 511, 0, 0))])
            qemu.send_ipcl(0x0A, 0x05, struct.pack(">HHB", 0x20, 0, 1))
            self.assertEqual(qemu.drain_ipcl(), [])

            # 3e. 0x0A:0x05 rejections: plen < 5, wlen > plen - 5, addr + wlen > 512
            qemu.send_ipcl(0x0A, 0x05, b"\x01\xf4\x00\x04")
            qemu.send_ipcl(0x0A, 0x05, struct.pack(">HHB", 508, 4, 8) + b"\xff\xff")
            qemu.send_ipcl(0x0A, 0x05, struct.pack(">HHB", 508, 8, 8) + b"\xff" * 8)
            self.assertEqual(qemu.drain_ipcl(), [])

            # Verify bytes 500..511 were not corrupted
            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 500, len(slice_end)))
            frames = qemu.drain_ipcl()
            self.assertEqual(
                frames,
                [(0x0A, 0x02, struct.pack(">HHB", 500, len(slice_end), 0) + slice_end)],
            )

            # 3f. 0x0A:0x06 accepted silently (logged on VIP control FIFO, no reply frame)
            qemu.drain_vip_ctl(wait=0.02)
            qemu.send_ipcl(0x0A, 0x06, b"\x00\x01\x02\x03")
            self.assertEqual(qemu.drain_ipcl(), [])
            ctl_out = qemu.drain_vip_ctl(wait=0.2)
            self.assertIn("HOST_TX 0x0a 0x06", ctl_out)

            # 4. Persistence across QMP system_reset (vip_reset_hold)
            reset_resp = qemu.qmp_cmd("system_reset")
            self.assertIn("return", reset_resp)
            qemu.drain_ipcl()

            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 0x20, len(slice_mid)))
            qemu.send_ipcl(0x0A, 0x04, struct.pack(">HH", 500, len(slice_end)))
            frames = qemu.drain_ipcl()
            self.assertEqual(
                frames,
                [
                    (0x0A, 0x02, struct.pack(">HHB", 0x20, len(slice_mid), 0) + slice_mid),
                    (0x0A, 0x02, struct.pack(">HHB", 500, len(slice_end), 0) + slice_end),
                ],
            )

    def test_coding_seeds_optional_system_setup_block_0x4b(self):
        import sys
        sys.path.insert(0, str(ROOT / "tools"))
        import rcd330_coding

        expected_langs = {
            "en_US": 0,
            "zh_CN": 1,
            "pt_BR": 2,
            "de_DE": 2,
            "ru_RU": 3,
            "es_MX": 4,
        }
        self.assertEqual(rcd330_coding.SYSTEM_LANGUAGES, expected_langs)

        with tempfile.TemporaryDirectory() as td:
            # Default "auto" does not write block 0x4b (preserves unseeded cold-boot path)
            cfg_auto = rcd330_coding.default_coding_config("5624_vw")
            self.assertEqual(cfg_auto["language"], "auto")
            rcd330_coding.seed_rootfs_coding(td, cfg_auto)
            self.assertFalse((pathlib.Path(td) / "pss1/HMI_SYSTEM_SETUP_PSS_NEWBLK.pss").exists())
            self.assertFalse((pathlib.Path(td) / "pss2/HMI_SYSTEM_SETUP_PSS_NEWBLK.pss").exists())

            # Invalid language value normalizes to "auto"
            cfg_invalid = rcd330_coding.normalize_coding_config({"language": "xx_INVALID"})
            self.assertEqual(cfg_invalid["language"], "auto")

            # apply_coding_update merges "language" and normalizes invalid values
            cfg_updated = rcd330_coding.apply_coding_update(cfg_auto, {"language": "de_DE"})
            self.assertEqual(cfg_updated["language"], "de_DE")
            cfg_reverted = rcd330_coding.apply_coding_update(cfg_updated, {"language": "bad_lang"})
            self.assertEqual(cfg_reverted["language"], "auto")

        # All SYSTEM_LANGUAGES mappings produce valid 28-byte CRC-16/CCITT-FALSE PSS blocks
        for lang_name, lang_idx in expected_langs.items():
            with tempfile.TemporaryDirectory() as td:
                cfg_lang = dict(cfg_auto, language=lang_name)
                rcd330_coding.seed_rootfs_coding(td, cfg_lang)
                p1_path = pathlib.Path(td) / "pss1/HMI_SYSTEM_SETUP_PSS_NEWBLK.pss"
                p2_path = pathlib.Path(td) / "pss2/HMI_SYSTEM_SETUP_PSS_NEWBLK.pss"
                p1 = p1_path.read_bytes()
                p2 = p2_path.read_bytes()
                self.assertEqual(p1, p2)
                self.assertEqual(len(p1), 28)
                crc, dlen, tag, cnt, rsv = struct.unpack(">HHBHB", p1[:8])
                payload = p1[8:]
                self.assertEqual(dlen, 20)
                self.assertEqual(tag, 0)
                self.assertEqual(cnt, 1)
                self.assertEqual(rsv, 0)
                self.assertEqual(crc, rcd330_coding.crc16_ccitt_false(payload))
                self.assertEqual(payload[0x03], 1)
                self.assertEqual(payload[0x04], lang_idx)
                self.assertEqual(payload[0x05], 1)
                self.assertEqual(payload[0x06], 1)
                self.assertEqual(payload[0x0B], 0)

                # Switching back to "auto" on a reused stage dir removes stale block 0x4b
                rcd330_coding.seed_rootfs_coding(td, cfg_auto)
                self.assertFalse(p1_path.exists())
                self.assertFalse(p2_path.exists())

    def test_vip_supports_can_lang_command(self):
        src = VIP_C.read_text()
        self.assertIn("can_lang_valid", src)
        self.assertIn("can_lang_code", src)
        self.assertIn('sscanf(line, "LANG %d %d"', src)
        reset_body = src.split("vip_reset_hold(")[1].split("vip_reset_exit(")[0]
        self.assertIn("s->can_lang_valid = 0;", reset_body)
        self.assertIn("s->can_lang_code = 0;", reset_body)

        if not QEMU_BIN.exists():
            self.fail(f"Built QEMU binary not found: {QEMU_BIN}")

        with QemuVipHarness() as qemu:
            qemu.drain_vip_ctl(wait=0.02)
            qemu.drain_ipcl()

            # 1. Send LANG 1 9 -> OK LANG + IPCL 0x06:0x08 (38-byte payload) with [0]=1, [1]=9
            qemu.vip_fifo.write(b"LANG 1 9\n")
            ctl_out = qemu.drain_vip_ctl(wait=0.2)
            self.assertIn("OK LANG", ctl_out)
            frames = qemu.drain_ipcl()
            bem_frames = [pl for grp, sub, pl in frames if (grp, sub) == (0x06, 0x08)]
            self.assertEqual(len(bem_frames), 1)
            self.assertEqual(len(bem_frames[0]), 38)
            self.assertEqual(bem_frames[0][0], 1)
            self.assertEqual(bem_frames[0][1], 9)

            # 2. Send LANG 0 0 -> OK LANG + IPCL 0x06:0x08 with [0]=0, [1]=0
            qemu.vip_fifo.write(b"LANG 0 0\n")
            ctl_out = qemu.drain_vip_ctl(wait=0.2)
            self.assertIn("OK LANG", ctl_out)
            frames = qemu.drain_ipcl()
            bem_frames = [pl for grp, sub, pl in frames if (grp, sub) == (0x06, 0x08)]
            self.assertEqual(len(bem_frames), 1)
            self.assertEqual(len(bem_frames[0]), 38)
            self.assertEqual(bem_frames[0][0], 0)
            self.assertEqual(bem_frames[0][1], 0)

            # 3. Set LANG 1 13, then system_reset, then trigger vip_send_bap_startup via IPCL(0x0C, 0x01) [0x57]
            qemu.vip_fifo.write(b"LANG 1 13\n")
            ctl_out = qemu.drain_vip_ctl(wait=0.2)
            self.assertIn("OK LANG", ctl_out)
            qemu.drain_ipcl()

            reset_resp = qemu.qmp_cmd("system_reset")
            self.assertIn("return", reset_resp)
            qemu.drain_ipcl()

            qemu.send_ipcl(0x0C, 0x01, b"\x57")
            frames = qemu.drain_ipcl()
            bem_frames = [pl for grp, sub, pl in frames if (grp, sub) == (0x06, 0x08)]
            self.assertEqual(len(bem_frames), 1)
            self.assertEqual(len(bem_frames[0]), 38)
            self.assertEqual(bem_frames[0][0], 0)
            self.assertEqual(bem_frames[0][1], 0)

    def test_vip_handbrake_arbitrary_raw_and_qdev_properties(self):
        src = VIP_C.read_text()
        self.assertIn('sscanf(line, "HANDBRAKE %d"', src)
        self.assertIn("handbrake", src)
        reset_body = src.split("vip_reset_hold(")[1].split("vip_reset_exit(")[0]
        self.assertIn("s->lights_mask = 0;", reset_body)
        self.assertIn("s->handbrake = 0;", reset_body)
        # RAW uses vip_parse_hex_bytes instead of 4-int sscanf and prevents silent cmd_buf truncation
        self.assertNotIn('sscanf(line, "RAW %x %x %x %x"', src)
        self.assertIn("ERR RAW\\n", src)
        self.assertIn("cmd_buf[4096]", src)
        self.assertIn("cmd_overflow", src)
        self.assertIn("ERR line too long\\n", src)
        for prop in (
            '"platform"',
            '"brand"',
            '"vehicle"',
            '"can-speed"',
            '"speakers"',
            '"eng-mode"',
            '"part-num"',
            '"coding-hex"',
        ):
            self.assertIn(prop, src)

    def test_run_rcd330_passes_vip_coding_globals_in_dry_run(self):
        import shlex
        import tarfile

        def parse_qemu_tokens(cmd: list[str]) -> list[str]:
            out = subprocess.check_output(cmd, text=True)
            qemu_lines = [ln for ln in out.splitlines() if "-M rcd330" in ln]
            self.assertEqual(len(qemu_lines), 1, f"Expected single QEMU line in:\n{out}")
            return shlex.split(qemu_lines[0])

        mqb_hex = "027300000000000000000000000000002F0018100000000000"
        with tempfile.TemporaryDirectory() as td:
            td_path = pathlib.Path(td)
            nonexistent_coding = td_path / "nonexistent_coding.json"

            dummy_vw_tar = td_path / "std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar"
            dummy_skoda_tar = td_path / "std_plus_pss-6.5inch_5606_SKODA_DE_CV.tar"
            for tar_path in (dummy_vw_tar, dummy_skoda_tar):
                with tarfile.open(tar_path, "w"):
                    pass

            # Default-args check (omits --fw) when stock tarball is present
            stock_5624 = ROOT / "original-fw/std_plus_pss-6.5inch_5624_VW_PQ_ROW.tar"
            default_vw_expected = (
                "rcd330-vip.platform=1",
                "rcd330-vip.brand=2",
                "rcd330-vip.vehicle=19",
                "rcd330-vip.can-speed=1",
                "rcd330-vip.speakers=4",
                "rcd330-vip.eng-mode=0",
                "rcd330-vip.part-num=6RD035187B ",
                "rcd330-vip.coding-hex=0E000440040013",
            )
            if stock_5624.exists():
                default_tokens = parse_qemu_tokens(
                    [
                        str(ROOT / "tools/run_rcd330.sh"),
                        "--headless",
                        "--dry-run",
                        "--coding-file",
                        str(nonexistent_coding),
                    ]
                )
                for expected in default_vw_expected:
                    self.assertIn(expected, default_tokens)

            # Always verify default VW and Skoda --fw resolution using temporary tars (works without original-fw/)
            vw_tokens = parse_qemu_tokens(
                [
                    str(ROOT / "tools/run_rcd330.sh"),
                    "--headless",
                    "--dry-run",
                    "--fw",
                    str(dummy_vw_tar),
                    "--coding-file",
                    str(nonexistent_coding),
                ]
            )
            for expected in default_vw_expected:
                self.assertIn(expected, vw_tokens)

            skoda_tokens = parse_qemu_tokens(
                [
                    str(ROOT / "tools/run_rcd330.sh"),
                    "--headless",
                    "--dry-run",
                    "--fw",
                    str(dummy_skoda_tar),
                    "--coding-file",
                    str(nonexistent_coding),
                ]
            )
            for expected in (
                "rcd330-vip.platform=1",
                "rcd330-vip.brand=1",
                "rcd330-vip.vehicle=20",
                "rcd330-vip.can-speed=1",
                "rcd330-vip.speakers=4",
                "rcd330-vip.eng-mode=0",
                "rcd330-vip.part-num=6RD035187B ",
                "rcd330-vip.coding-hex=0E000440040014",
            ):
                self.assertIn(expected, skoda_tokens)

            mqb_coding_path = td_path / "mqb_coding.json"
            mqb_coding_path.write_text(
                json.dumps(
                    {
                        "platform": "MQB",
                        "brand": "SKODA",
                        "can_speed": "LS",
                        "part_number": "5GD 035 187",
                        "vehicle_mqb": 53,
                        "speakers": 2,
                        "eng_mode": True,
                        "coding_hex": mqb_hex,
                    }
                ),
                encoding="utf-8",
            )
            tokens = parse_qemu_tokens(
                [
                    str(ROOT / "tools/run_rcd330.sh"),
                    "--headless",
                    "--dry-run",
                    "--fw",
                    str(dummy_vw_tar),
                    "--coding-file",
                    str(mqb_coding_path),
                ]
            )
            for expected in (
                "rcd330-vip.platform=2",
                "rcd330-vip.brand=1",
                "rcd330-vip.vehicle=53",
                "rcd330-vip.can-speed=2",
                "rcd330-vip.speakers=2",
                "rcd330-vip.eng-mode=1",
                "rcd330-vip.part-num=5GD 035 187",
                f"rcd330-vip.coding-hex={mqb_hex}",
            ):
                self.assertIn(expected, tokens)

            pq_hex = "0E000240040014"
            pq_coding_path = td_path / "pq_coding.json"
            pq_coding_path.write_text(
                json.dumps(
                    {
                        "platform": "PQ",
                        "brand": "SKODA",
                        "can_speed": "HS",
                        "part_number": "56D035187B",
                        "speakers": 2,
                        "eng_mode": False,
                        "coding_hex": pq_hex,
                    }
                ),
                encoding="utf-8",
            )
            pq_tokens = parse_qemu_tokens(
                [
                    str(ROOT / "tools/run_rcd330.sh"),
                    "--headless",
                    "--dry-run",
                    "--fw",
                    str(dummy_vw_tar),
                    "--coding-file",
                    str(pq_coding_path),
                ]
            )
            for expected in (
                "rcd330-vip.platform=1",
                "rcd330-vip.brand=1",
                "rcd330-vip.vehicle=20",
                "rcd330-vip.can-speed=1",
                "rcd330-vip.speakers=2",
                "rcd330-vip.eng-mode=0",
                "rcd330-vip.part-num=56D035187B ",
                f"rcd330-vip.coding-hex={pq_hex}",
            ):
                self.assertIn(expected, pq_tokens)

    def test_vip_handbrake_and_raw_runtime_protocol(self):
        if not QEMU_BIN.exists():
            self.fail(f"Built QEMU binary not found: {QEMU_BIN}")

        with QemuVipHarness() as qemu:
            qemu.drain_vip_ctl(wait=0.02)
            qemu.drain_ipcl()

            # 1. HANDBRAKE 1 / HANDBRAKE 0 / backwards-compat LIGHTS 1
            self.assertEqual(qemu.send_vip_cmd("HANDBRAKE 1"), "OK HANDBRAKE\n")
            self.assertEqual(qemu.drain_ipcl(), [(0x06, 0x03, b"\x01\x00")])

            self.assertEqual(qemu.send_vip_cmd("HANDBRAKE 0"), "OK HANDBRAKE\n")
            self.assertEqual(qemu.drain_ipcl(), [(0x06, 0x03, b"\x00\x00")])

            self.assertEqual(qemu.send_vip_cmd("LIGHTS 1"), "OK LIGHTS\n")
            self.assertEqual(qemu.drain_ipcl(), [(0x06, 0x03, b"\x01\x00")])

            # 2. Valid RAW payloads (0-byte, 2-byte, 8-byte, colon-separated, max 1016 bytes)
            self.assertEqual(qemu.send_vip_cmd("RAW 06 03"), "OK RAW\n")
            self.assertEqual(qemu.drain_ipcl(), [(0x06, 0x03, b"")])

            self.assertEqual(qemu.send_vip_cmd("RAW 06 03 01 02"), "OK RAW\n")
            self.assertEqual(qemu.drain_ipcl(), [(0x06, 0x03, b"\x01\x02")])

            self.assertEqual(qemu.send_vip_cmd("RAW 0c 02 0102030405060708"), "OK RAW\n")
            self.assertEqual(
                qemu.drain_ipcl(),
                [(0x0C, 0x02, bytes.fromhex("0102030405060708"))],
            )

            self.assertEqual(qemu.send_vip_cmd("RAW 0c 02 aa:bb:cc"), "OK RAW\n")
            self.assertEqual(qemu.drain_ipcl(), [(0x0C, 0x02, b"\xaa\xbb\xcc")])

            max_payload = bytes(range(256)) * 3 + bytes(range(248))  # 1016 == IPCL_MAX_FRAME - 8
            self.assertEqual(len(max_payload), 1016)
            self.assertEqual(qemu.send_vip_cmd(f"RAW 0c 02 {max_payload.hex()}"), "OK RAW\n")
            self.assertEqual(qemu.drain_ipcl(), [(0x0C, 0x02, max_payload)])

            # 3. Invalid RAW commands must reply ERR RAW and enqueue nothing
            invalid_raw_cmds = [
                "RAW 06 03 zz",
                "RAW 06 03 123",
                "RAW 06 03 1 2",
                "RAW 06 03zz",
                "RAW 100 01 00",
                "RAW 06 100 00",
                f"RAW 06 03 {'ab' * 1017}",  # 1017 bytes = 2034 hex chars (> IPCL_MAX_FRAME - 8)
            ]
            for bad_cmd in invalid_raw_cmds:
                with self.subTest(cmd=bad_cmd[:32]):
                    self.assertEqual(qemu.send_vip_cmd(bad_cmd), "ERR RAW\n")
                    self.assertEqual(qemu.drain_ipcl(), [])

            # 4. Line exceeding cmd_buf (4096) triggers ERR line too long without truncating
            too_long = "RAW 06 03 " + ("aa" * 2100)  # 4210 chars
            self.assertEqual(qemu.send_vip_cmd(too_long), "ERR line too long\n")
            self.assertEqual(qemu.drain_ipcl(), [])
            # Chardev stays healthy after overflow
            self.assertEqual(qemu.send_vip_cmd("PING"), "PONG rcd330-vip\n")

    def test_vip_qdev_startup_properties_runtime(self):
        if not QEMU_BIN.exists():
            self.fail(f"Built QEMU binary not found: {QEMU_BIN}")

        # 25-byte MQB coding with byte 19 == 0x00 (RVC disabled) to prove 25-byte coding-hex took effect
        mqb_coding_no_rvc = "027300000000000000000000000000002F0018000000000000"
        extra_args = [
            "-global", "rcd330-vip.platform=2",
            "-global", "rcd330-vip.brand=1",
            "-global", "rcd330-vip.vehicle=53",
            "-global", "rcd330-vip.can-speed=2",
            "-global", "rcd330-vip.speakers=2",
            "-global", "rcd330-vip.eng-mode=1",
            "-global", "rcd330-vip.part-num=5GD035187B ",
            "-global", f"rcd330-vip.coding-hex={mqb_coding_no_rvc}",
        ]
        with QemuVipHarness(extra_args=extra_args) as qemu:
            qemu.drain_vip_ctl(wait=0.02)
            qemu.drain_ipcl()

            # Query VRM (0x0E:0x01 [0x01]) -> 0x0E:0x02 [0x02, plat=2, brand=1, veh=53, 0, part[11], can=2]
            qemu.send_ipcl(0x0E, 0x01, b"\x01")
            frames = qemu.drain_ipcl()
            self.assertEqual(
                frames,
                [(0x0E, 0x02, bytes([0x02, 2, 1, 53, 0]) + b"5GD035187B " + bytes([2]))],
            )

            # Query HW info (0x0E:0x01 [0x03]) -> 0x0E:0x02 [0x04, part[11], ...]
            qemu.send_ipcl(0x0E, 0x01, b"\x03")
            hw_frames = qemu.drain_ipcl()
            self.assertEqual(len(hw_frames), 1)
            self.assertEqual(hw_frames[0][0:2], (0x0E, 0x02))
            self.assertEqual(hw_frames[0][2][:12], b"\x045GD035187B ")

            # Query speakers (0x07:0x00 [0x03, 0x06, 0x00, 0x01]) -> 0x07:0x01 [0x04, 0x06, 0x00, 0x01, 2]
            qemu.send_ipcl(0x07, 0x00, b"\x03\x06\x00\x01")
            self.assertEqual(qemu.drain_ipcl(), [(0x07, 0x01, b"\x04\x06\x00\x01\x02")])

            # Query eng-mode (0x07:0x00 [0x03, 0x06, 0x00, 0x07]) -> 0x07:0x01 [0x04, 0x06, 0x00, 0x07, 1]
            qemu.send_ipcl(0x07, 0x00, b"\x03\x06\x00\x07")
            self.assertEqual(qemu.drain_ipcl(), [(0x07, 0x01, b"\x04\x06\x00\x07\x01")])

            # Drain HOST_TX notifications emitted by MMIO queries before sending socket command
            ctl_log = qemu.drain_vip_ctl(wait=0.05)
            self.assertIn("HOST_TX 0x0e 0x01", ctl_log)

            # REV 1 checks vip_is_rvc_coded(s) on MQB byte 19 (0x00 -> OPS mode 1 instead of RVC mode 4)
            self.assertEqual(qemu.send_vip_cmd("REV 1"), "OK REV\n")
            rev_frames = qemu.drain_ipcl()
            popup_frames = [pl for grp, sub, pl in rev_frames if (grp, sub) == (0x0C, 0x02) and pl[:2] == b"\x4b\x02"]
            self.assertEqual(popup_frames, [b"\x4b\x02\x01\x00"])

            # Verify startup properties persist across system_reset
            reset_resp = qemu.qmp_cmd("system_reset")
            self.assertIn("return", reset_resp)
            qemu.drain_ipcl()
            qemu.send_ipcl(0x0E, 0x01, b"\x01")
            self.assertEqual(
                qemu.drain_ipcl(),
                [(0x0E, 0x02, bytes([0x02, 2, 1, 53, 0]) + b"5GD035187B " + bytes([2]))],
            )

    def test_hal_gpio_shim_rx_stash_1024_immediate_tx_sigio_and_log_tags(self):
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        self.assertIn("g_rx_stash[1024]", shim_c)
        self.assertIn("QEMU_LOG_TAGS", shim_c)
        self.assertIn("a10", shim_c)
        self.assertIn("pthread_once", shim_c)
        self.assertIn("#ifndef _GNU_SOURCE", shim_c)

        msgsnd_body = shim_c.split("int msgsnd(")[1].split("\n}\n")[0]
        self.assertIn("kill(getpid(), SIGIO)", msgsnd_body)
        self.assertIn("__atomic_load_n(&g_srq_thread_started, __ATOMIC_ACQUIRE)", msgsnd_body)

        fcntl_body = shim_c.split("int fcntl(")[1].split("\n}\n")[0]
        self.assertIn("g_srq_thread_created", fcntl_body)
        idx_mmio = fcntl_body.find("get_vip_mmio(real_open)")
        idx_create = fcntl_body.find("pthread_create(")
        idx_unlock = fcntl_body.find("pthread_mutex_unlock(&g_shim_lock)")
        idx_store = fcntl_body.find("__atomic_store_n(&g_srq_thread_started, 1, __ATOMIC_RELEASE)")
        self.assertGreater(idx_mmio, -1)
        self.assertGreater(idx_create, idx_mmio)
        self.assertGreater(idx_unlock, idx_create)
        self.assertGreater(idx_store, idx_unlock)

        srq_body = shim_c.split("v850srq_sigio_thread(")[1].split("\n}\n")[0]
        self.assertIn("usleep(5000)", srq_body)
        self.assertNotIn("usleep(20000)", srq_body)

        event0_body = shim_c.split("event0_poll_thread(")[1].split("\n}\n")[0]
        self.assertIn("usleep(5000)", event0_body)
        self.assertNotIn("usleep(20000)", event0_body)

    def test_hal_gpio_shim_log_filter_and_10arg_formatting_runtime(self):
        harness = r"""
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "hal_gpio_shim.c"

static void *thread_worker(void *arg) {
    unsigned long tid = (unsigned long)(uintptr_t)arg;
    for (unsigned long i = 0; i < 5; i++) {
        log_simple("PSS", "thr-%lu-%lu", tid, i, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("NOPE", "skip-%lu-%lu", tid, i, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *mode = argv[1];

    if (strcmp(mode, "default") == 0 || strcmp(mode, "empty") == 0) {
        assert(log_matches_filter("HMI", " ==== HMI ==== boot ok") == 1);
        assert(log_matches_filter(NULL, " ==== HMI ==== null tag") == 1);
        assert(log_matches_filter("HMI", " ==== HMI ==== BTsubsystemNotify x") == 0);
        assert(log_matches_filter("HMI", " ==== HMI ==== PA_OUT_DEV_STS x") == 0);
        assert(log_matches_filter("HMI", " ==== HMI ==== PA_OUT_VRM_INTO x") == 0);
        assert(log_matches_filter("HMI", " ==== HMI ==== PA_OUT_WAKE_UP_REASON x") == 0);
        assert(log_matches_filter("HMI", " ==== HMI ==== PA_OUT_MODE_CHG x") == 0);
        assert(log_matches_filter("PSS", "unrelated line") == 0);
        assert(log_matches_filter("HMI", NULL) == 0);

        log_simple("HMI", " ==== HMI ==== hello %lu", 7UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("HMI", " ==== HMI ==== PA_OUT_DEV_STS %lu", 1UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("PSS", "plain %lu", 2UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("HMI", NULL, 3UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    } else if (strcmp(mode, "wildcard") == 0) {
        assert(log_matches_filter("ANY", "arbitrary line") == 1);
        assert(log_matches_filter(NULL, "no tag line") == 1);
        assert(log_matches_filter("ANY", NULL) == 0);

        log_simple("ANY", "wild=%lu", 5UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("ANY", NULL, 6UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("T10", "%lu-%lu-%lu-%lu-%lu-%lu-%lu-%lu-%lu-%lu",
                   11UL, 22UL, 33UL, 44UL, 55UL, 66UL, 77UL, 88UL, 99UL, 1010UL);
        log_simple("T2", "pair=%lu:%lu",
                   42UL, 84UL, 999UL, 999UL, 999UL, 999UL, 999UL, 999UL, 999UL, 999UL);
    } else if (strcmp(mode, "tags") == 0) {
        assert(log_matches_filter("PSS", "msg by tag") == 1);
        assert(log_matches_filter("svc_VGW_tx", "msg by tag") == 1);
        assert(log_matches_filter("OTHER", "msg with PSS in fmt") == 1);
        assert(log_matches_filter(NULL, "VGW rx frame") == 1);
        assert(log_matches_filter("HMI", " ==== HMI ==== unrelated") == 0);
        assert(log_matches_filter("PSS", NULL) == 0);

        log_simple("PSS", "by_tag=%lu", 1UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("OTHER", "VGW_fmt=%lu", 2UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        log_simple("HMI", " ==== HMI ==== drop=%lu", 3UL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    } else if (strcmp(mode, "empty_seg") == 0) {
        assert(log_matches_filter("PSS", "hit") == 1);
        assert(log_matches_filter("OTHER", "in PSS fmt") == 1);
        assert(log_matches_filter("OTHER", "miss") == 0);
        assert(log_matches_filter(NULL, "miss") == 0);
    } else if (strcmp(mode, "comma_only") == 0) {
        assert(log_matches_filter("PSS", "anything") == 0);
    } else if (strcmp(mode, "threads") == 0) {
        pthread_t th[4];
        for (unsigned long t = 0; t < 4; t++) {
            assert(pthread_create(&th[t], NULL, thread_worker, (void *)(uintptr_t)t) == 0);
        }
        for (unsigned long t = 0; t < 4; t++) {
            assert(pthread_join(th[t], NULL) == 0);
        }
    } else {
        return 2;
    }
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            src = tmp_path / "test_hal_log_shim.c"
            bin_path = tmp_path / "test_hal_log_shim"
            src.write_text(harness)
            comp = subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    f"-I{ROOT / 'tools/arm_hal_shim'}",
                    str(src),
                    "-o",
                    str(bin_path),
                    "-ldl",
                    "-lpthread",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                comp.returncode,
                0,
                f"Host C test compile failed:\nstdout:\n{comp.stdout}\nstderr:\n{comp.stderr}",
            )
            self.assertEqual(
                comp.stderr.strip(),
                "",
                f"Expected warning-free C compile, got:\n{comp.stderr}",
            )

            base_env = {k: v for k, v in os.environ.items() if k != "QEMU_LOG_TAGS"}

            def run_mode(mode: str, tags_val: str | None) -> str:
                env = dict(base_env)
                if tags_val is not None:
                    env["QEMU_LOG_TAGS"] = tags_val
                res = subprocess.run(
                    [str(bin_path), mode],
                    env=env,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    res.returncode,
                    0,
                    f"mode={mode} tags={tags_val!r} failed ({res.returncode}):\nstderr:\n{res.stderr}",
                )
                return res.stderr

            self.assertEqual(run_mode("default", None), " ==== HMI ==== hello 7\n")
            self.assertEqual(run_mode("empty", ""), " ==== HMI ==== hello 7\n")
            self.assertEqual(
                run_mode("wildcard", "*"),
                "wild=5\n11-22-33-44-55-66-77-88-99-1010\npair=42:84\n",
            )
            self.assertEqual(run_mode("tags", "PSS,VGW"), "by_tag=1\nVGW_fmt=2\n")
            self.assertEqual(run_mode("empty_seg", ",PSS,"), "")
            self.assertEqual(run_mode("comma_only", ","), "")

            thr_out = run_mode("threads", "PSS")
            lines = sorted(thr_out.splitlines())
            expected_lines = sorted(f"thr-{t}-{i}" for t in range(4) for i in range(5))
            self.assertEqual(lines, expected_lines)

    def test_prebuilt_libhal_gpio_shim_elf_and_glibc_2_13_compat(self):
        import re

        so_path = ROOT / "tools/prebuilt/libhal_gpio_shim.so"
        data = so_path.read_bytes()
        self.assertGreaterEqual(len(data), 52)

        # ELF32 header checks: ARM (EM_ARM=40), little-endian (ELFDATA2LSB=1), ET_DYN=3
        e_ident = data[:16]
        self.assertEqual(e_ident[:4], b"\x7fELF")
        self.assertEqual(e_ident[4], 1, "Expected ELFCLASS32")
        self.assertEqual(e_ident[5], 1, "Expected ELFDATA2LSB")
        e_type, e_machine = struct.unpack_from("<HH", data, 16)
        self.assertEqual(e_type, 3, "Expected ET_DYN shared object")
        self.assertEqual(e_machine, 40, "Expected EM_ARM")

        e_shoff = struct.unpack_from("<I", data, 32)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
        self.assertEqual(e_shentsize, 40)
        self.assertGreater(e_shnum, 0)

        sections = []
        for idx in range(e_shnum):
            off = e_shoff + idx * e_shentsize
            sh_name, sh_type, _sh_flags, _sh_addr, sh_offset, sh_size, sh_link, _sh_info, _sh_align, sh_entsize = struct.unpack_from(
                "<10I", data, off
            )
            sections.append((sh_name, sh_type, sh_offset, sh_size, sh_link, sh_entsize))

        def read_cstr(blob: bytes, start: int) -> str:
            end = blob.find(b"\x00", start)
            if end < 0:
                end = len(blob)
            return blob[start:end].decode("ascii", errors="replace")

        # 1. Verify log_simple is exported in .dynsym (SHT_DYNSYM = 11)
        dynsym_secs = [s for s in sections if s[1] == 11]
        self.assertEqual(len(dynsym_secs), 1)
        _, _, sym_off, sym_sz, dynstr_idx, sym_entsz = dynsym_secs[0]
        self.assertEqual(sym_entsz, 16)
        _, _, str_off, str_sz, _, _ = sections[dynstr_idx]
        dynstr = data[str_off : str_off + str_sz]

        exported_syms = set()
        for pos in range(sym_off, sym_off + sym_sz, 16):
            st_name, _st_val, _st_sz, _st_info, _st_other, st_shndx = struct.unpack_from("<IIIBBH", data, pos)
            if st_shndx != 0:
                exported_syms.add(read_cstr(dynstr, st_name))
        self.assertIn("log_simple", exported_syms)

        # 2. Verify all SHT_GNU_verneed (0x6ffffffe) GLIBC_X.Y entries are <= (2, 13)
        verneed_secs = [s for s in sections if s[1] == 0x6FFFFFFE]
        self.assertGreater(len(verneed_secs), 0)
        glibc_versions = []
        for _, _, vn_base, _vn_sz, vn_link, _ in verneed_secs:
            _, _, vn_str_off, vn_str_sz, _, _ = sections[vn_link]
            vn_str = data[vn_str_off : vn_str_off + vn_str_sz]
            cur = vn_base
            while True:
                _vn_ver, vn_cnt, _vn_file, vn_aux, vn_next = struct.unpack_from("<HHIII", data, cur)
                aux = cur + vn_aux
                for _ in range(vn_cnt):
                    _hash, _flags, _other, vna_name, vna_next = struct.unpack_from("<IHHII", data, aux)
                    ver_name = read_cstr(vn_str, vna_name)
                    if ver_name.startswith("GLIBC_"):
                        glibc_versions.append(ver_name)
                    if vna_next == 0:
                        break
                    aux += vna_next
                if vn_next == 0:
                    break
                cur += vn_next

        self.assertGreater(len(glibc_versions), 0, "Expected GLIBC_* version needs")
        for ver in glibc_versions:
            m = re.fullmatch(r"GLIBC_(\d+)\.(\d+)(?:\.(\d+))?", ver)
            self.assertIsNotNone(m, f"Unexpected GLIBC version format: {ver}")
            maj, min_v = int(m.group(1)), int(m.group(2))
            self.assertLessEqual((maj, min_v), (2, 13), f"Symbol version {ver} exceeds GLIBC_2.13")

        # Defense in depth: scan raw binary for any GLIBC_X.Y string > (2, 13)
        for m in re.finditer(rb"GLIBC_(\d+)\.(\d+)", data):
            maj, min_v = int(m.group(1)), int(m.group(2))
            self.assertLessEqual(
                (maj, min_v),
                (2, 13),
                f"Embedded GLIBC version {m.group(0).decode()} exceeds GLIBC_2.13",
            )


if __name__ == "__main__":
    unittest.main()
