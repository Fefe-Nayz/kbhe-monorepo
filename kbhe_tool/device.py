import struct
import threading
import time

import hid

from .protocol import (
    ADVANCED_TICK_RATE_DEFAULT,
    ADVANCED_TICK_RATE_MAX,
    ADVANCED_TICK_RATE_MIN,
    CALIBRATION_VALUES_PER_CHUNK,
    Command,
    GAMEPAD_API_MODES,
    GAMEPAD_AXES,
    GAMEPAD_BUTTONS,
    GAMEPAD_DIRECTIONS,
    GAMEPAD_CURVE_MAX_DISTANCE_MM,
    GAMEPAD_CURVE_POINT_COUNT,
    GAMEPAD_KEYBOARD_ROUTING,
    DEVICE_SERIAL_LENGTH,
    KEYBOARD_NAME_LENGTH,
    LAYER_COUNT,
    KEY_BEHAVIORS,
    KEY_COUNT,
    SETTINGS_PROFILE_COUNT,
    SETTINGS_PROFILE_NAME_LENGTH,
    KEY_SETTINGS_PER_CHUNK,
    KEY_STATES_PER_CHUNK,
    LED_BYTES_PER_CHUNK,
    LED_EFFECT_PARAM_COUNT,
    LED_EFFECT_PARAM_COLOR_B,
    LED_EFFECT_PARAM_COLOR_G,
    LED_EFFECT_PARAM_COLOR_R,
    LED_EFFECT_PARAM_SPEED,
    LED_AUDIO_SPECTRUM_BAND_COUNT,
    SCHEMA_DESCRIPTOR_BYTES,
    SCHEMA_PARAMS_PER_CHUNK,
    PACKET_SIZE,
    PID,
    parse_schema_chunk,
    RAW_HID_INTERFACE,
    RAW_HID_USAGE_PAGE,
    Status,
    VID,
)


def find_device_path(logger=print):
    """Find the Raw HID interface path."""
    if logger is not None:
        logger(f"Searching for device VID=0x{VID:04x} PID=0x{PID:04x}...")
    
    for d in hid.enumerate(VID, PID):
        if logger is not None:
            logger(
                f"  -> Found: {d['product_string']} (Interface: {d['interface_number']}, UsagePage: 0x{d['usage_page']:04x})"
            )
        
        # Raw HID is on Interface 1 or Usage Page 0xFF00
        if d['interface_number'] == 1 or d['usage_page'] == 0xFF00:
            return d['path']
    
    return None

class KBHEDevice:
    """KBHE keyboard device communication class."""
    
    def __init__(self):
        self.device = None
        self.path = None
        self._io_lock = threading.Lock()
    
    def connect(self, logger=print):
        """Connect to the device."""
        # Ensure stale handles from a previous unplug are fully released first.
        self.disconnect()

        self.path = find_device_path(logger=logger)
        if self.path is None:
            raise Exception("Device not found")
        
        if logger is not None:
            logger(f"✅ Raw HID interface found: {self.path}")
        
        self.device = hid.device()
        self.device.open_path(self.path)
        self.device.set_nonblocking(1)
        
        return True

    def reconnect(self, logger=None):
        """Reconnect to the device using a fresh HID handle."""
        self.disconnect()
        return self.connect(logger=logger)
    
    def disconnect(self):
        """Disconnect from the device."""
        if self.device:
            try:
                self.device.close()
            except Exception:
                pass
            self.device = None
    
    def send_command(self, cmd_id, data=None, timeout_ms=100):
        """Send a command and wait for response."""
        if self.device is None:
            return None

        with self._io_lock:
            # Flush stale responses that could belong to previous requests.
            for _ in range(8):
                stale = self.device.read(PACKET_SIZE)
                if not stale:
                    break

            # Build packet
            packet = [0] * (PACKET_SIZE + 1)
            packet[0] = 0x00  # Report ID (required by hidapi on Windows)
            packet[1] = cmd_id

            if data:
                for i, byte in enumerate(data):
                    if i + 2 < len(packet):
                        packet[i + 2] = byte

            # Send
            self.device.write(packet)

            # Read until timeout and keep the response matching this command.
            deadline = time.time() + (timeout_ms / 1000.0)
            while time.time() < deadline:
                response = self.device.read(PACKET_SIZE)
                if response and len(response) >= 2:
                    if response[0] == int(cmd_id):
                        return response
                time.sleep(0.001)

        return None

    @staticmethod
    def _unpack_u16(data, offset):
        return data[offset] | (data[offset + 1] << 8)

    @staticmethod
    def _chunk_count(total_count, start_index, chunk_size):
        remaining = max(0, int(total_count) - int(start_index))
        return min(chunk_size, remaining)

    @staticmethod
    def _sanitize_socd_resolution(value):
        try:
            value = int(value)
        except Exception:
            return 0
        return value if value in (0, 1, 2, 3, 4) else 0

    @staticmethod
    def _sanitize_key_behavior_mode(value):
        try:
            value = int(value)
        except Exception:
            return int(KEY_BEHAVIORS["Normal"])
        return value if value in KEY_BEHAVIORS.values() else int(KEY_BEHAVIORS["Normal"])

    @staticmethod
    def _sanitize_advanced_tick_rate(value):
        try:
            value = int(value)
        except Exception:
            return int(ADVANCED_TICK_RATE_DEFAULT)
        return max(int(ADVANCED_TICK_RATE_MIN), min(int(ADVANCED_TICK_RATE_MAX), value))

    @staticmethod
    def _default_dynamic_zones(primary_keycode=0x14):
        return [
            {"end_mm_tenths": 40, "end_mm": 4.0, "hid_keycode": int(primary_keycode)},
            {"end_mm_tenths": 40, "end_mm": 4.0, "hid_keycode": 0},
            {"end_mm_tenths": 40, "end_mm": 4.0, "hid_keycode": 0},
            {"end_mm_tenths": 40, "end_mm": 4.0, "hid_keycode": 0},
        ]

    @classmethod
    def _sanitize_dynamic_zones(cls, zones, primary_keycode=0x14):
        defaults = cls._default_dynamic_zones(primary_keycode)
        sanitized = []
        previous_end = 0

        for index in range(4):
            source = zones[index] if isinstance(zones, (list, tuple)) and index < len(zones) else defaults[index]
            if isinstance(source, dict):
                if "end_mm_tenths" in source:
                    end_mm_tenths = int(source.get("end_mm_tenths", defaults[index]["end_mm_tenths"]))
                else:
                    end_mm_tenths = int(round(float(source.get("end_mm", defaults[index]["end_mm"])) * 10.0))
                hid_keycode = int(source.get("hid_keycode", defaults[index]["hid_keycode"]))
            else:
                end_mm_tenths = defaults[index]["end_mm_tenths"]
                hid_keycode = defaults[index]["hid_keycode"]

            end_mm_tenths = max(previous_end or 1, min(40, end_mm_tenths))
            previous_end = end_mm_tenths
            sanitized.append(
                {
                    "end_mm_tenths": end_mm_tenths,
                    "end_mm": end_mm_tenths / 10.0,
                    "hid_keycode": hid_keycode,
                }
            )

        if all(int(zone["hid_keycode"]) == 0 for zone in sanitized):
            sanitized[0]["hid_keycode"] = int(primary_keycode)

        return sanitized

    @staticmethod
    def _default_gamepad_curve_points():
        return [
            {"x_01mm": 0, "x_mm": 0.00, "y": 0},
            {"x_01mm": 133, "x_mm": 1.33, "y": 85},
            {"x_01mm": 266, "x_mm": 2.66, "y": 170},
            {"x_01mm": 400, "x_mm": 4.00, "y": 255},
        ]

    @staticmethod
    def _sanitize_gamepad_routing(value):
        try:
            value = int(value)
        except Exception:
            return int(GAMEPAD_KEYBOARD_ROUTING["All Keys"])
        return (
            value
            if value in GAMEPAD_KEYBOARD_ROUTING.values()
            else int(GAMEPAD_KEYBOARD_ROUTING["All Keys"])
        )

    @staticmethod
    def _sanitize_gamepad_api_mode(value):
        try:
            value = int(value)
        except Exception:
            return int(GAMEPAD_API_MODES["HID (DirectInput)"])
        return (
            value
            if value in GAMEPAD_API_MODES.values()
            else int(GAMEPAD_API_MODES["HID (DirectInput)"])
        )

    @classmethod
    def _sanitize_gamepad_curve_points(cls, points):
        defaults = cls._default_gamepad_curve_points()
        sanitized = []
        previous_x = 0

        for index in range(GAMEPAD_CURVE_POINT_COUNT):
            source = points[index] if isinstance(points, (list, tuple)) and index < len(points) else defaults[index]
            if isinstance(source, dict):
                if "x_01mm" in source:
                    x_01mm = int(source.get("x_01mm", defaults[index]["x_01mm"]))
                else:
                    x_01mm = int(round(float(source.get("x_mm", defaults[index]["x_mm"])) * 100.0))
                y = int(source.get("y", defaults[index]["y"]))
            else:
                x_01mm = defaults[index]["x_01mm"]
                y = defaults[index]["y"]

            x_01mm = max(previous_x, min(int(GAMEPAD_CURVE_MAX_DISTANCE_MM * 100), x_01mm))
            y = max(0, min(255, y))
            previous_x = x_01mm
            sanitized.append({"x_01mm": x_01mm, "x_mm": x_01mm / 100.0, "y": y})

        return sanitized

    @staticmethod
    def _gamepad_curve_start_deadzone(points):
        if not points:
            return 0
        start_01mm = max(0, min(int(GAMEPAD_CURVE_MAX_DISTANCE_MM * 100), int(points[0].get("x_01mm", 0))))
        return int(round((start_01mm * 255.0) / (GAMEPAD_CURVE_MAX_DISTANCE_MM * 100.0)))

    @staticmethod
    def _decode_c_string(data):
        raw = bytes(int(v) & 0xFF for v in data)
        return raw.split(b"\x00", 1)[0].decode("ascii", errors="ignore")
    
    def get_firmware_version(self):
        """Get firmware version."""
        resp = self.send_command(Command.GET_FIRMWARE_VERSION)
        if resp and len(resp) >= 4:
            version = resp[2] | (resp[3] << 8)
            major = (version >> 8) & 0xFF
            minor = version & 0xFF
            return f"{major}.{minor}"
        return None

    def get_device_info(self):
        """Get firmware version, serial number, and keyboard name."""
        resp = self.send_command(Command.GET_DEVICE_INFO)
        if resp and len(resp) >= 64 and resp[1] == Status.OK:
            version = resp[2] | (resp[3] << 8)
            major = (version >> 8) & 0xFF
            minor = version & 0xFF
            serial_start = 4
            serial_end = serial_start + DEVICE_SERIAL_LENGTH
            keyboard_end = serial_end + KEYBOARD_NAME_LENGTH
            serial = self._decode_c_string(resp[serial_start:serial_end])
            keyboard_name = self._decode_c_string(resp[serial_end:keyboard_end])
            return {
                "firmware_version": f"{major}.{minor}",
                "firmware_version_raw": int(version),
                "serial_number": serial,
                "keyboard_name": keyboard_name,
            }
        return None

    def get_keyboard_name(self):
        """Get persistent keyboard custom name."""
        resp = self.send_command(Command.GET_KEYBOARD_NAME)
        if resp and len(resp) >= 34 and resp[1] == Status.OK:
            return self._decode_c_string(resp[2:34])
        return None

    def set_keyboard_name(self, name):
        """Set persistent keyboard custom name (max 32 bytes)."""
        if name is None:
            name = ""
        name_bytes = str(name).encode("ascii", errors="ignore")[:KEYBOARD_NAME_LENGTH]
        payload = [0] + list(name_bytes) + [0] * (KEYBOARD_NAME_LENGTH - len(name_bytes))
        resp = self.send_command(Command.SET_KEYBOARD_NAME, payload, timeout_ms=3000)
        return resp and len(resp) >= 34 and resp[1] == Status.OK

    def get_profile_state(self):
        """Get active profile and used-slot mask from MCU."""
        resp = self.send_command(Command.GET_ACTIVE_PROFILE)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            active_profile = int(resp[2])
            used_mask = int(resp[3]) & ((1 << SETTINGS_PROFILE_COUNT) - 1)
            if 0 <= active_profile < SETTINGS_PROFILE_COUNT:
                return {
                    'active_profile': active_profile,
                    'used_mask': used_mask,
                    'used_slots': [i for i in range(SETTINGS_PROFILE_COUNT) if used_mask & (1 << i)],
                }
        return None

    def get_active_profile(self):
        """Get active persistent profile index (0..3)."""
        state = self.get_profile_state()
        if not state:
            return None
        return int(state['active_profile'])

    def set_active_profile(self, profile_index):
        """Set active persistent profile index (0..3)."""
        try:
            profile_index = int(profile_index)
        except Exception:
            return False
        if profile_index < 0 or profile_index >= SETTINGS_PROFILE_COUNT:
            return False

        payload = [0, profile_index]
        resp = self.send_command(Command.SET_ACTIVE_PROFILE, payload, timeout_ms=3000)
        return resp and len(resp) >= 4 and resp[1] == Status.OK

    def get_profile_name(self, profile_index):
        """Get profile name for one slot."""
        try:
            profile_index = int(profile_index)
        except Exception:
            return None
        if profile_index < 0 or profile_index >= SETTINGS_PROFILE_COUNT:
            return None

        payload = [0, profile_index]
        resp = self.send_command(Command.GET_PROFILE_NAME, payload, timeout_ms=3000)
        if resp and len(resp) >= 4 + SETTINGS_PROFILE_NAME_LENGTH and resp[1] == Status.OK:
            return self._decode_c_string(resp[4:4 + SETTINGS_PROFILE_NAME_LENGTH])
        return None

    def set_profile_name(self, profile_index, name):
        """Set profile name for one slot (ASCII, fixed-size payload)."""
        try:
            profile_index = int(profile_index)
        except Exception:
            return False
        if profile_index < 0 or profile_index >= SETTINGS_PROFILE_COUNT:
            return False

        if name is None:
            name = ""
        name_bytes = str(name).encode("ascii", errors="ignore")[:SETTINGS_PROFILE_NAME_LENGTH]
        payload = [0, profile_index] + list(name_bytes)
        payload += [0] * (SETTINGS_PROFILE_NAME_LENGTH - len(name_bytes))

        resp = self.send_command(Command.SET_PROFILE_NAME, payload, timeout_ms=3000)
        return resp and len(resp) >= 4 + SETTINGS_PROFILE_NAME_LENGTH and resp[1] == Status.OK

    def create_profile(self, name=""):
        """Create a profile in the first free slot on MCU (max 4)."""
        if name is None:
            name = ""
        name_bytes = str(name).encode("ascii", errors="ignore")[:SETTINGS_PROFILE_NAME_LENGTH]
        payload = [0, 0xFF] + list(name_bytes)
        payload += [0] * (SETTINGS_PROFILE_NAME_LENGTH - len(name_bytes))

        resp = self.send_command(Command.CREATE_PROFILE, payload, timeout_ms=3000)
        if resp and len(resp) >= 4 + SETTINGS_PROFILE_NAME_LENGTH and resp[1] == Status.OK:
            return {
                'profile_index': int(resp[2]),
                'used_mask': int(resp[3]) & ((1 << SETTINGS_PROFILE_COUNT) - 1),
                'profile_name': self._decode_c_string(resp[4:4 + SETTINGS_PROFILE_NAME_LENGTH]),
            }
        return None

    def delete_profile(self, profile_index):
        """Delete one MCU profile slot (fails if it is the last remaining slot)."""
        try:
            profile_index = int(profile_index)
        except Exception:
            return None
        if profile_index < 0 or profile_index >= SETTINGS_PROFILE_COUNT:
            return None

        payload = [0, profile_index]
        resp = self.send_command(Command.DELETE_PROFILE, payload, timeout_ms=3000)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            return {
                'active_profile': int(resp[2]),
                'used_mask': int(resp[3]) & ((1 << SETTINGS_PROFILE_COUNT) - 1),
            }
        return None

    def copy_profile_slot(self, source_profile_index, target_profile_index):
        """Copy source profile slot into target profile slot on MCU."""
        try:
            source_profile_index = int(source_profile_index)
            target_profile_index = int(target_profile_index)
        except Exception:
            return None

        if (
            source_profile_index < 0
            or source_profile_index >= SETTINGS_PROFILE_COUNT
            or target_profile_index < 0
            or target_profile_index >= SETTINGS_PROFILE_COUNT
        ):
            return None

        payload = [0, source_profile_index, target_profile_index]
        resp = self.send_command(Command.COPY_PROFILE_SLOT, payload, timeout_ms=3000)
        if resp and len(resp) >= 5 and resp[1] == Status.OK:
            return {
                'source_profile_index': int(resp[2]),
                'target_profile_index': int(resp[3]),
                'used_mask': int(resp[4]) & ((1 << SETTINGS_PROFILE_COUNT) - 1),
            }
        return None

    def reset_profile_slot(self, profile_index):
        """Reset one used profile slot to firmware defaults."""
        try:
            profile_index = int(profile_index)
        except Exception:
            return None
        if profile_index < 0 or profile_index >= SETTINGS_PROFILE_COUNT:
            return None

        payload = [0, profile_index]
        resp = self.send_command(Command.RESET_PROFILE_SLOT, payload, timeout_ms=3000)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            return {
                'profile_index': int(resp[2]),
                'used_mask': int(resp[3]) & ((1 << SETTINGS_PROFILE_COUNT) - 1),
            }
        return None

    def get_profile_names(self):
        """Get profile names in slot order (unused slots return None)."""
        state = self.get_profile_state()
        if not state:
            return None

        used_mask = int(state['used_mask'])
        names = [None] * SETTINGS_PROFILE_COUNT
        for i in range(SETTINGS_PROFILE_COUNT):
            if not (used_mask & (1 << i)):
                continue
            name = self.get_profile_name(i)
            if name is None:
                return None
            names[i] = name
        return names

    def list_profiles(self):
        """Return used MCU profiles as [{'index','name','is_active'}]."""
        state = self.get_profile_state()
        if not state:
            return None

        active = int(state['active_profile'])
        used_slots = list(state['used_slots'])
        profiles = []
        for idx in used_slots:
            name = self.get_profile_name(idx)
            if name is None:
                return None
            profiles.append({
                'index': int(idx),
                'name': str(name),
                'is_active': int(idx) == active,
            })
        return profiles
    
    def get_options(self):
        """Get all options."""
        resp = self.send_command(Command.GET_OPTIONS)
        if resp and len(resp) >= 5 and resp[1] == Status.OK:
            return {
                'keyboard_enabled': bool(resp[2]),
                'gamepad_enabled': bool(resp[3]),
                'raw_hid_echo': bool(resp[4]),
                'led_thermal_protection_enabled': bool(resp[5]) if len(resp) >= 6 else False,
            }
        return None
    
    def set_keyboard_enabled(self, enabled):
        """Set keyboard enabled state."""
        data = [0, 1 if enabled else 0]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_KEYBOARD_ENABLED, data, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def set_gamepad_enabled(self, enabled):
        """Set gamepad enabled state."""
        data = [0, 1 if enabled else 0]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_GAMEPAD_ENABLED, data, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def get_nkro_enabled(self):
        """Get keyboard mode flag (True=Auto NKRO, False=6KRO only)."""
        resp = self.send_command(Command.GET_NKRO_ENABLED)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return bool(resp[2])
        return None
    
    def set_nkro_enabled(self, enabled):
        """Set keyboard mode flag (True=Auto NKRO, False=6KRO only)."""
        data = [0, 1 if enabled else 0]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_NKRO_ENABLED, data, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def get_advanced_tick_rate(self):
        """Get advanced key tick rate (scan ticks between consecutive advanced actions)."""
        resp = self.send_command(Command.GET_ADVANCED_TICK_RATE)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return self._sanitize_advanced_tick_rate(resp[2])
        return None

    def set_advanced_tick_rate(self, tick_rate):
        """Set advanced key tick rate."""
        value = self._sanitize_advanced_tick_rate(tick_rate)
        data = [0, value]
        resp = self.send_command(Command.SET_ADVANCED_TICK_RATE, data, timeout_ms=300)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def save_settings(self):
        """Force an immediate settings flush to flash."""
        # Consolidation can still erase the sector when the append log is full.
        resp = self.send_command(Command.SAVE_SETTINGS, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def factory_reset(self):
        """Reset to factory defaults."""
        # Factory reset also erases flash, needs longer timeout
        resp = self.send_command(Command.FACTORY_RESET, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def usb_reenumerate(self, timeout_s=6.0, logger=None):
        """Request a USB-only re-enumeration and reconnect the Raw HID handle."""
        resp = self.send_command(Command.USB_REENUMERATE, timeout_ms=500)
        if not resp or len(resp) < 2 or resp[1] != Status.OK:
            return False

        self.disconnect()
        time.sleep(0.35)

        deadline = time.time() + max(1.0, float(timeout_s))
        last_error = None
        while time.time() < deadline:
            try:
                self.connect(logger=logger)
                return True
            except Exception as exc:
                last_error = exc
                time.sleep(0.25)

        if logger is not None and last_error is not None:
            logger(f"USB re-enumeration reconnect failed: {last_error}")
        return False
    
    # --- LED Matrix Commands ---
    
    def led_get_enabled(self):
        """Get LED matrix enabled state."""
        resp = self.send_command(Command.GET_LED_ENABLED)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return bool(resp[2])
        return None
    
    def led_set_enabled(self, enabled):
        """Set LED matrix enabled state."""
        data = [0, 1 if enabled else 0]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_LED_ENABLED, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def led_get_brightness(self):
        """Get LED brightness."""
        resp = self.send_command(Command.GET_LED_BRIGHTNESS)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return resp[2]
        return None
    
    def led_set_brightness(self, brightness):
        """Set LED brightness (0-255)."""
        data = [0, min(255, max(0, brightness))]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_LED_BRIGHTNESS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def led_set_pixel(self, index, r, g, b):
        """Set a single LED pixel."""
        # Packet format: [cmd] [status_placeholder] [index] [r] [g] [b]
        data = [0, index, r, g, b]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_LED_PIXEL, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def led_get_pixel(self, index):
        """Get a single LED pixel."""
        data = [0, index]
        resp = self.send_command(Command.GET_LED_PIXEL, data)
        if resp and len(resp) >= 6 and resp[1] == Status.OK:
            return [int(resp[3]), int(resp[4]), int(resp[5])]
        return None
    
    def led_get_row(self, row):
        """Get a row of pixels (8 pixels = 24 bytes RGB)."""
        data = [0, row]  # 0 = placeholder for status byte
        resp = self.send_command(Command.GET_LED_ROW, data)
        if resp and len(resp) >= 27 and resp[1] == Status.OK:
            return list(resp[3:27])
        return None
    
    def led_set_row(self, row, pixels):
        """Set a row of pixels (8 pixels = 24 bytes RGB)."""
        data = [0, row] + list(pixels[:24])  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_LED_ROW, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def led_clear(self):
        """Clear all LEDs (set to black)."""
        resp = self.send_command(Command.LED_CLEAR)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def led_fill(self, r, g, b):
        """Fill all LEDs with a color."""
        # Packet format: [cmd] [status_placeholder] [r] [g] [b]
        data = [0, r, g, b]  # 0 = placeholder for status byte
        resp = self.send_command(Command.LED_FILL, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def led_test_rainbow(self):
        """Show rainbow test pattern."""
        resp = self.send_command(Command.LED_TEST_RAINBOW)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def led_set_volume_overlay(self, level):
        """Push a host-driven volume overlay level (0-255)."""
        data = [0, min(255, max(0, int(level)))]
        resp = self.send_command(Command.SET_LED_VOLUME_OVERLAY, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def led_clear_volume_overlay(self):
        """Clear the host-driven volume overlay."""
        resp = self.send_command(Command.CLEAR_LED_VOLUME_OVERLAY)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def led_upload_all(self, pixels):
        """Upload the full persisted LED frame in HID chunks."""
        pixel_bytes = list(int(v) & 0xFF for v in pixels)
        total_size = KEY_COUNT * 3
        if len(pixel_bytes) < total_size:
            pixel_bytes.extend([0] * (total_size - len(pixel_bytes)))
        else:
            pixel_bytes = pixel_bytes[:total_size]

        chunk_idx = 0
        for offset in range(0, total_size, LED_BYTES_PER_CHUNK):
            chunk = pixel_bytes[offset:offset + LED_BYTES_PER_CHUNK]
            data = [0, chunk_idx, len(chunk)] + chunk
            resp = self.send_command(Command.SET_LED_ALL_CHUNK, data, timeout_ms=200)
            if not resp or len(resp) < 4 or resp[1] != Status.OK:
                return False
            chunk_idx += 1
            time.sleep(0.01)
        return True

    def led_download_all(self):
        """Download the full live LED frame from the device."""
        total_size = KEY_COUNT * 3
        pixels = []
        chunk_idx = 0

        while len(pixels) < total_size:
            data = [0, chunk_idx]
            resp = self.send_command(Command.GET_LED_ALL, data, timeout_ms=200)
            if not resp or len(resp) < 4 or resp[1] != Status.OK:
                return None

            returned_chunk = int(resp[2])
            chunk_size = int(resp[3])
            if returned_chunk != chunk_idx or chunk_size <= 0:
                return None

            payload_end = 4 + chunk_size
            if payload_end > len(resp):
                return None

            pixels.extend(int(v) for v in resp[4:payload_end])
            chunk_idx += 1

        return pixels[:total_size]
    
    # --- Key Settings Commands ---
    
    def get_key_settings(self, key_index, profile_index=None, layer_index=0):
        """Get settings for a specific key/profile/layer (extended format)."""
        key_index = max(0, min(int(KEY_COUNT) - 1, int(key_index)))
        layer_index = max(0, min(int(LAYER_COUNT) - 1, int(layer_index)))
        if profile_index is None:
            active = self.get_active_profile()
            profile_index = 0 if active is None else int(active)
        profile_index = max(0, min(int(SETTINGS_PROFILE_COUNT) - 1, int(profile_index)))

        data = [0, key_index, profile_index, layer_index]  # 0 = placeholder for status
        resp = self.send_command(Command.GET_KEY_SETTINGS, data)
        if resp and len(resp) >= 37 and resp[1] == Status.OK:
            primary_keycode = self._unpack_u16(resp, 5)
            dynamic_zones = self._default_dynamic_zones(primary_keycode)
            if len(resp) >= 33:
                dynamic_zones = self._sanitize_dynamic_zones(
                    [
                        {
                            "end_mm_tenths": int(resp[21 + i * 3]),
                            "hid_keycode": self._unpack_u16(resp, 22 + i * 3),
                        }
                        for i in range(4)
                    ],
                    primary_keycode=primary_keycode,
                )
            tap_hold_options = int(resp[33]) if len(resp) > 33 else 0
            dks_bottom_out_point = int(resp[34]) if len(resp) > 34 else 40
            socd_fully_pressed_enabled = bool(resp[35]) if len(resp) > 35 else False
            socd_fully_pressed_point = int(resp[36]) if len(resp) > 36 else 40
            return {
                'key_index': resp[2],
                'profile_index': int(resp[3]),
                'layer_index': int(resp[4]),
                'hid_keycode': primary_keycode,
                'actuation_point_mm': resp[7] / 10.0,  # 0.1mm to mm
                'release_point_mm': resp[8] / 10.0,
                'rapid_trigger_press': resp[9] / 100.0,  # 0.01mm to mm
                'rapid_trigger_release': resp[10] / 100.0,
                'socd_pair': resp[11] if resp[11] != 255 else None,
                'socd_resolution': self._sanitize_socd_resolution(resp[12]),
                'rapid_trigger_enabled': bool(resp[13]),
                'disable_kb_on_gamepad': bool(resp[14]),
                'continuous_rapid_trigger': bool(resp[15]),
                'behavior_mode': self._sanitize_key_behavior_mode(resp[16]),
                'hold_threshold_ms': int(resp[17]) * 10,
                'dynamic_zone_count': max(1, min(4, int(resp[18]))),
                'secondary_hid_keycode': self._unpack_u16(resp, 19),
                'dynamic_zones': dynamic_zones,
                'tap_hold_hold_on_other_key_press': bool(tap_hold_options & 0x01),
                'tap_hold_uppercase_hold': bool(tap_hold_options & 0x02),
                'dks_bottom_out_point_mm': dks_bottom_out_point / 10.0,
                'socd_fully_pressed_enabled': socd_fully_pressed_enabled,
                'socd_fully_pressed_point_mm': socd_fully_pressed_point / 10.0,
            }
        return None

    def get_layer_keycode(self, layer_index, key_index):
        """Get the keycode assigned to one layer/key slot."""
        layer_index = max(0, min(int(LAYER_COUNT) - 1, int(layer_index)))
        key_index = max(0, min(int(KEY_COUNT) - 1, int(key_index)))
        resp = self.send_command(
            Command.GET_LAYER_KEYCODE, [0, layer_index, key_index], timeout_ms=150
        )
        if resp and len(resp) >= 6 and resp[1] == Status.OK:
            return {
                "layer_index": int(resp[2]),
                "key_index": int(resp[3]),
                "hid_keycode": self._unpack_u16(resp, 4),
            }
        return None

    def set_layer_keycode(self, layer_index, key_index, hid_keycode):
        """Set the keycode assigned to one layer/key slot."""
        layer_index = max(0, min(int(LAYER_COUNT) - 1, int(layer_index)))
        key_index = max(0, min(int(KEY_COUNT) - 1, int(key_index)))
        hid_keycode = int(hid_keycode) & 0xFFFF
        data = [0, layer_index, key_index, hid_keycode & 0xFF, (hid_keycode >> 8) & 0xFF]
        resp = self.send_command(Command.SET_LAYER_KEYCODE, data, timeout_ms=150)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def reset_key_trigger_settings(self, key_index):
        """Reset actuation and rapid-trigger fields for one key to firmware defaults."""
        key_index = max(0, min(int(KEY_COUNT) - 1, int(key_index)))
        resp = self.send_command(
            Command.RESET_KEY_TRIGGER_SETTINGS, [0, key_index], timeout_ms=200
        )
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def set_key_settings_extended(self, key_index, settings, profile_index=None, layer_index=0):
        """Set settings for a specific key/profile/layer (extended format)."""
        key_index = max(0, min(int(KEY_COUNT) - 1, int(key_index)))
        layer_index = max(0, min(int(LAYER_COUNT) - 1, int(layer_index)))
        if profile_index is None:
            active = self.get_active_profile()
            profile_index = 0 if active is None else int(active)
        profile_index = max(0, min(int(SETTINGS_PROFILE_COUNT) - 1, int(profile_index)))

        hid_keycode = int(settings.get('hid_keycode', 0x14))
        dynamic_zones = self._sanitize_dynamic_zones(
            settings.get('dynamic_zones'),
            primary_keycode=hid_keycode,
        )
        data = [
            0,  # placeholder for status
            key_index,
            profile_index,
            layer_index,
            hid_keycode & 0xFF,
            (hid_keycode >> 8) & 0xFF,
            int(settings.get('actuation_point_mm', 2.0) * 10),  # mm to 0.1mm
            int(settings.get('release_point_mm', 1.8) * 10),
            int(settings.get('rapid_trigger_press', 0.3) * 100),  # mm to 0.01mm
            int(settings.get('rapid_trigger_release', 0.3) * 100),
            settings.get('socd_pair', 255),
            self._sanitize_socd_resolution(settings.get('socd_resolution', 0)),
            1 if settings.get('rapid_trigger_enabled', False) else 0,
            1 if settings.get('disable_kb_on_gamepad', False) else 0,
            1 if settings.get('continuous_rapid_trigger', False) else 0,
            self._sanitize_key_behavior_mode(settings.get('behavior_mode', 0)),
            max(1, min(255, int(round(settings.get('hold_threshold_ms', 200) / 10.0)))),
            max(1, min(4, int(settings.get('dynamic_zone_count', 1)))),
            int(settings.get('secondary_hid_keycode', 0)) & 0xFF,
            (int(settings.get('secondary_hid_keycode', 0)) >> 8) & 0xFF,
        ]
        for zone in dynamic_zones:
            data.append(int(zone['end_mm_tenths']) & 0xFF)
            data.append(int(zone['hid_keycode']) & 0xFF)
            data.append((int(zone['hid_keycode']) >> 8) & 0xFF)
        tap_hold_options = 0
        if settings.get('tap_hold_hold_on_other_key_press', False):
            tap_hold_options |= 0x01
        if settings.get('tap_hold_uppercase_hold', False):
            tap_hold_options |= 0x02
        dks_bottom_out_point = int(round(settings.get('dks_bottom_out_point_mm', 4.0) * 10.0))
        dks_bottom_out_point = max(1, min(40, dks_bottom_out_point))
        socd_fully_pressed_point = int(round(settings.get('socd_fully_pressed_point_mm', 4.0) * 10.0))
        socd_fully_pressed_point = max(1, min(40, socd_fully_pressed_point))
        data.extend([
            tap_hold_options,
            dks_bottom_out_point,
            1 if settings.get('socd_fully_pressed_enabled', False) else 0,
            socd_fully_pressed_point,
        ])
        resp = self.send_command(Command.SET_KEY_SETTINGS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def get_all_key_settings(self):
        """Get settings for all keys in multiple HID chunks."""
        keys = []
        next_index = 0

        while next_index < KEY_COUNT:
            resp = self.send_command(Command.GET_ALL_KEY_SETTINGS, [0, next_index], timeout_ms=150)
            if not resp or len(resp) < 4 or resp[1] != Status.OK:
                return None

            start_index = int(resp[2])
            key_count = int(resp[3])
            if start_index != next_index or key_count <= 0 or key_count > KEY_SETTINGS_PER_CHUNK:
                return None

            for i in range(key_count):
                offset = 4 + i * 8
                hid_keycode = self._unpack_u16(resp, offset)
                flags = resp[offset + 7]
                keys.append({
                    'hid_keycode': hid_keycode,
                    'actuation_point_mm': resp[offset + 2] / 10.0,
                    'release_point_mm': resp[offset + 3] / 10.0,
                    'rapid_trigger_press': resp[offset + 4] / 100.0,
                    'rapid_trigger_release': resp[offset + 5] / 100.0,
                    'socd_pair': resp[offset + 6] if resp[offset + 6] != 255 else None,
                    'socd_resolution': self._sanitize_socd_resolution((flags >> 2) & 0x07),
                    'rapid_trigger_enabled': bool(flags & 0x01),
                    'disable_kb_on_gamepad': bool(flags & 0x02),
                    'continuous_rapid_trigger': False,
                    'behavior_mode': int(KEY_BEHAVIORS["Normal"]),
                    'hold_threshold_ms': 200,
                    'secondary_hid_keycode': 0,
                    'dynamic_zone_count': 1,
                    'dynamic_zones': self._default_dynamic_zones(hid_keycode),
                })

            next_index += key_count

        return keys
    
    # --- Gamepad Settings Commands ---
    
    def get_gamepad_settings(self):
        """Get gamepad settings."""
        resp = self.send_command(Command.GET_GAMEPAD_SETTINGS)
        if resp and len(resp) >= 18 and resp[1] == Status.OK:
            routing = self._sanitize_gamepad_routing(resp[3])
            api_mode = int(GAMEPAD_API_MODES["HID (DirectInput)"])
            offset = 6
            if len(resp) >= 19:
                api_mode = self._sanitize_gamepad_api_mode(resp[6])
                offset = 7
            points = []
            for _ in range(GAMEPAD_CURVE_POINT_COUNT):
                x_01mm = self._unpack_u16(resp, offset)
                y = int(resp[offset + 2])
                points.append({"x_01mm": x_01mm, "x_mm": x_01mm / 100.0, "y": y})
                offset += 3
            deadzone = self._gamepad_curve_start_deadzone(points)
            return {
                'radial_deadzone': deadzone,
                'deadzone': deadzone,
                'keyboard_routing': routing,
                'keep_keyboard_output': routing != int(GAMEPAD_KEYBOARD_ROUTING["Disabled"]),
                'mapped_keys_replace_keyboard': routing == int(GAMEPAD_KEYBOARD_ROUTING["Unmapped Only"]),
                'square_mode': bool(resp[4]),
                'reactive_stick': bool(resp[5]),
                'snappy_mode': bool(resp[5]),
                'api_mode': api_mode,
                'curve_points': self._sanitize_gamepad_curve_points(points),
            }
        return None

    def set_gamepad_settings(self, settings):
        """Set gamepad settings from the canonical dictionary payload."""
        settings = dict(settings or {})

        routing = self._sanitize_gamepad_routing(
            settings.get("keyboard_routing", GAMEPAD_KEYBOARD_ROUTING["All Keys"])
        )
        api_mode = self._sanitize_gamepad_api_mode(
            settings.get("api_mode", GAMEPAD_API_MODES["HID (DirectInput)"])
        )
        points = self._sanitize_gamepad_curve_points(settings.get("curve_points"))
        deadzone = self._gamepad_curve_start_deadzone(points)

        data = [
            0,
            deadzone,
            routing,
            1 if settings.get("square_mode", False) else 0,
            1 if settings.get("reactive_stick", settings.get("snappy_mode", False)) else 0,
            api_mode,
        ]
        for point in points:
            data.append(point["x_01mm"] & 0xFF)
            data.append((point["x_01mm"] >> 8) & 0xFF)
            data.append(point["y"] & 0xFF)

        resp = self.send_command(Command.SET_GAMEPAD_SETTINGS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def get_rotary_encoder_settings(self):
        """Get rotary encoder settings."""
        resp = self.send_command(Command.GET_ROTARY_ENCODER_SETTINGS)
        if resp and len(resp) >= 38 and resp[1] == Status.OK:
            return {
                'rotation_action': int(resp[2]),
                'button_action': int(resp[3]),
                'sensitivity': int(resp[4]),
                'step_size': int(resp[5]),
                'invert_direction': bool(resp[6]),
                'rgb_behavior': int(resp[7]),
                'rgb_effect_mode': int(resp[8]),
                'progress_style': int(resp[9]),
                'progress_effect_mode': int(resp[10]),
                'progress_color': [int(resp[11]), int(resp[12]), int(resp[13])],
                'cw_binding': {
                    'mode': int(resp[14]),
                    'keycode': self._unpack_u16(resp, 15),
                    'modifier_mask_exact': int(resp[17]),
                    'fallback_no_mod_keycode': self._unpack_u16(resp, 18),
                    'layer_mode': int(resp[20]),
                    'layer_index': int(resp[21]),
                },
                'ccw_binding': {
                    'mode': int(resp[22]),
                    'keycode': self._unpack_u16(resp, 23),
                    'modifier_mask_exact': int(resp[25]),
                    'fallback_no_mod_keycode': self._unpack_u16(resp, 26),
                    'layer_mode': int(resp[28]),
                    'layer_index': int(resp[29]),
                },
                'click_binding': {
                    'mode': int(resp[30]),
                    'keycode': self._unpack_u16(resp, 31),
                    'modifier_mask_exact': int(resp[33]),
                    'fallback_no_mod_keycode': self._unpack_u16(resp, 34),
                    'layer_mode': int(resp[36]),
                    'layer_index': int(resp[37]),
                },
            }
        return None

    def set_rotary_encoder_settings(self, settings):
        """Set rotary encoder settings."""
        def _binding_payload(binding):
            binding = dict(binding or {})
            keycode = int(binding.get('keycode', 0)) & 0xFFFF
            fallback = int(binding.get('fallback_no_mod_keycode', 0)) & 0xFFFF
            return [
                int(binding.get('mode', 0)) & 0xFF,
                keycode & 0xFF,
                (keycode >> 8) & 0xFF,
                int(binding.get('modifier_mask_exact', 0)) & 0xFF,
                fallback & 0xFF,
                (fallback >> 8) & 0xFF,
                int(binding.get('layer_mode', 0)) & 0xFF,
                int(binding.get('layer_index', 0)) & 0xFF,
            ]

        progress_color = list(settings.get('progress_color', [40, 210, 64]))
        while len(progress_color) < 3:
            progress_color.append(0)

        cw_binding = settings.get('cw_binding', {'mode': 0})
        ccw_binding = settings.get('ccw_binding', {'mode': 0})
        click_binding = settings.get('click_binding', {'mode': 0})

        data = [
            0,
            int(settings.get('rotation_action', 0)) & 0xFF,
            int(settings.get('button_action', 0)) & 0xFF,
            int(settings.get('sensitivity', 1)) & 0xFF,
            int(settings.get('step_size', 1)) & 0xFF,
            1 if settings.get('invert_direction', False) else 0,
            int(settings.get('rgb_behavior', 0)) & 0xFF,
            int(settings.get('rgb_effect_mode', 4)) & 0xFF,
            int(settings.get('progress_style', 0)) & 0xFF,
            int(settings.get('progress_effect_mode', 1)) & 0xFF,
            int(progress_color[0]) & 0xFF,
            int(progress_color[1]) & 0xFF,
            int(progress_color[2]) & 0xFF,
        ]
        data.extend(_binding_payload(cw_binding))
        data.extend(_binding_payload(ccw_binding))
        data.extend(_binding_payload(click_binding))
        resp = self.send_command(Command.SET_ROTARY_ENCODER_SETTINGS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def get_rotary_state(self):
        """Get runtime rotary state (button + last direction + step counter)."""
        resp = self.send_command(Command.GET_ROTARY_STATE)
        if resp and len(resp) >= 8 and resp[1] == Status.OK:
            step_counter = (
                int(resp[4])
                | (int(resp[5]) << 8)
                | (int(resp[6]) << 16)
                | (int(resp[7]) << 24)
            )
            last_direction = struct.unpack('<b', bytes([int(resp[3])]))[0]
            return {
                'button_pressed': bool(resp[2]),
                'last_direction': int(last_direction),
                'step_counter': int(step_counter),
            }
        return None
    
    # --- Calibration Commands ---
    
    def get_calibration(self):
        """Get calibration settings."""
        key_zeros = [0] * KEY_COUNT
        key_maxs = [0] * KEY_COUNT
        lut_zero = None
        next_index = 0

        while next_index < KEY_COUNT:
            resp = self.send_command(Command.GET_CALIBRATION, [0, next_index], timeout_ms=150)
            if not resp or len(resp) < 6 or resp[1] != Status.OK:
                return None

            start_index = int(resp[2])
            value_count = int(resp[3])
            if start_index != next_index or value_count <= 0 or value_count > CALIBRATION_VALUES_PER_CHUNK:
                return None

            current_lut_zero = struct.unpack('<h', bytes(resp[4:6]))[0]
            if lut_zero is None:
                lut_zero = current_lut_zero

            for i in range(value_count):
                base = 6 + i * 2
                key_zeros[start_index + i] = struct.unpack('<h', bytes(resp[base:base + 2]))[0]

            next_index += value_count

        next_index = 0
        while next_index < KEY_COUNT:
            resp = self.send_command(Command.GET_CALIBRATION_MAX, [0, next_index], timeout_ms=150)
            if not resp or len(resp) < 6 or resp[1] != Status.OK:
                return None

            start_index = int(resp[2])
            value_count = int(resp[3])
            if start_index != next_index or value_count <= 0 or value_count > CALIBRATION_VALUES_PER_CHUNK:
                return None

            for i in range(value_count):
                base = 6 + i * 2
                key_maxs[start_index + i] = struct.unpack('<h', bytes(resp[base:base + 2]))[0]

            next_index += value_count

        return {
            'lut_zero_value': lut_zero if lut_zero is not None else 0,
            'key_zero_values': key_zeros,
            'key_max_values': key_maxs,
        }
    
    def set_calibration(self, lut_zero, key_zeros, key_maxs=None):
        """Set calibration settings."""
        key_zeros = list(key_zeros or [])
        if len(key_zeros) < KEY_COUNT:
            key_zeros = key_zeros + [int(lut_zero)] * (KEY_COUNT - len(key_zeros))
        else:
            key_zeros = key_zeros[:KEY_COUNT]

        key_maxs = list(key_maxs or [])
        if len(key_maxs) < KEY_COUNT:
            default_max = 4095
            key_maxs = key_maxs + [default_max] * (KEY_COUNT - len(key_maxs))
        else:
            key_maxs = key_maxs[:KEY_COUNT]

        next_index = 0
        while next_index < KEY_COUNT:
            count = self._chunk_count(KEY_COUNT, next_index, CALIBRATION_VALUES_PER_CHUNK)
            data = [0, next_index, count]
            data.extend(struct.pack('<h', int(lut_zero)))
            for value in key_zeros[next_index:next_index + count]:
                data.extend(struct.pack('<h', int(value)))
            resp = self.send_command(Command.SET_CALIBRATION, data, timeout_ms=300)
            if not resp or len(resp) < 4 or resp[1] != Status.OK:
                return False
            next_index += count

        next_index = 0
        while next_index < KEY_COUNT:
            count = self._chunk_count(KEY_COUNT, next_index, CALIBRATION_VALUES_PER_CHUNK)
            data = [0, next_index, count]
            data.extend(struct.pack('<h', int(lut_zero)))
            for value in key_maxs[next_index:next_index + count]:
                data.extend(struct.pack('<h', int(value)))
            resp = self.send_command(Command.SET_CALIBRATION_MAX, data, timeout_ms=300)
            if not resp or len(resp) < 4 or resp[1] != Status.OK:
                return False
            next_index += count
        return True
    
    def auto_calibrate(self, key_index=0xFF):
        """Auto-calibrate a key or all keys (0xFF = all)."""
        data = [0, key_index]  # placeholder, key_index
        resp = self.send_command(Command.AUTO_CALIBRATE, data)
        if not resp or len(resp) < 2 or resp[1] != Status.OK:
            return None
        return self.get_calibration()

    def guided_calibration_start(self):
        resp = self.send_command(Command.GUIDED_CALIBRATION_START, timeout_ms=500)
        if not resp or len(resp) < 10 or resp[1] != Status.OK:
            return None
        return self.guided_calibration_status()

    def guided_calibration_status(self):
        resp = self.send_command(Command.GUIDED_CALIBRATION_STATUS, timeout_ms=150)
        if not resp or len(resp) < 10 or resp[1] != Status.OK:
            return None
        return {
            'active': bool(resp[2]),
            'phase': int(resp[3]),
            'current_key': int(resp[4]),
            'progress_percent': int(resp[5]),
            'sample_count': self._unpack_u16(resp, 6),
            'phase_elapsed_ms': self._unpack_u16(resp, 8),
        }

    def guided_calibration_abort(self):
        resp = self.send_command(Command.GUIDED_CALIBRATION_ABORT, timeout_ms=300)
        if not resp or len(resp) < 2 or resp[1] != Status.OK:
            return None
        return {
            'active': bool(resp[2]) if len(resp) >= 3 else False,
            'phase': int(resp[3]) if len(resp) >= 4 else 0,
            'current_key': int(resp[4]) if len(resp) >= 5 else 0,
            'progress_percent': int(resp[5]) if len(resp) >= 6 else 0,
        }
    
    # --- Per-Key Curve Commands ---
    
    def get_key_curve(self, key_index):
        """Get analog curve for a key."""
        data = [0, key_index]  # placeholder, key_index
        resp = self.send_command(Command.GET_KEY_CURVE, data)
        if resp and len(resp) >= 8 and resp[1] == Status.OK:
            return {
                'key_index': resp[2],
                'curve_enabled': resp[3] != 0,
                'p1_x': resp[4],
                'p1_y': resp[5],
                'p2_x': resp[6],
                'p2_y': resp[7]
            }
        return None
    
    def set_key_curve(self, key_index, curve_enabled, p1_x, p1_y, p2_x, p2_y):
        """Set analog curve for a key."""
        data = [0, key_index, 1 if curve_enabled else 0, p1_x, p1_y, p2_x, p2_y]
        resp = self.send_command(Command.SET_KEY_CURVE, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    # --- Per-Key Gamepad Mapping Commands ---
    
    def get_key_gamepad_map(self, key_index):
        """Get gamepad mapping for a key."""
        data = [0, key_index]  # placeholder, key_index
        resp = self.send_command(Command.GET_KEY_GAMEPAD_MAP, data)
        if resp and len(resp) >= 6 and resp[1] == Status.OK:
            return {
                'key_index': resp[2],
                'axis': resp[3],
                'direction': resp[4],
                'button': resp[5]
            }
        return None
    
    def set_key_gamepad_map(self, key_index, axis, direction, button):
        """Set gamepad mapping for a key."""
        if isinstance(axis, str):
            axis = GAMEPAD_AXES.get(axis, 0)
        if isinstance(direction, str):
            direction = GAMEPAD_DIRECTIONS.get(direction, 0)
        if isinstance(button, str):
            button = GAMEPAD_BUTTONS.get(button, 0)
        data = [0, key_index, axis, direction, button]
        resp = self.send_command(Command.SET_KEY_GAMEPAD_MAP, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    # --- LED Effect Commands ---
    
    def get_led_effect(self):
        """Get current LED effect mode."""
        resp = self.send_command(Command.GET_LED_EFFECT)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return resp[2]
        return None
    
    def set_led_effect(self, mode):
        """Set LED effect mode."""
        data = [0, mode]  # placeholder, mode
        resp = self.send_command(Command.SET_LED_EFFECT, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def get_led_effect_speed(self):
        """Get speed parameter for the active LED effect."""
        mode = self.get_led_effect()
        if mode is None:
            return None

        params = self.get_led_effect_params(mode)
        if params is None or len(params) <= LED_EFFECT_PARAM_SPEED:
            return None

        speed = int(params[LED_EFFECT_PARAM_SPEED])
        return speed if speed > 0 else 1
    
    def set_led_effect_speed(self, speed):
        """Set speed parameter for the active LED effect."""
        mode = self.get_led_effect()
        if mode is None:
            return False

        params = self.get_led_effect_params(mode)
        if params is None:
            return False

        while len(params) < LED_EFFECT_PARAM_COUNT:
            params.append(0)
        params[LED_EFFECT_PARAM_SPEED] = max(1, min(255, int(speed)))
        return bool(self.set_led_effect_params(mode, params))
    
    def set_led_effect_color(self, r, g, b):
        """Set color for the active LED effect."""
        mode = self.get_led_effect()
        if mode is None:
            return False

        params = self.get_led_effect_params(mode)
        if params is None:
            return False

        while len(params) < LED_EFFECT_PARAM_COUNT:
            params.append(0)
        params[LED_EFFECT_PARAM_COLOR_R] = max(0, min(255, int(r)))
        params[LED_EFFECT_PARAM_COLOR_G] = max(0, min(255, int(g)))
        params[LED_EFFECT_PARAM_COLOR_B] = max(0, min(255, int(b)))
        return bool(self.set_led_effect_params(mode, params))

    def get_led_effect_color(self):
        """Get color for the active LED effect."""
        mode = self.get_led_effect()
        if mode is None:
            return None

        params = self.get_led_effect_params(mode)
        if params is None or len(params) <= LED_EFFECT_PARAM_COLOR_B:
            return None

        return [
            int(params[LED_EFFECT_PARAM_COLOR_R]),
            int(params[LED_EFFECT_PARAM_COLOR_G]),
            int(params[LED_EFFECT_PARAM_COLOR_B]),
        ]

    def get_led_effect_params(self, effect_mode):
        """Get persisted tuning params for one LED effect."""
        data = [0, int(effect_mode) & 0xFF]
        resp = self.send_command(Command.GET_LED_EFFECT_PARAMS, data)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            count = min(int(resp[3]), LED_EFFECT_PARAM_COUNT, max(0, len(resp) - 4))
            return list(resp[4:4 + count])
        return None

    def get_led_effect_schema(self, effect_mode):
        """Get dynamic parameter schema for one LED effect (chunked HID read)."""
        mode = int(effect_mode) & 0xFF
        descriptors = []
        total_chunks = None
        total_active = None
        chunk_index = 0

        while True:
            data = [0, mode, chunk_index]
            resp = self.send_command(Command.GET_LED_EFFECT_SCHEMA, data)
            if not resp or len(resp) < 6 or resp[1] != Status.OK:
                return None

            payload = bytes(resp[2:])
            if len(payload) < 4:
                return None

            current_total_chunks = int(payload[2])
            current_total_active = int(payload[3])

            if current_total_chunks <= 0:
                return None

            expected_count = max(
                0,
                min(
                    SCHEMA_PARAMS_PER_CHUNK,
                    current_total_active - (chunk_index * SCHEMA_PARAMS_PER_CHUNK),
                ),
            )
            payload_len = 4 + (expected_count * SCHEMA_DESCRIPTOR_BYTES)
            if len(payload) < payload_len:
                return None

            try:
                parsed = parse_schema_chunk(payload[:payload_len])
            except Exception:
                return None

            if int(parsed.get("effect_id", -1)) != mode:
                return None
            if int(parsed.get("chunk_index", -1)) != chunk_index:
                return None

            if total_chunks is None:
                total_chunks = current_total_chunks
                total_active = current_total_active
            elif (
                total_chunks != current_total_chunks
                or total_active != current_total_active
            ):
                return None

            for desc in parsed.get("descriptors", []):
                descriptors.append(
                    {
                        "id": int(desc.get("id", 0)),
                        "type": int(desc.get("type", 0)),
                        "min": int(desc.get("min", 0)),
                        "max": int(desc.get("max", 0)),
                        "default": int(desc.get("default", 0)),
                        "step": int(desc.get("step", 0)),
                    }
                )

            chunk_index += 1
            if chunk_index >= total_chunks:
                break

        descriptors.sort(key=lambda d: d["id"])
        if total_active is not None and total_active >= 0:
            descriptors = descriptors[:total_active]

        return {
            "effect_id": mode,
            "total_chunks": int(total_chunks if total_chunks is not None else 0),
            "total_active": int(
                total_active if total_active is not None else len(descriptors)
            ),
            "descriptors": descriptors,
        }

    def set_led_effect_params(self, effect_mode, params):
        """Set persisted tuning params for one LED effect."""
        values = [int(v) & 0xFF for v in list(params)[:LED_EFFECT_PARAM_COUNT]]
        while len(values) < LED_EFFECT_PARAM_COUNT:
            values.append(0)
        data = [0, int(effect_mode) & 0xFF, *values]
        resp = self.send_command(Command.SET_LED_EFFECT_PARAMS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def led_set_audio_spectrum(self, bands, impact_level=0):
        """Push host-side audio spectrum levels for audio-reactive effects."""
        values = [max(0, min(255, int(v))) for v in list(bands or [])[:LED_AUDIO_SPECTRUM_BAND_COUNT]]
        if not values:
            return False
        payload = [0, len(values), *values, max(0, min(255, int(impact_level)))]
        resp = self.send_command(Command.SET_LED_AUDIO_SPECTRUM, payload)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def led_clear_audio_spectrum(self):
        """Clear host-side audio spectrum data on the firmware."""
        resp = self.send_command(Command.CLEAR_LED_AUDIO_SPECTRUM)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def get_led_fps_limit(self):
        """Get LED FPS limit (0 = unlimited)."""
        resp = self.send_command(Command.GET_LED_FPS_LIMIT)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return resp[2]
        return None
    
    def set_led_fps_limit(self, fps):
        """Set LED FPS limit (0 = unlimited, 1-255 = limit)."""
        data = [0, fps]  # placeholder, fps
        resp = self.send_command(Command.SET_LED_FPS_LIMIT, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    # --- ADC Filter Commands ---
    
    def get_filter_enabled(self):
        """Get ADC filter enabled state."""
        resp = self.send_command(Command.GET_FILTER_ENABLED)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return resp[2] != 0
        return None
    
    def set_filter_enabled(self, enabled):
        """Set ADC filter enabled state."""
        data = [0, 1 if enabled else 0]
        resp = self.send_command(Command.SET_FILTER_ENABLED, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def get_filter_params(self):
        """Get ADC filter parameters.
        Returns dict with noise_band, alpha_min_denom, alpha_max_denom."""
        resp = self.send_command(Command.GET_FILTER_PARAMS)
        if resp and len(resp) >= 5 and resp[1] == Status.OK:
            return {
                'noise_band': resp[2],
                'alpha_min_denom': resp[3],
                'alpha_max_denom': resp[4]
            }
        return None
    
    def set_filter_params(self, noise_band, alpha_min_denom, alpha_max_denom):
        """Set ADC filter parameters.
        noise_band: Noise band in ADC counts (default 30)
        alpha_min_denom: Alpha min denominator 1/N (default 32)
        alpha_max_denom: Alpha max denominator 1/N (default 4)
        """
        data = [0, noise_band, alpha_min_denom, alpha_max_denom]
        resp = self.send_command(Command.SET_FILTER_PARAMS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    # --- Debug Commands ---
    
    def get_adc_values(self):
        """Get ADC values for all keys (debug) with timing info."""
        resp = self.send_command(Command.GET_ADC_VALUES)
        if resp and len(resp) >= 30 and resp[1] == Status.OK:
            raw_values = []
            filtered_values = []

            for i in range(6):
                raw_idx = 2 + i * 2
                filt_idx = 14 + i * 2
                raw_values.append(resp[raw_idx] | (resp[raw_idx + 1] << 8))
                filtered_values.append(resp[filt_idx] | (resp[filt_idx + 1] << 8))

            scan_time_us = resp[26] | (resp[27] << 8)
            scan_rate_hz = resp[28] | (resp[29] << 8)

            task_times_us = None
            if len(resp) >= 46:
                task_times_us = {
                    'analog': resp[30] | (resp[31] << 8),
                    'trigger': resp[32] | (resp[33] << 8),
                    'socd': resp[34] | (resp[35] << 8),
                    'keyboard': resp[36] | (resp[37] << 8),
                    'keyboard_nkro': resp[38] | (resp[39] << 8),
                    'gamepad': resp[40] | (resp[41] << 8),
                    'led': resp[42] | (resp[43] << 8),
                    'total': resp[44] | (resp[45] << 8),
                }

            analog_monitor_us = None
            if len(resp) >= 64:
                analog_monitor_us = {
                    'raw': resp[46] | (resp[47] << 8),
                    'filter': resp[48] | (resp[49] << 8),
                    'calibration': resp[50] | (resp[51] << 8),
                    'lut': resp[52] | (resp[53] << 8),
                    'store': resp[54] | (resp[55] << 8),
                    'key_min': resp[56] | (resp[57] << 8),
                    'key_max': resp[58] | (resp[59] << 8),
                    'key_avg': resp[60] | (resp[61] << 8),
                    'nonzero_keys': resp[62] | (resp[63] << 8),
                }

            return {
                'adc': raw_values,
                'adc_raw': raw_values,
                'adc_filtered': filtered_values,
                'scan_time_us': scan_time_us,
                'scan_rate_hz': scan_rate_hz,
                'task_times_us': task_times_us,
                'analog_monitor_us': analog_monitor_us,
                'adc_payload_format': 'extended'
            }
        return None

    def get_mcu_metrics(self):
        """Get MCU-side telemetry such as internal temperature, clock, and scan load."""
        resp = self.send_command(Command.GET_MCU_METRICS, timeout_ms=120)
        if not resp or len(resp) < 19 or resp[1] != Status.OK:
            return None

        temperature_raw = struct.unpack_from("<h", bytes(resp), 2)[0]
        vref_mv = self._unpack_u16(resp, 4)
        core_clock_hz = struct.unpack_from("<I", bytes(resp), 6)[0]
        scan_cycle_us = self._unpack_u16(resp, 10)
        scan_rate_hz = self._unpack_u16(resp, 12)
        work_us = self._unpack_u16(resp, 14)
        load_permille = self._unpack_u16(resp, 16)
        temp_valid = bool(resp[18])

        return {
            "temperature_c": int(temperature_raw) if temp_valid else None,
            "temperature_valid": temp_valid,
            "vref_mv": int(vref_mv),
            "core_clock_hz": int(core_clock_hz),
            "scan_cycle_us": int(scan_cycle_us),
            "scan_rate_hz": int(scan_rate_hz),
            "work_us": int(work_us),
            "load_percent": float(load_permille) / 10.0,
            "load_permille": int(load_permille),
        }

    def get_raw_adc_chunk(self, start_index: int):
        """Fetch one raw ADC chunk from the firmware."""
        data = [0, max(0, min(255, int(start_index)))]
        resp = self.send_command(Command.GET_RAW_ADC_CHUNK, data, timeout_ms=150)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            returned_start = resp[2]
            value_count = min(resp[3], (len(resp) - 4) // 2)
            values = []
            for i in range(value_count):
                base = 4 + i * 2
                values.append(resp[base] | (resp[base + 1] << 8))
            return {
                "start_index": returned_start,
                "values": values,
            }
        return None

    def get_filtered_adc_chunk(self, start_index: int):
        """Fetch one EMA-filtered ADC chunk from the firmware."""
        data = [0, max(0, min(255, int(start_index)))]
        resp = self.send_command(Command.GET_FILTERED_ADC_CHUNK, data, timeout_ms=150)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            returned_start = resp[2]
            value_count = min(resp[3], (len(resp) - 4) // 2)
            values = []
            for i in range(value_count):
                base = 4 + i * 2
                values.append(resp[base] | (resp[base + 1] << 8))
            return {
                "start_index": returned_start,
                "values": values,
            }
        return None

    def get_calibrated_adc_chunk(self, start_index: int):
        """Fetch one calibrated ADC chunk from the firmware."""
        data = [0, max(0, min(255, int(start_index)))]
        resp = self.send_command(Command.GET_CALIBRATED_ADC_CHUNK, data, timeout_ms=150)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            returned_start = resp[2]
            value_count = min(resp[3], (len(resp) - 4) // 2)
            values = []
            for i in range(value_count):
                base = 4 + i * 2
                values.append(resp[base] | (resp[base + 1] << 8))
            return {
                "start_index": returned_start,
                "values": values,
            }
        return None

    def get_all_raw_adc_values(self, key_count: int = KEY_COUNT):
        """Fetch raw ADC values for the whole keyboard in multiple HID chunks."""
        values = [0] * key_count
        next_index = 0

        while next_index < key_count:
            chunk = self.get_raw_adc_chunk(next_index)
            if not chunk:
                return None

            start_index = int(chunk.get("start_index", next_index))
            chunk_values = list(chunk.get("values", []))
            if start_index >= key_count or not chunk_values:
                return None

            for offset, value in enumerate(chunk_values):
                dst = start_index + offset
                if dst >= key_count:
                    break
                values[dst] = int(value)

            advanced_to = start_index + len(chunk_values)
            if advanced_to <= next_index:
                return None
            next_index = advanced_to

        return values

    def get_all_filtered_adc_values(self, key_count: int = KEY_COUNT):
        """Fetch EMA-filtered ADC values for the whole keyboard in multiple HID chunks."""
        values = [0] * key_count
        next_index = 0

        while next_index < key_count:
            chunk = self.get_filtered_adc_chunk(next_index)
            if not chunk:
                return None

            start_index = int(chunk.get("start_index", next_index))
            chunk_values = list(chunk.get("values", []))
            if start_index >= key_count or not chunk_values:
                return None

            for offset, value in enumerate(chunk_values):
                dst = start_index + offset
                if dst >= key_count:
                    break
                values[dst] = int(value)

            advanced_to = start_index + len(chunk_values)
            if advanced_to <= next_index:
                return None
            next_index = advanced_to

        return values

    def get_all_calibrated_adc_values(self, key_count: int = KEY_COUNT):
        """Fetch calibrated ADC values for the whole keyboard in multiple HID chunks."""
        values = [0] * key_count
        next_index = 0

        while next_index < key_count:
            chunk = self.get_calibrated_adc_chunk(next_index)
            if not chunk:
                return None

            start_index = int(chunk.get("start_index", next_index))
            chunk_values = list(chunk.get("values", []))
            if start_index >= key_count or not chunk_values:
                return None

            for offset, value in enumerate(chunk_values):
                dst = start_index + offset
                if dst >= key_count:
                    break
                values[dst] = int(value)

            advanced_to = start_index + len(chunk_values)
            if advanced_to <= next_index:
                return None
            next_index = advanced_to

        return values
    
    def get_key_states(self):
        """Get key states (debug) with actual distances in mm."""
        states = [0] * KEY_COUNT
        distances_norm = [0] * KEY_COUNT
        distances_01mm = [0] * KEY_COUNT
        distances_mm = [0.0] * KEY_COUNT
        next_index = 0

        while next_index < KEY_COUNT:
            resp = self.send_command(Command.GET_KEY_STATES, [0, next_index], timeout_ms=150)
            if not resp or len(resp) < 4 or resp[1] != Status.OK:
                return None

            start_index = int(resp[2])
            key_count = int(resp[3])
            if start_index != next_index or key_count <= 0 or key_count > KEY_STATES_PER_CHUNK:
                return None

            for i in range(key_count):
                offset = 4 + i * 4
                key_index = start_index + i
                states[key_index] = int(resp[offset])
                distances_norm[key_index] = int(resp[offset + 1])
                value_01mm = self._unpack_u16(resp, offset + 2)
                distances_01mm[key_index] = value_01mm
                distances_mm[key_index] = value_01mm / 100.0

            next_index += key_count

        return {
            'states': states,
            'distances': distances_norm,
            'distances_01mm': distances_01mm,
            'distances_mm': distances_mm,
        }

    def adc_capture_start(self, key_index, duration_ms):
        """Start ADC capture in MCU RAM for one key and a fixed duration."""
        duration_ms = max(1, int(duration_ms))
        data = [
            0,
            int(key_index) & 0xFF,
            0,
            duration_ms & 0xFF,
            (duration_ms >> 8) & 0xFF,
            (duration_ms >> 16) & 0xFF,
            (duration_ms >> 24) & 0xFF,
        ]
        resp = self.send_command(Command.ADC_CAPTURE_START, data)
        if resp and len(resp) >= 16 and resp[1] == Status.OK:
            return {
                'active': bool(resp[2]),
                'key_index': resp[3],
                'duration_ms': resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24),
                'sample_count': resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24),
                'overflow_count': resp[12] | (resp[13] << 8) | (resp[14] << 16) | (resp[15] << 24),
            }
        return None

    def adc_capture_status(self):
        """Get current ADC capture status from MCU RAM."""
        resp = self.send_command(Command.ADC_CAPTURE_STATUS)
        if resp and len(resp) >= 16 and resp[1] == Status.OK:
            return {
                'active': bool(resp[2]),
                'key_index': resp[3],
                'duration_ms': resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24),
                'sample_count': resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24),
                'overflow_count': resp[12] | (resp[13] << 8) | (resp[14] << 16) | (resp[15] << 24),
            }
        return None

    def adc_capture_read(self, start_index, max_samples=12):
        """Read a chunk of captured ADC samples from MCU RAM."""
        if max_samples < 1:
            max_samples = 1
        if max_samples > 12:
            max_samples = 12

        start_index = int(start_index)
        data = [
            0,
            start_index & 0xFF,
            (start_index >> 8) & 0xFF,
            (start_index >> 16) & 0xFF,
            (start_index >> 24) & 0xFF,
            int(max_samples) & 0xFF,
        ]

        resp = self.send_command(Command.ADC_CAPTURE_READ, data)
        if not resp or len(resp) < 13 or resp[1] != Status.OK:
            return None

        total_samples = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24)
        returned_start = resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24)
        count = resp[12]

        raw_samples = []
        filtered_samples = []

        raw_base = 13
        filtered_base = raw_base + 24
        count = min(count, 12)

        for i in range(count):
            raw_idx = raw_base + i * 2
            filtered_idx = filtered_base + i * 2
            raw_val = resp[raw_idx] | (resp[raw_idx + 1] << 8)
            filtered_val = resp[filtered_idx] | (resp[filtered_idx + 1] << 8)
            raw_samples.append(raw_val)
            filtered_samples.append(filtered_val)

        return {
            'active': bool(resp[2]),
            'key_index': resp[3],
            'total_samples': total_samples,
            'start_index': returned_start,
            'sample_count': count,
            'raw_samples': raw_samples,
            'filtered_samples': filtered_samples,
        }

    def get_lock_states(self):
        """Get keyboard lock states (Caps, Num, Scroll)."""
        resp = self.send_command(Command.GET_LOCK_STATES)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            lock_byte = resp[2]
            return {
                'num_lock': bool(lock_byte & 0x01),
                'caps_lock': bool(lock_byte & 0x02),
                'scroll_lock': bool(lock_byte & 0x04)
            }
        return None
