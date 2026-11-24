#!/usr/bin/env python3
import io
import json
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


class TestBugfixesAndCoding(unittest.TestCase):
    # --- Bug 1: Ignition OFF flicker & CL_15 boot default ---
    def test_vip_ignition_power_state_machine_prevents_standby_overwrite(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        self.assertIn("vip_current_pwr_mode", vip_c)
        self.assertIn("vip_send_power_state", vip_c)
        self.assertIn("vip_send_audio_mode", vip_c)
        self.assertIn("startup_synced", vip_c)
        # Must NOT hardcode PWR_STATE_RUN (0x03) inside vip_send_startup_handshake
        self.assertNotIn("run_mode[] = { 0x03, 0x03", vip_c)
        # DEV_STS (0x01, 0x03) must be sent BEFORE MODE_CHG (0x01, 0x01) in vip_send_power_state
        pwr_fn = vip_c.split("vip_send_power_state(")[1].split("\n}")[0]
        idx_dev = pwr_fn.find("vip_enqueue_ipcl(s, 0x01, 0x03")
        idx_mode = pwr_fn.find("vip_enqueue_ipcl(s, 0x01, 0x01")
        self.assertGreater(idx_dev, -1)
        self.assertGreater(idx_mode, -1)
        self.assertLess(idx_dev, idx_mode, "DEV_STS must be enqueued before MODE_CHG")
        # grp == 0x08 && sub == 0x03 must dispatch on pl[0] and NOT trigger startup handshake
        hmi_block = vip_c.split("grp == 0x08 && sub == 0x03")[1].split("if (grp ==")[0]
        self.assertNotIn("vip_send_startup_handshake", hmi_block)
        self.assertIn("pl[0] == 0x01", hmi_block)
        self.assertIn("pl[0] == 0x02", hmi_block)
        # Watchdog fallback must only trigger on pl[0] == 0x02 while !startup_synced
        wd_block = vip_c.split("grp == 0x01 && sub == 0x03")[1].split("if (grp ==")[0]
        self.assertIn("pl[0] == 0x02", wd_block)
        self.assertIn("!s->startup_synced", wd_block)
        # Boot default CL_15 (ign_state = 2, pwr_off = 0)
        self.assertIn("s->ign_state = 2;", vip_c)
        self.assertIn("s->pwr_off = 0;", vip_c)

    # --- Bug 2: VRM, Coding, RVC & OPS Activation ---
    def test_vip_implements_vrm_hwinfo_coding_rvc_and_ops_popups(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        self.assertIn("vip_send_vrm", vip_c)
        self.assertIn("vip_send_hw_info", vip_c)
        self.assertIn("vip_send_ops_config", vip_c)
        self.assertIn("vip_send_popup_ack", vip_c)
        # VRM (IPCL 0x0E:0x02) & OPS sensor count (0x0C) & Popup Ack (0x4B, 0x02)
        self.assertIn("vip_enqueue_ipcl(s, 0x0e, 0x02", vip_c)
        self.assertIn("0x4b, 0x02", vip_c)
        # Handle HMI popup control requests on IPCL(0x0C, 0x01) [0x49]
        self.assertIn("grp == 0x0c && sub == 0x01", vip_c)
        self.assertIn("pl[0] == 0x49", vip_c)
        # Handle opt/diag queries on IPCL(0x07, 0x00) and VRM/HW queries on IPCL(0x0E, 0x01)
        self.assertIn("grp == 0x07 && sub == 0x00", vip_c)
        self.assertIn("grp == 0x0e && sub == 0x01", vip_c)
        # Control socket commands for coding & vehicle setup
        for cmd_tok in ("VRM ", "OPS_CFG ", "SPEAKERS ", "ENGMODE ", "CODING ", "CODING_SYNC "):
            self.assertIn(cmd_tok, vip_c)

    # --- Bug 3: Exterior Temp (-50 fix) & ETM Vehicle Signals ---
    def test_vip_exterior_temp_in_payload_byte1_and_sent_on_startup(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        car_fn = vip_c.split("vip_send_car_status(")[1].split("\n}")[0]
        self.assertNotIn("{ temp_raw, 0,", car_fn)
        self.assertIn("{ s->light_sensor, temp_raw,", car_fn)
        hs_fn = vip_c.split("vip_send_startup_handshake(")[1].split("\n}")[0]
        bap_fn = vip_c.split("vip_send_bap_startup(")[1].split("\n}")[0]
        self.assertIn("vip_send_bap_startup(s);", hs_fn)
        self.assertIn("vip_send_car_status(s);", bap_fn)
        self.assertIn("vip_send_ops_config(s);", bap_fn)
        self.assertIn("vip_send_vrm(s);", hs_fn)
        self.assertIn("vip_send_hw_info(s);", hs_fn)

    def test_vip_etm_clamps_cl58_light_sensor_and_battery_bem(self):
        """Verify IPCL 0x06:0x04 ([CL_S, CL_15, CL_58]), IPCL 0x06:0x06 (light sensor,
        MQB CL_58d/CL_58s, night mode), IPCL 0x08:0x03[0x13]->0x08:0x02[0x8b] (battery
        voltage), and IPCL 0x06:0x08 (BEM low-battery warning & shutdown popups)."""
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()

        # 1. CL_S / CL_15 / CL_58 sent over IPCL(0x06, 0x04) -> VMF(0xA2, 0x03)
        self.assertIn("vip_send_clamp_state", vip_c)
        clamp_fn = vip_c.split("vip_send_clamp_state(")[1].split("\n}")[0]
        self.assertIn("vip_enqueue_ipcl(s, 0x06, 0x04", clamp_fn)
        self.assertIn("s->ign_state >= 1", clamp_fn)
        self.assertIn("s->ign_state >= 2", clamp_fn)
        self.assertIn("s->cl_58", clamp_fn)

        # Power state updates & BAP startup must both push clamp state so ETM CL_15 tracks IGN
        pwr_fn = vip_c.split("vip_send_power_state(")[1].split("\n}")[0]
        bap_fn = vip_c.split("vip_send_bap_startup(")[1].split("\n}")[0]
        self.assertIn("vip_send_clamp_state(s);", pwr_fn)
        self.assertIn("vip_send_clamp_state(s);", bap_fn)
        self.assertIn("vip_send_bem_car_info(s);", bap_fn)
        self.assertIn("vip_send_battery_voltage(s);", bap_fn)

        # FUEL command must NOT enqueue any IPCL frame (IPCL 0x06:0x04 is [CL_S, CL_15, CL_58])
        fuel_block = vip_c.split('sscanf(line, "FUEL %d"')[1].split("} else if")[0]
        self.assertNotIn("vip_enqueue_ipcl", fuel_block)

        # 2. IPCL(0x06, 0x06) carries PQ light_sensor, MQB CL_58d/CL_58s, and night_mode
        car_fn = vip_c.split("vip_send_car_status(")[1].split("\n}")[0]
        self.assertIn("s->light_sensor", car_fn)
        self.assertIn("s->cl_58d", car_fn)
        self.assertIn("s->cl_58s", car_fn)

        # 3. Battery voltage query IPCL(0x08, 0x03) [0x13] -> IPCL(0x08, 0x02) [0x8b, state, hi, lo]
        self.assertIn("vip_send_battery_voltage", vip_c)
        batt_fn = vip_c.split("vip_send_battery_voltage(")[1].split("\n}")[0]
        self.assertIn("0x8b", batt_fn)
        self.assertIn("s->batt_dv", batt_fn)
        self.assertIn("vip_enqueue_ipcl(s, 0x08, 0x02", batt_fn)
        hmi_block = vip_c.split("grp == 0x08 && sub == 0x03")[1].split("if (grp ==")[0]
        self.assertIn("pl[0] == 0x13", hmi_block)
        self.assertIn("vip_send_battery_voltage(s);", hmi_block)

        # 4. BEM low-battery warning/shutdown on IPCL(0x06, 0x08) -> VMF(0xA2, 0x06)
        self.assertIn("vip_send_bem_car_info", vip_c)
        bem_fn = vip_c.split("vip_send_bem_car_info(")[1].split("\n}")[0]
        self.assertIn("vip_enqueue_ipcl(s, 0x06, 0x08", bem_fn)
        self.assertIn("V03.70.03", bem_fn)
        self.assertIn("V03.05.01", bem_fn)
        self.assertIn("115", bem_fn)
        self.assertIn("105", bem_fn)

        # 5. Control socket commands & reset defaults
        for cmd_tok in ('"CL58 %d"', '"CL58D %d"', '"CL58S %d"', '"LIGHT %d"', '"BATT %d"'):
            self.assertIn(cmd_tok, vip_c)
        reset_fn = vip_c.split("static void vip_reset_hold(")[1].split("\n}")[0]
        self.assertIn("s->cl_58 = 0;", reset_fn)
        self.assertIn("s->cl_58d = 0;", reset_fn)
        self.assertIn("s->cl_58s = 0;", reset_fn)
        self.assertIn("s->light_sensor = 128;", reset_fn)
        self.assertIn("s->batt_dv = 126;", reset_fn)

    # --- Bug 4: Real Volume Overlay Bar & VIP DSP ---
    def test_vip_volume_uses_real_ipcl_and_dsp_replies_and_tuner_idle_zero(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        # Tuner idle state must be 0x00 (not 0x09 which freezes FM/AM volume in 0x4aed0)
        self.assertNotIn("st[13] = 0x09;", vip_c)
        self.assertNotIn("bc[13] = 0x09;", vip_c)
        self.assertIn("st[13] = 0x00;", vip_c)
        self.assertIn("bc[13] = 0x00;", vip_c)
        # VIP DSP replies on IPCL(0x03, 0x00) -> IPCL(0x03, 0x02)
        self.assertIn("grp == 0x03 && sub == 0x00", vip_c)
        self.assertIn("vip_enqueue_ipcl(s, 0x03, 0x02", vip_c)
        for opcode in ("0x58", "0x56", "0x40", "0x41", "0x42", "0x57"):
            self.assertIn(opcode, vip_c)
        # Real volume knob (0x8c, 0x15/0x16) and SWC/Mute (0x83, 0x57/0x58/0x5d)
        self.assertIn("0x8c", vip_c)
        self.assertIn("0x15", vip_c)
        self.assertIn("0x16", vip_c)
        self.assertIn("0x57", vip_c)
        self.assertIn("0x58", vip_c)
        self.assertIn("0x5d", vip_c)
        # Obsolete vol_q / MMIO 0x24 shortcut removed
        self.assertNotIn("vol_q", vip_c)
        self.assertNotIn("offset == 0x24", vip_c)
        self.assertNotIn("mmio[0x24 / 4]", shim_c)

    # --- Bug 5: System Info Versions (BT, CarPlay, Android Auto) ---
    def test_sysinfo_fixes_in_build_rootfs_init_and_hal_shim(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        init_c = (ROOT / "tools/arm_init/init.c").read_text()
        shim_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()
        # /bin/bash must NOT be a symlink to busybox (no bash applet in busybox-armv5l!)
        self.assertNotIn('ln -sf busybox "$FACTORY_BASE_STAGE/bin/bash"', build_sh)
        self.assertIn("exec /bin/ash", build_sh)
        # /sbin/init must mount usbfs, create /var/run, and put /opt and . in PATH for start_carplay.sh
        self.assertIn("/proc/bus/usb", init_c)
        self.assertIn("/var/run", init_c)
        self.assertIn('setenv("PATH"', init_c)
        self.assertIn("/opt", init_c)
        # hal_gpio_shim must virtualize /dev/ttymxc4 for opt/bt and announce BT_VER from stdplus.ini
        self.assertIn("/dev/ttymxc4", shim_c)
        self.assertIn("BT_VER", shim_c)
        # hal_gpio_shim msgget must not prematurely create AA SysV queues (100, 300)
        self.assertIn("key != 100", shim_c)
        self.assertIn("key != 300", shim_c)

    # --- Coding Model, CRC-16, PSS Generation, Validation & Persistence ---
    def test_coding_crc16_pss_validation_and_rootfs_seeding(self):
        import rcd330_coding as rc

        # Standard CRC-16/CCITT-FALSE check value
        self.assertEqual(rc.crc16_ccitt_false(b"123456789"), 0x29B1)

        # Default PQ coding must pass vipImage.bin 0x1f48a validator cleanly
        cfg = rc.default_coding_config("std_plus_pss-6.5inch_5624_VW_PQ_ROW")
        self.assertEqual(cfg["platform"], "PQ")
        self.assertEqual(cfg["brand"], "VW")
        self.assertEqual(rc.validate_pq_coding(bytes.fromhex(cfg["coding_hex"])), [])
        dec = rc.decode_coding_fields(cfg)
        self.assertTrue(dec["bt_enabled"])
        self.assertTrue(dec["rvc_enabled"])
        self.assertEqual(dec["vehicle_pq"], 19)

        # Validator enforces vipImage.bin 0x1f48a rules
        self.assertEqual(rc.validate_pq_coding(bytes.fromhex("16020040010005")), [])
        self.assertNotEqual(rc.validate_pq_coding(bytes.fromhex("00000000000000")), [])
        self.assertNotEqual(rc.validate_pq_coding(bytes.fromhex("0E000440040113")), [])  # B5 != 0
        self.assertNotEqual(rc.validate_pq_coding(bytes.fromhex("0E000441040013")), [])  # B3 bit0 rejected
        self.assertNotEqual(rc.validate_pq_coding(bytes.fromhex("0E020440010013")), [])  # B1=02 requires B2=00

        # PQ BT bit (Byte 3 bit 5) is inverted: disabling BT sets bit 5
        cfg_nobt = rc.apply_coding_update(cfg, {"bt_enabled": False, "rvc_enabled": False})
        raw_nobt = bytes.fromhex(cfg_nobt["coding_hex"])
        self.assertEqual(raw_nobt[3] & 0x60, 0x20)
        self.assertFalse(rc.decode_coding_fields(cfg_nobt)["bt_enabled"])
        self.assertFalse(rc.decode_coding_fields(cfg_nobt)["rvc_enabled"])

        # Persistent save/load + rootfs seeding (PSS CRC + stdplus.ini + bt_coding.config)
        with tempfile.TemporaryDirectory() as tmp:
            tp = pathlib.Path(tmp)
            coding_json = tp / "coding.json"
            rc.save_coding_config(coding_json, cfg)
            loaded = rc.load_coding_config(coding_json)
            self.assertEqual(loaded, cfg)

            stage = tp / "stage"
            (stage / "pss1").mkdir(parents=True)
            (stage / "pss1/stdplus.ini").write_text("[Version]\nBT_VER = C210\n[CODING]\nBT=0\nCODINGS=0\n")
            rc.seed_rootfs_coding(stage, cfg)

            pss1_bytes = (stage / "pss1/DIAG_PSS_CODING.pss").read_bytes()
            pss2_bytes = (stage / "pss2/DIAG_PSS_CODING.pss").read_bytes()
            self.assertEqual(pss1_bytes, pss2_bytes)
            self.assertEqual(len(pss1_bytes), 33)
            crc_hdr, dlen, tag, cnt, rsv = struct.unpack(">HHBHB", pss1_bytes[:8])
            self.assertEqual(dlen, 25)
            self.assertEqual(crc_hdr, rc.crc16_ccitt_false(pss1_bytes[8:]))
            self.assertEqual(pss1_bytes[8:15], bytes.fromhex(cfg["coding_hex"]))

            ini_txt = (stage / "pss1/stdplus.ini").read_text()
            self.assertIn("CODINGS=3", ini_txt)
            self.assertIn("BT=1", ini_txt)
            self.assertIn("RVC=1", ini_txt)
            self.assertIn("PLAT=PQ", ini_txt)
            self.assertIn("BT_VER=C210", ini_txt)
            self.assertEqual((stage / "pss2/app_data/bt_coding.config").read_bytes(), b"\x2f")

    def test_native_gui_replaces_web_panel_and_wires_launcher(self):
        self.assertFalse((ROOT / "tools/rcd330_panel.py").exists())
        self.assertTrue((ROOT / "tools/build_gui.sh").is_file())
        self.assertTrue((ROOT / "tools/rcd330_gui/CMakeLists.txt").is_file())

        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        self.assertNotIn("rcd330_panel.py", run_sh)
        self.assertIn("build_gui.sh", run_sh)
        self.assertIn("rcd330_gui", run_sh)
        self.assertIn("-display", run_sh)
        self.assertIn("-vnc", run_sh)
        self.assertIn("--coding-file", run_sh)

    def test_build_and_run_scripts_integrate_persistent_coding(self):
        build_sh = (ROOT / "tools/build_rootfs.sh").read_text()
        run_sh = (ROOT / "tools/run_rcd330.sh").read_text()
        self.assertIn("rcd330_coding.py", build_sh)
        self.assertIn("--coding-file", build_sh)
        self.assertIn("killall", build_sh)
        self.assertIn("CODING_FILE", run_sh)
        self.assertIn("--coding-file", run_sh)

    def test_live_verification_regressions(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        ipu_c = (ROOT / "qemu/hw/display/imx6_ipu.c").read_text()
        hal_c = (ROOT / "tools/arm_hal_shim/hal_gpio_shim.c").read_text()

        # 1. QEMU 11 GraphicHwOps.gfx_update must return true for synchronous updates (no QMP screendump hang)
        gfx_fn = ipu_c[ipu_c.index("static bool imx6_ipu_gfx_update"):ipu_c.index("static const GraphicHwOps")]
        self.assertNotIn("return false;", gfx_fn)
        self.assertIn("return true;", gfx_fn)

        # 2. Safe Code / IMMO unlocked (SC_STATE=0) sent at startup and on VBAPProxy OnSetBAPHMIStartUp (0x57)
        self.assertIn("vip_send_bap_startup", vip_c)
        self.assertIn("sc_unlocked[2] = { 0x01, 0x00 }", vip_c)
        self.assertIn("pl[0] == 0x57", vip_c)

        # 3. GetMutePinState (0x19 -> 0x90) is hardware eCall pin and must always reply 0x00 (inactive)
        self.assertIn("mute_pin_inactive[2] = { 0x90, 0x00 }", vip_c)
        self.assertNotIn("{ 0x90, s->muted }", vip_c)

        # 4. PQ vs MQB faceplate key mapping (VKeyDriver 0xaa71c) and RVC mode=4 direct popup
        self.assertIn("vip_button_code(const Rcd330VipState *s", vip_c)
        self.assertIn("vip_send_popup_ack(s, 4, 1)", vip_c)

        # 5. Early-boot opt/camera gpio44_value defaults to "1\n" (no reverse gear at boot, no read EOF spam)
        self.assertIn("gpio44_value", hal_c)

    # --- Native Panel Support: POWER 10s Reboot, Disconnect Safety, EEPROM & Blank-on-Reset ---
    def test_vip_power_long_press_reboot_and_eeprom_persistence(self):
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        ipu_c = (ROOT / "qemu/hw/display/imx6_ipu.c").read_text()

        # POWER 10s hold timer + cold reboot via qemu_system_reset_request
        self.assertIn('#include "system/runstate.h"', vip_c)
        self.assertIn("pwr_hold_timer", vip_c)
        self.assertIn("vip_pwr_hold_timer_cb", vip_c)
        self.assertIn("QEMU_CLOCK_REALTIME", vip_c)
        self.assertIn("qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET)", vip_c)
        self.assertIn("REBOOT POWER_10S", vip_c)
        self.assertIn("pwr_btn_down", vip_c)
        self.assertIn("pwr_reboot_fired", vip_c)

        # Stuck-key & power-timer cleanup on TCP disconnect
        self.assertIn("held_keys_mask", vip_c)
        self.assertIn("vip_chr_event", vip_c)
        self.assertIn("CHR_EVENT_CLOSED", vip_c)

        # Non-volatile coding/VRM EEPROM initialized in vip_realize, preserved across vip_reset_hold
        realize_fn = vip_c.split("static void vip_realize(")[1].split("\n}")[0]
        reset_fn = vip_c.split("static void vip_reset_hold(")[1].split("\n}")[0]
        self.assertIn("default_pq_coding", realize_fn)
        self.assertIn("s->platform = 1;", realize_fn)
        self.assertNotIn("default_pq_coding", reset_fn)
        self.assertNotIn("s->coding_len =", reset_fn)
        self.assertIn("timer_del(s->pwr_hold_timer);", reset_fn)
        self.assertIn("s->held_keys_mask = 0;", reset_fn)

        # IPU blanks display surface on reset so 10s POWER reboot shows immediate black screen
        ipu_reset = ipu_c.split("static void imx6_ipu_reset(")[1].split("\n}")[0]
        self.assertIn("qemu_console_surface", ipu_reset)
        self.assertIn("qemu_console_update", ipu_reset)

    def test_coding_cli_json_load_apply_and_reset(self):
        import subprocess

        script = str(ROOT / "tools/rcd330_coding.py")
        with tempfile.TemporaryDirectory() as tmp:
            coding_file = pathlib.Path(tmp) / "test_coding.json"

            # 1. load creates default payload with lookup tables & CODING_SYNC
            res_load = subprocess.run(
                [sys.executable, script, "load", "--coding-file", str(coding_file), "--fw-id", "5624_VW"],
                capture_output=True, text=True, check=True,
            )
            data_load = json.loads(res_load.stdout)
            self.assertEqual(data_load["config"]["platform"], "PQ")
            self.assertEqual(data_load["config"]["brand"], "VW")
            self.assertTrue(data_load["decoded"]["rvc_enabled"])
            self.assertIn("19", data_load["pq_vehicles"])
            self.assertIn("56", data_load["mqb_vehicles"])
            self.assertIn("24", data_load["mqb_bootlogos"])
            self.assertTrue(any(c.startswith("CODING_SYNC ") for c in data_load["vip_cmds"]))

            # 2. apply --live updates fields, writes persistent JSON, returns CODING (live)
            upd = json.dumps({
                "rvc_enabled": False,
                "ops_mode": "360",
                "speakers": 2,
                "eng_mode": True,
            })
            res_apply = subprocess.run(
                [sys.executable, script, "apply", "--coding-file", str(coding_file), "--fw-id", "5624_VW", "--live"],
                input=upd, capture_output=True, text=True, check=True,
            )
            data_apply = json.loads(res_apply.stdout)
            self.assertFalse(data_apply["decoded"]["rvc_enabled"])
            self.assertEqual(data_apply["config"]["ops_mode"], "360")
            self.assertEqual(data_apply["config"]["speakers"], 2)
            self.assertTrue(data_apply["config"]["eng_mode"])
            self.assertTrue(coding_file.is_file())
            self.assertTrue(any(c.startswith("CODING ") for c in data_apply["vip_cmds"]))
            self.assertIn("OPS_CFG 4 4 4 4", data_apply["vip_cmds"])
            self.assertIn("ENGMODE 1", data_apply["vip_cmds"])

            # 3. apply invalid raw hex returns validation_warnings without crashing
            bad_upd = json.dumps({"platform": "PQ", "coding_hex": "0E000441040113"})
            res_bad = subprocess.run(
                [sys.executable, script, "apply", "--coding-file", str(coding_file), "--fw-id", "5624_VW", "--live"],
                input=bad_upd, capture_output=True, text=True, check=True,
            )
            data_bad = json.loads(res_bad.stdout)
            self.assertGreater(len(data_bad["decoded"]["validation_warnings"]), 0)

            # 4. reset restores defaults & writes file
            res_reset = subprocess.run(
                [sys.executable, script, "reset", "--coding-file", str(coding_file), "--fw-id", "5624_VW", "--live"],
                capture_output=True, text=True, check=True,
            )
            data_reset = json.loads(res_reset.stdout)
            self.assertTrue(data_reset["decoded"]["rvc_enabled"])
            self.assertEqual(data_reset["config"]["ops_mode"], "8K")
            self.assertFalse(data_reset["config"]["eng_mode"])
            self.assertEqual(data_reset["decoded"]["validation_warnings"], [])

    def test_imx6_gpt_prescaler24m_and_en24m_realtime_clock(self):
        """i.MX6DL Linux 3.0.35 writes GPT_PR=0x7000 (PRESCALER24M=7) and GPT_CR.EN_24M=1
        then reads GPT_PR back to compute the GPT rate; QEMU must preserve both so guest
        time runs at 1x realtime instead of 8x slow (which breaks HMI long-press timers)."""
        gpt_h = (ROOT / "qemu/include/hw/timer/imx_gpt.h").read_text()
        gpt_c = (ROOT / "qemu/hw/timer/imx_gpt.c").read_text()

        self.assertIn("GPT_CR_EN_24M", gpt_h)
        self.assertIn("GPT_PR_PRESCALER24M_SHIFT", gpt_h)
        self.assertIn("GPT_CR_EN_24M", gpt_c)
        self.assertIn("GPT_PR_PRESCALER24M_SHIFT", gpt_c)
        self.assertIn("s->pr = value & 0xffff;", gpt_c)
        self.assertIn("s->cr = value & ~0x7814;", gpt_c)
        # Free-run mode anchors CNT to QEMU_CLOCK_VIRTUAL so one-shot OCR1 latency never loses guest time
        self.assertIn("frr_base_ns", gpt_h)
        self.assertIn("QEMU_CLOCK_VIRTUAL", gpt_c)
        # Rollover and OCR1 write safety: single latched count, no recursive re-read on GPT_TIMER_MAX rollover
        self.assertIn("imx_gpt_compute_next_timeout_with_count", gpt_c)
        self.assertIn("limit = (long long)timeout - (long long)count;", gpt_c)
        self.assertIn("imx_gpt_anchor_frr(s, 0);", gpt_c)
        self.assertIn("imx_gpt_compute_next_timeout_with_count(s, false, cur);", gpt_c)
        # VIP ACKs VBAPProxy startup (0x57 -> 0x69) and exit (0x72 -> 0x6b) to stop 200ms BapAckTimer storm
        vip_c = (ROOT / "qemu/hw/misc/rcd330_vip.c").read_text()
        self.assertIn("bap_start_ack", vip_c)
        self.assertIn("0x69", vip_c)
        self.assertIn("bap_exit_ack", vip_c)
        self.assertIn("0x6b", vip_c)
        # Volatile telemetry cleared on vip_reset_hold
        reset_fn = vip_c.split("static void vip_reset_hold(")[1].split("\n}")[0]
        self.assertIn("s->speed_kmh = 0;", reset_fn)
        self.assertIn("s->rpm = 0;", reset_fn)
        self.assertIn("s->doors_mask = 0;", reset_fn)
        self.assertIn("s->lights_mask = 0;", reset_fn)


if __name__ == "__main__":
    unittest.main()
