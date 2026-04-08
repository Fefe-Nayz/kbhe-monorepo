from __future__ import annotations

from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication

from kbhe_tool.protocol import KEY_COUNT
from kbhe_tool.qtgui.app import KBHEQtMainWindow


class DummyDevice:
    def get_firmware_version(self): return "2.4"
    def get_options(self): return {"keyboard_enabled": False, "gamepad_enabled": True, "raw_hid_echo": False}
    def get_nkro_enabled(self): return True
    def get_key_settings(self, index):
        return {
            "hid_keycode": 0x14 + (index % 26),
            "actuation_point_mm": 1.2 + ((index % 5) * 0.2),
            "release_point_mm": 1.0 + ((index % 5) * 0.2),
            "rapid_trigger_enabled": bool(index % 3 == 0),
            "rapid_trigger_activation": 0.5,
            "rapid_trigger_press": 0.2 + ((index % 3) * 0.05),
            "rapid_trigger_release": 0.2 + ((index % 4) * 0.04),
            "socd_pair": (index + 1) % KEY_COUNT if index % 11 == 0 else None,
            "disable_kb_on_gamepad": bool(index % 7 == 0),
        }
    def get_all_key_settings(self): return [self.get_key_settings(i) for i in range(KEY_COUNT)]
    def set_key_settings_extended(self, index, settings): return True
    def get_calibration(self): return {"lut_zero_value": 2048, "key_zero_values": [2000 + (i % 30) for i in range(KEY_COUNT)]}
    def set_calibration(self, lut_zero, key_zeros): return True
    def auto_calibrate(self, key_index=0xFF): return self.get_calibration()
    def get_key_curve(self, key_index): return {"key_index": key_index, "curve_enabled": key_index % 2 == 0, "p1_x": 64, "p1_y": 32, "p2_x": 192, "p2_y": 220}
    def set_key_curve(self, *args, **kwargs): return True
    def get_key_gamepad_map(self, key_index): return {"key_index": key_index, "axis": (key_index % 6) + 1, "direction": key_index % 2, "button": (key_index % 17)}
    def set_key_gamepad_map(self, *args, **kwargs): return True
    def get_gamepad_with_keyboard(self): return True
    def set_gamepad_with_keyboard(self, enabled): return True
    def get_gamepad_settings(self): return {"deadzone": 18, "curve_type": 1, "square_mode": True, "snappy_mode": False}
    def set_gamepad_settings(self, *args, **kwargs): return True
    def get_filter_enabled(self): return True
    def set_filter_enabled(self, enabled): return True
    def get_filter_params(self): return {"noise_band": 30, "alpha_min_denom": 32, "alpha_max_denom": 4}
    def set_filter_params(self, *args, **kwargs): return True
    def get_led_effect(self): return 0
    def set_led_effect(self, mode): return True
    def get_led_effect_speed(self): return 75
    def set_led_effect_speed(self, speed): return True
    def set_led_effect_color(self, r, g, b): return True
    def get_led_fps_limit(self): return 60
    def set_led_fps_limit(self, fps): return True
    def get_led_diagnostic(self): return 0
    def set_led_diagnostic(self, mode): return True
    def led_get_brightness(self): return 96
    def led_set_brightness(self, value): return True
    def led_get_enabled(self): return True
    def led_set_enabled(self, enabled): return True
    def led_get_pixel(self, index): return [(index * 17) % 256, (index * 37) % 256, (index * 53) % 256]
    def led_set_pixel(self, index, r, g, b): return True
    def led_download_all(self):
        payload = []
        for i in range(KEY_COUNT):
            payload.extend([(i * 17) % 256, (i * 37) % 256, (i * 53) % 256])
        return payload
    def led_upload_all(self, pixels): return True
    def led_clear(self): return True
    def led_fill(self, r, g, b): return True
    def led_test_rainbow(self): return True
    def save_settings(self): return True
    def factory_reset(self): return True
    def set_keyboard_enabled(self, enabled): return True
    def set_gamepad_enabled(self, enabled): return True
    def set_nkro_enabled(self, enabled): return True
    def get_adc_values(self):
        return {
            "adc": [2050, 2100, 2200, 2300, 2400, 2500],
            "adc_raw": [2050, 2100, 2200, 2300, 2400, 2500],
            "adc_filtered": [2040, 2090, 2190, 2290, 2390, 2490],
            "scan_time_us": 760,
            "scan_rate_hz": 1315,
            "task_times_us": {"analog": 220, "trigger": 110, "socd": 30, "keyboard": 80, "keyboard_nkro": 95, "gamepad": 45, "total": 760},
            "analog_monitor_us": {"raw": 80, "filter": 45, "calibration": 25, "lut": 20, "store": 10, "key_min": 2, "key_max": 12, "key_avg": 6, "nonzero_keys": 18, "key_max_index": 37},
        }
    def get_all_raw_adc_values(self, key_count=KEY_COUNT): return [1900 + ((i * 13) % 500) for i in range(key_count)]
    def get_key_states(self):
        states = [1 if i % 9 == 0 else 0 for i in range(KEY_COUNT)]
        norms = [(i * 3) % 255 for i in range(KEY_COUNT)]
        d01 = [int(v * 400 / 255) for v in norms]
        return {"states": states, "distances": norms, "distances_01mm": d01, "distances_mm": [v / 100.0 for v in d01]}
    def get_lock_states(self): return {"caps_lock": True, "num_lock": False, "scroll_lock": False}
    def adc_capture_start(self, *args, **kwargs): return None
    def adc_capture_status(self): return None
    def adc_capture_read(self, *args, **kwargs): return None


def capture_all(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    app = QApplication.instance() or QApplication([])
    window = KBHEQtMainWindow(DummyDevice())
    window.resize(1500, 980)
    window.show()
    app.processEvents()

    for page_id in list(window.pages.keys()):
        window.show_page(page_id)
        app.processEvents()
        QTimer.singleShot(50, lambda: None)
        app.processEvents()
        window.grab().save(str(output_dir / f"{page_id}.png"))

    window.close()
    app.processEvents()


if __name__ == "__main__":
    capture_all(REPO_ROOT / "build" / "ui-screenshots")
