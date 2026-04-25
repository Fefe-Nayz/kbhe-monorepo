from __future__ import annotations

from copy import deepcopy

from .protocol import (
    ADVANCED_TICK_RATE_DEFAULT,
    ADVANCED_TICK_RATE_MAX,
    ADVANCED_TICK_RATE_MIN,
    GAMEPAD_API_MODES,
    GAMEPAD_KEYBOARD_ROUTING,
    KEY_COUNT,
    LEDEffect,
    LAYER_COUNT,
    LED_AUDIO_SPECTRUM_BAND_COUNT,
    LED_EFFECT_PARAM_COLOR_B,
    LED_EFFECT_PARAM_COLOR_G,
    LED_EFFECT_PARAM_COLOR_R,
    LED_EFFECT_PARAM_COUNT,
    LED_EFFECT_PARAM_SPEED,
    ParamType,
)


def _default_key_settings(index: int) -> dict:
    return {
        "hid_keycode": 0x14 + (index % 26),
        "actuation_point_mm": 1.2 + ((index % 5) * 0.2),
        "release_point_mm": 1.0 + ((index % 5) * 0.2),
        "rapid_trigger_enabled": bool(index % 3 == 0),
        "rapid_trigger_press": 0.2 + ((index % 3) * 0.05),
        "rapid_trigger_release": 0.2 + ((index % 4) * 0.04),
        "continuous_rapid_trigger": bool(index % 5 == 0),
        "socd_pair": (index + 1) % KEY_COUNT if index % 11 == 0 else None,
        "socd_resolution": 1 if index % 22 == 0 else 0,
        "disable_kb_on_gamepad": bool(index % 7 == 0),
        "behavior_mode": 1 if index % 9 == 0 else 0,
        "hold_threshold_ms": 220,
        "secondary_hid_keycode": 0x2C if index % 9 == 0 else 0,
        "dynamic_zone_count": 2,
        "dynamic_zones": [
            {"end_mm_tenths": 12, "end_mm": 1.2, "hid_keycode": 0x1E},
            {"end_mm_tenths": 24, "end_mm": 2.4, "hid_keycode": 0x1F},
            {"end_mm_tenths": 32, "end_mm": 3.2, "hid_keycode": 0},
            {"end_mm_tenths": 40, "end_mm": 4.0, "hid_keycode": 0},
        ],
    }


class DemoDevice:
    def __init__(self) -> None:
        self.connected = True
        self._firmware_version = "2.4-demo"
        self._serial_number = "KBHE-F7-DEMO000000000000"
        self._keyboard_name = "KBHE Demo Keyboard"
        self._options = {
            "keyboard_enabled": True,
            "gamepad_enabled": True,
            "raw_hid_echo": False,
            "led_thermal_protection_enabled": True,
        }
        self._nkro_enabled = True
        self._advanced_tick_rate = int(ADVANCED_TICK_RATE_DEFAULT)
        self._keys = [_default_key_settings(i) for i in range(KEY_COUNT)]
        self._layer_keycodes = [
            [int(self._keys[i]["hid_keycode"]) for i in range(KEY_COUNT)]
        ] + [[0x0001 for _ in range(KEY_COUNT)] for _ in range(LAYER_COUNT - 1)]
        self._curves = [
            {
                "key_index": i,
                "curve_enabled": i % 2 == 0,
                "p1_x": 64,
                "p1_y": 32,
                "p2_x": 192,
                "p2_y": 220,
            }
            for i in range(KEY_COUNT)
        ]
        self._gamepad_maps = [
            {
                "key_index": i,
                "axis": (i % 6) + 1,
                "direction": i % 2,
                "button": i % 17,
            }
            for i in range(KEY_COUNT)
        ]
        self._gamepad_with_keyboard = True
        self._gamepad_settings = {
            "radial_deadzone": 18,
            "keyboard_routing": 1,
            "square_mode": True,
            "reactive_stick": False,
            "api_mode": int(GAMEPAD_API_MODES["HID (DirectInput)"]),
            "curve_points": [
                {"x_01mm": 0, "x_mm": 0.0, "y": 0},
                {"x_01mm": 90, "x_mm": 0.9, "y": 32},
                {"x_01mm": 250, "x_mm": 2.5, "y": 196},
                {"x_01mm": 400, "x_mm": 4.0, "y": 255},
            ],
        }
        self._filter_enabled = True
        self._filter_params = {
            "noise_band": 30,
            "alpha_min_denom": 32,
            "alpha_max_denom": 4,
        }
        self._calibration = {
            "lut_zero_value": 2048,
            "key_zero_values": [2000 + (i % 30) for i in range(KEY_COUNT)],
            "key_max_values": [3600 - (i % 40) for i in range(KEY_COUNT)],
        }
        self._led_enabled = True
        self._led_brightness = 96
        self._led_effect = 1
        self._led_effect_color = [64, 180, 255]
        self._led_effect_params = {}
        self._audio_spectrum_bands = [0] * LED_AUDIO_SPECTRUM_BAND_COUNT
        self._audio_spectrum_impact = 0
        self._led_fps_limit = 60
        self._led_pixels = []
        for i in range(KEY_COUNT):
            self._led_pixels.extend([(i * 17) % 256, (i * 37) % 256, (i * 53) % 256])
        self._host_volume_overlay = None
        self._rotary_settings = {
            "rotation_action": 0,
            "button_action": 0,
            "sensitivity": 4,
            "step_size": 4,
            "invert_direction": False,
            "rgb_behavior": 0,
            "rgb_effect_mode": 4,
            "progress_style": 0,
            "progress_effect_mode": 1,
            "progress_color": [40, 210, 64],
        }
        self._guided_status = {
            "active": False,
            "phase": 0,
            "current_key": 0,
            "progress_percent": 0,
            "sample_count": 0,
            "phase_elapsed_ms": 0,
        }

    def connect(self, logger=print):
        self.connected = True
        if logger is not None:
            logger("Demo device connected.")
        return True

    def reconnect(self, logger=None):
        return self.connect(logger=logger)

    def disconnect(self):
        self.connected = False

    def get_firmware_version(self):
        return self._firmware_version

    def get_device_info(self):
        return {
            "firmware_version": self._firmware_version,
            "firmware_version_raw": 0x0204,
            "serial_number": self._serial_number,
            "keyboard_name": self._keyboard_name,
        }

    def get_keyboard_name(self):
        return self._keyboard_name

    def set_keyboard_name(self, name):
        cleaned = str(name or "").strip()
        self._keyboard_name = cleaned[:32] if cleaned else "KBHE Demo Keyboard"
        return True

    def get_options(self):
        return dict(self._options)

    def get_mcu_metrics(self):
        return {
            "temperature_c": 41,
            "temperature_valid": True,
            "vref_mv": 3290,
            "core_clock_hz": 216_000_000,
            "scan_cycle_us": 125,
            "scan_rate_hz": 8000,
            "work_us": 78,
            "load_percent": 62.4,
            "load_permille": 624,
        }

    def set_keyboard_enabled(self, enabled):
        self._options["keyboard_enabled"] = bool(enabled)
        return True

    def set_gamepad_enabled(self, enabled):
        self._options["gamepad_enabled"] = bool(enabled)
        return True

    def get_nkro_enabled(self):
        return bool(self._nkro_enabled)

    def set_nkro_enabled(self, enabled):
        self._nkro_enabled = bool(enabled)
        return True

    def get_advanced_tick_rate(self):
        return int(self._advanced_tick_rate)

    def set_advanced_tick_rate(self, tick_rate):
        self._advanced_tick_rate = max(
            int(ADVANCED_TICK_RATE_MIN),
            min(int(ADVANCED_TICK_RATE_MAX), int(tick_rate)),
        )
        return True

    def get_key_settings(self, index):
        return dict(self._keys[int(index)])

    def get_all_key_settings(self):
        return [dict(entry) for entry in self._keys]

    def set_key_settings_extended(self, index, settings):
        self._keys[int(index)] = {**self._keys[int(index)], **deepcopy(settings)}
        self._layer_keycodes[0][int(index)] = int(self._keys[int(index)]["hid_keycode"])
        return True

    def get_layer_keycode(self, layer_index, key_index):
        layer_index = max(0, min(int(LAYER_COUNT) - 1, int(layer_index)))
        key_index = max(0, min(int(KEY_COUNT) - 1, int(key_index)))
        if layer_index == 0:
            hid_keycode = int(self._keys[key_index]["hid_keycode"])
        else:
            hid_keycode = int(self._layer_keycodes[layer_index][key_index])
        return {
            "layer_index": layer_index,
            "key_index": key_index,
            "hid_keycode": hid_keycode,
        }

    def set_layer_keycode(self, layer_index, key_index, hid_keycode):
        layer_index = max(0, min(int(LAYER_COUNT) - 1, int(layer_index)))
        key_index = max(0, min(int(KEY_COUNT) - 1, int(key_index)))
        hid_keycode = int(hid_keycode) & 0xFFFF
        if layer_index == 0:
            self._keys[key_index]["hid_keycode"] = hid_keycode
            self._layer_keycodes[0][key_index] = hid_keycode
        else:
            self._layer_keycodes[layer_index][key_index] = hid_keycode
        return True

    def get_calibration(self):
        return deepcopy(self._calibration)

    def set_calibration(self, lut_zero, key_zeros, key_maxs=None):
        self._calibration["lut_zero_value"] = int(lut_zero)
        self._calibration["key_zero_values"] = list(key_zeros)
        if key_maxs is not None:
            self._calibration["key_max_values"] = list(key_maxs)
        return True

    def auto_calibrate(self, key_index=0xFF):
        return self.get_calibration()

    def guided_calibration_start(self):
        self._guided_status.update(
            {
                "active": True,
                "phase": 1,
                "current_key": 0,
                "progress_percent": 0,
                "sample_count": 128,
                "phase_elapsed_ms": 0,
            }
        )
        return dict(self._guided_status)

    def guided_calibration_status(self):
        if self._guided_status["active"]:
            progress = min(100, int(self._guided_status["progress_percent"]) + 25)
            self._guided_status["progress_percent"] = progress
            self._guided_status["phase_elapsed_ms"] = int(
                self._guided_status["phase_elapsed_ms"]
            ) + 600
            if progress >= 100:
                self._guided_status["active"] = False
                self._guided_status["phase"] = 2
        return dict(self._guided_status)

    def guided_calibration_abort(self):
        self._guided_status["active"] = False
        return dict(self._guided_status)

    def get_key_curve(self, key_index):
        return dict(self._curves[int(key_index)])

    def set_key_curve(self, key_index, curve_enabled, p1_x, p1_y, p2_x, p2_y):
        self._curves[int(key_index)] = {
            "key_index": int(key_index),
            "curve_enabled": bool(curve_enabled),
            "p1_x": int(p1_x),
            "p1_y": int(p1_y),
            "p2_x": int(p2_x),
            "p2_y": int(p2_y),
        }
        return True

    def get_key_gamepad_map(self, key_index):
        return dict(self._gamepad_maps[int(key_index)])

    def set_key_gamepad_map(self, key_index, axis, direction, button):
        self._gamepad_maps[int(key_index)] = {
            "key_index": int(key_index),
            "axis": int(axis),
            "direction": int(direction),
            "button": int(button),
        }
        return True

    def get_gamepad_settings(self):
        settings = deepcopy(self._gamepad_settings)
        curve_points = list(settings.get("curve_points") or [])
        if curve_points:
            start_01mm = int(curve_points[0].get("x_01mm", 0))
            settings["deadzone"] = int(round((start_01mm * 255.0) / 400.0))
            settings["radial_deadzone"] = settings["deadzone"]
        else:
            settings.setdefault("deadzone", int(settings.get("radial_deadzone", 0)))
        settings.setdefault("snappy_mode", bool(settings.get("reactive_stick", False)))
        settings.setdefault("curve_type", 0)
        settings.setdefault("api_mode", int(GAMEPAD_API_MODES["HID (DirectInput)"]))
        return settings

    def set_gamepad_settings(self, settings):
        settings = deepcopy(settings or {})
        curve_points = list(settings.get("curve_points") or self._gamepad_settings.get("curve_points") or [])
        if curve_points:
            settings["radial_deadzone"] = int(
                round(max(0, min(400, int(curve_points[0].get("x_01mm", 0)))) * 255.0 / 400.0)
            )
        else:
            settings["radial_deadzone"] = max(0, int(settings.get("radial_deadzone", settings.get("deadzone", 0))))
        settings["deadzone"] = int(settings.get("radial_deadzone", settings.get("deadzone", 0)))
        settings["snappy_mode"] = bool(settings.get("reactive_stick", settings.get("snappy_mode", False)))
        settings.setdefault("curve_type", 0)
        settings["api_mode"] = int(settings.get("api_mode", GAMEPAD_API_MODES["HID (DirectInput)"]))
        self._gamepad_settings = settings
        self._gamepad_with_keyboard = bool(self._gamepad_settings.get("keyboard_routing", 1) != 0)
        return True

    def get_filter_enabled(self):
        return bool(self._filter_enabled)

    def set_filter_enabled(self, enabled):
        self._filter_enabled = bool(enabled)
        return True

    def get_filter_params(self):
        return dict(self._filter_params)

    def set_filter_params(self, noise_band, alpha_min_denom, alpha_max_denom):
        self._filter_params = {
            "noise_band": int(noise_band),
            "alpha_min_denom": int(alpha_min_denom),
            "alpha_max_denom": int(alpha_max_denom),
        }
        return True

    def get_led_effect(self):
        return int(self._led_effect)

    def set_led_effect(self, mode):
        self._led_effect = int(mode)
        params = self.get_led_effect_params(self._led_effect)
        self._led_effect_color = [
            int(params[LED_EFFECT_PARAM_COLOR_R]),
            int(params[LED_EFFECT_PARAM_COLOR_G]),
            int(params[LED_EFFECT_PARAM_COLOR_B]),
        ]
        return True

    def get_led_effect_speed(self):
        mode = int(self._led_effect)
        params = self.get_led_effect_params(mode)
        speed = int(params[LED_EFFECT_PARAM_SPEED])
        return speed if speed > 0 else 1

    def set_led_effect_speed(self, speed):
        mode = int(self._led_effect)
        params = self.get_led_effect_params(mode)
        params[LED_EFFECT_PARAM_SPEED] = max(1, min(255, int(speed)))
        self._led_effect_params[mode] = list(params)
        return True

    def set_led_effect_color(self, r, g, b):
        mode = int(self._led_effect)
        params = self.get_led_effect_params(mode)
        params[LED_EFFECT_PARAM_COLOR_R] = max(0, min(255, int(r)))
        params[LED_EFFECT_PARAM_COLOR_G] = max(0, min(255, int(g)))
        params[LED_EFFECT_PARAM_COLOR_B] = max(0, min(255, int(b)))
        self._led_effect_params[mode] = list(params)
        self._led_effect_color = [
            params[LED_EFFECT_PARAM_COLOR_R],
            params[LED_EFFECT_PARAM_COLOR_G],
            params[LED_EFFECT_PARAM_COLOR_B],
        ]
        return True

    def get_led_effect_color(self):
        mode = int(self._led_effect)
        params = self.get_led_effect_params(mode)
        if len(params) > LED_EFFECT_PARAM_COLOR_B:
            return [
                int(params[LED_EFFECT_PARAM_COLOR_R]),
                int(params[LED_EFFECT_PARAM_COLOR_G]),
                int(params[LED_EFFECT_PARAM_COLOR_B]),
            ]
        return list(self._led_effect_color)

    def get_led_effect_params(self, effect_mode):
        mode = int(effect_mode)
        defaults = [0] * LED_EFFECT_PARAM_COUNT
        defaults[0:6] = [160, 96, 160, 255, 0, 0]
        defaults[LED_EFFECT_PARAM_SPEED] = 75
        defaults[LED_EFFECT_PARAM_COLOR_R] = int(self._led_effect_color[0])
        defaults[LED_EFFECT_PARAM_COLOR_G] = int(self._led_effect_color[1])
        defaults[LED_EFFECT_PARAM_COLOR_B] = int(self._led_effect_color[2])

        values = list(self._led_effect_params.get(mode, defaults))
        while len(values) < LED_EFFECT_PARAM_COUNT:
            values.append(0)
        return values[:LED_EFFECT_PARAM_COUNT]

    def get_led_effect_schema(self, effect_mode):
        mode = int(effect_mode)
        params = self.get_led_effect_params(mode)
        descriptors = []

        def add_desc(param_id, param_type, minimum, maximum, default_value, step):
            descriptors.append(
                {
                    "id": int(param_id),
                    "type": int(param_type),
                    "min": int(minimum),
                    "max": int(maximum),
                    "default": int(default_value),
                    "step": int(step),
                }
            )

        if mode == int(LEDEffect.AUDIO_SPECTRUM):
            add_desc(0, ParamType.HUE, 0, 255, 176, 1)
            add_desc(1, ParamType.U8, 0, 255, 32, 4)
            add_desc(2, ParamType.U8, 0, 255, 208, 8)
            add_desc(3, ParamType.BOOL, 0, 1, 1, 1)
            add_desc(4, ParamType.U8, 0, 255, 172, 8)
            add_desc(5, ParamType.U8, 0, 3, 0, 1)
            add_desc(6, ParamType.U8, 0, 255, 236, 4)

        if mode == int(LEDEffect.KEY_STATE_DEMO):
            add_desc(0, ParamType.BOOL, 0, 1, 0, 1)
            add_desc(1, ParamType.U8, 0, 255, 255, 8)
            add_desc(2, ParamType.U8, 0, 255, 96, 8)

        if mode not in (int(LEDEffect.NONE), int(LEDEffect.THIRD_PARTY)):
            add_desc(
                LED_EFFECT_PARAM_SPEED,
                ParamType.U8,
                1,
                255,
                max(1, int(params[LED_EFFECT_PARAM_SPEED])),
                1,
            )
            add_desc(LED_EFFECT_PARAM_COLOR_R, ParamType.COLOR, 0, 255, int(params[LED_EFFECT_PARAM_COLOR_R]), 0)
            add_desc(LED_EFFECT_PARAM_COLOR_G, ParamType.COLOR, 0, 255, int(params[LED_EFFECT_PARAM_COLOR_G]), 0)
            add_desc(LED_EFFECT_PARAM_COLOR_B, ParamType.COLOR, 0, 255, int(params[LED_EFFECT_PARAM_COLOR_B]), 0)

        if mode == int(LEDEffect.KEY_STATE_DEMO):
            add_desc(LED_EFFECT_PARAM_COLOR_R + 3, ParamType.COLOR, 0, 255, int(params[LED_EFFECT_PARAM_COLOR_R + 3]), 0)
            add_desc(LED_EFFECT_PARAM_COLOR_G + 3, ParamType.COLOR, 0, 255, int(params[LED_EFFECT_PARAM_COLOR_G + 3]), 0)
            add_desc(LED_EFFECT_PARAM_COLOR_B + 3, ParamType.COLOR, 0, 255, int(params[LED_EFFECT_PARAM_COLOR_B + 3]), 0)

        descriptors.sort(key=lambda entry: int(entry["id"]))
        return {
            "effect_id": mode,
            "total_chunks": 1,
            "total_active": len(descriptors),
            "descriptors": descriptors,
        }

    def set_led_effect_params(self, effect_mode, params):
        mode = int(effect_mode)
        values = [int(v) & 0xFF for v in list(params)[:LED_EFFECT_PARAM_COUNT]]
        while len(values) < LED_EFFECT_PARAM_COUNT:
            values.append(0)
        values[LED_EFFECT_PARAM_SPEED] = max(1, values[LED_EFFECT_PARAM_SPEED])
        self._led_effect_params[mode] = list(values)
        if mode == int(self._led_effect):
            self._led_effect_color = [
                values[LED_EFFECT_PARAM_COLOR_R],
                values[LED_EFFECT_PARAM_COLOR_G],
                values[LED_EFFECT_PARAM_COLOR_B],
            ]
        return True

    def led_set_audio_spectrum(self, bands, impact_level=0):
        values = [max(0, min(255, int(v))) for v in list(bands or [])[:LED_AUDIO_SPECTRUM_BAND_COUNT]]
        if not values:
            return False
        if len(values) < LED_AUDIO_SPECTRUM_BAND_COUNT:
            values.extend([0] * (LED_AUDIO_SPECTRUM_BAND_COUNT - len(values)))
        self._audio_spectrum_bands = values[:LED_AUDIO_SPECTRUM_BAND_COUNT]
        self._audio_spectrum_impact = max(0, min(255, int(impact_level)))
        return True

    def led_clear_audio_spectrum(self):
        self._audio_spectrum_bands = [0] * LED_AUDIO_SPECTRUM_BAND_COUNT
        self._audio_spectrum_impact = 0
        return True

    def get_led_fps_limit(self):
        return int(self._led_fps_limit)

    def set_led_fps_limit(self, fps):
        self._led_fps_limit = int(fps)
        return True

    def led_get_brightness(self):
        return int(self._led_brightness)

    def led_set_brightness(self, value):
        self._led_brightness = int(value)
        return True

    def led_get_enabled(self):
        return bool(self._led_enabled)

    def led_set_enabled(self, enabled):
        self._led_enabled = bool(enabled)
        return True

    def led_get_pixel(self, index):
        base = int(index) * 3
        return list(self._led_pixels[base : base + 3])

    def led_set_pixel(self, index, r, g, b):
        base = int(index) * 3
        self._led_pixels[base : base + 3] = [int(r), int(g), int(b)]
        return True

    def led_get_row(self, row):
        start = int(row) * 8 * 3
        return list(self._led_pixels[start : start + 24])

    def led_set_row(self, row, pixels):
        start = int(row) * 8 * 3
        row_pixels = list(pixels[:24])
        while len(row_pixels) < 24:
            row_pixels.append(0)
        self._led_pixels[start : start + 24] = row_pixels
        return True

    def led_download_all(self):
        return list(self._led_pixels)

    def led_upload_all(self, pixels):
        self._led_pixels = list(pixels[: KEY_COUNT * 3])
        while len(self._led_pixels) < KEY_COUNT * 3:
            self._led_pixels.append(0)
        return True

    def led_clear(self):
        self._led_pixels = [0] * (KEY_COUNT * 3)
        return True

    def led_fill(self, r, g, b):
        self._led_pixels = []
        for _ in range(KEY_COUNT):
            self._led_pixels.extend([int(r), int(g), int(b)])
        return True

    def led_test_rainbow(self):
        for i in range(KEY_COUNT):
            self.led_set_pixel(i, (i * 11) % 256, (i * 23) % 256, (i * 41) % 256)
        return True

    def led_set_volume_overlay(self, level):
        self._host_volume_overlay = int(level)
        return True

    def led_clear_volume_overlay(self):
        self._host_volume_overlay = None
        return True

    def save_settings(self):
        return True

    def factory_reset(self):
        self.__init__()
        return True

    def get_adc_values(self):
        return {
            "adc": [2050, 2100, 2200, 2300, 2400, 2500],
            "adc_raw": [2050, 2100, 2200, 2300, 2400, 2500],
            "adc_filtered": [2040, 2090, 2190, 2290, 2390, 2490],
            "scan_time_us": 760,
            "scan_rate_hz": 1315,
            "task_times_us": {
                "analog": 220,
                "trigger": 110,
                "socd": 30,
                "keyboard": 80,
                "keyboard_nkro": 95,
                "gamepad": 45,
                "led": 62,
                "total": 760,
            },
            "analog_monitor_us": {
                "raw": 80,
                "filter": 45,
                "calibration": 25,
                "lut": 20,
                "store": 10,
                "key_min": 2,
                "key_max": 12,
                "key_avg": 6,
                "nonzero_keys": 18,
                "key_max_index": 37,
            },
        }

    def get_all_raw_adc_values(self, key_count=KEY_COUNT):
        return [1900 + ((i * 13) % 500) for i in range(int(key_count))]

    def get_all_filtered_adc_values(self, key_count=KEY_COUNT):
        return [1840 + ((i * 11) % 480) for i in range(int(key_count))]

    def get_all_calibrated_adc_values(self, key_count=KEY_COUNT):
        return [max(0, (i * 7) % 4000) for i in range(int(key_count))]

    def get_key_states(self):
        states = [1 if i % 9 == 0 else 0 for i in range(KEY_COUNT)]
        norms = [(i * 3) % 255 for i in range(KEY_COUNT)]
        d01 = [int(v * 400 / 255) for v in norms]
        return {
            "states": states,
            "distances": norms,
            "distances_01mm": d01,
            "distances_mm": [v / 100.0 for v in d01],
        }

    def get_lock_states(self):
        return {"caps_lock": True, "num_lock": False, "scroll_lock": False}

    def adc_capture_start(self, *args, **kwargs):
        return None

    def adc_capture_status(self):
        return None

    def adc_capture_read(self, *args, **kwargs):
        return None

    def get_rotary_encoder_settings(self):
        return deepcopy(self._rotary_settings)

    def set_rotary_encoder_settings(self, settings):
        self._rotary_settings.update(deepcopy(settings))
        return True
