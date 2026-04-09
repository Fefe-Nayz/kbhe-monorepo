import struct
import threading
import time

import hid

from .protocol import (
    CALIBRATION_VALUES_PER_CHUNK,
    Command,
    GAMEPAD_AXES,
    GAMEPAD_BUTTONS,
    GAMEPAD_DIRECTIONS,
    KEY_COUNT,
    KEY_SETTINGS_PER_CHUNK,
    KEY_STATES_PER_CHUNK,
    LED_BYTES_PER_CHUNK,
    LED_EFFECT_PARAM_COUNT,
    PACKET_SIZE,
    PID,
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
    
    def get_firmware_version(self):
        """Get firmware version."""
        resp = self.send_command(Command.GET_FIRMWARE_VERSION)
        if resp and len(resp) >= 4:
            version = resp[2] | (resp[3] << 8)
            major = (version >> 8) & 0xFF
            minor = version & 0xFF
            return f"{major}.{minor}"
        return None
    
    def get_options(self):
        """Get all options."""
        resp = self.send_command(Command.GET_OPTIONS)
        if resp and len(resp) >= 5 and resp[1] == Status.OK:
            return {
                'keyboard_enabled': bool(resp[2]),
                'gamepad_enabled': bool(resp[3]),
                'raw_hid_echo': bool(resp[4])
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
        """Get NKRO mode enabled state."""
        resp = self.send_command(Command.GET_NKRO_ENABLED)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return bool(resp[2])
        return None
    
    def set_nkro_enabled(self, enabled):
        """Set NKRO mode enabled state."""
        data = [0, 1 if enabled else 0]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_NKRO_ENABLED, data, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def save_settings(self):
        """Save settings to flash. Uses longer timeout for flash erase operation."""
        # Flash erase for 128KB sector can take up to 2 seconds on STM32F7
        resp = self.send_command(Command.SAVE_SETTINGS, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def factory_reset(self):
        """Reset to factory defaults."""
        # Factory reset also erases flash, needs longer timeout
        resp = self.send_command(Command.FACTORY_RESET, timeout_ms=3000)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
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
    
    def get_key_settings(self, key_index):
        """Get settings for a specific key (extended format)."""
        data = [0, key_index]  # 0 = placeholder for status
        resp = self.send_command(Command.GET_KEY_SETTINGS, data)
        if resp and len(resp) >= 13 and resp[1] == Status.OK:
            return {
                'key_index': resp[2],
                'hid_keycode': self._unpack_u16(resp, 3),
                'actuation_point_mm': resp[5] / 10.0,  # 0.1mm to mm
                'release_point_mm': resp[6] / 10.0,
                'rapid_trigger_activation': resp[7] / 10.0,  # 0.1mm
                'rapid_trigger_press': resp[8] / 100.0,  # 0.01mm to mm
                'rapid_trigger_release': resp[9] / 100.0,
                'socd_pair': resp[10] if resp[10] != 255 else None,
                'rapid_trigger_enabled': bool(resp[11]),
                'disable_kb_on_gamepad': bool(resp[12])
            }
        return None
    
    def set_key_settings(self, key_index, hid_keycode, actuation_mm, release_mm, rapid_trigger_mm, socd_pair=None):
        """Set settings for a specific key (legacy format for backwards compatibility)."""
        # Use extended format with defaults
        settings = {
            'hid_keycode': hid_keycode,
            'actuation_point_mm': actuation_mm,
            'release_point_mm': release_mm,
            'rapid_trigger_enabled': False,
            'rapid_trigger_activation': 0.5,
            'rapid_trigger_press': rapid_trigger_mm,
            'rapid_trigger_release': rapid_trigger_mm,
            'socd_pair': socd_pair if socd_pair is not None else 255,
            'disable_kb_on_gamepad': False
        }
        return self.set_key_settings_extended(key_index, settings)
    
    def set_key_settings_extended(self, key_index, settings):
        """Set settings for a specific key (extended format)."""
        data = [
            0,  # placeholder for status
            key_index,
            settings.get('hid_keycode', 0x14) & 0xFF,
            (settings.get('hid_keycode', 0x14) >> 8) & 0xFF,
            int(settings.get('actuation_point_mm', 2.0) * 10),  # mm to 0.1mm
            int(settings.get('release_point_mm', 1.8) * 10),
            int(settings.get('rapid_trigger_activation', 0.5) * 10),  # mm to 0.1mm
            int(settings.get('rapid_trigger_press', 0.3) * 100),  # mm to 0.01mm
            int(settings.get('rapid_trigger_release', 0.3) * 100),
            settings.get('socd_pair', 255),
            1 if settings.get('rapid_trigger_enabled', False) else 0,
            1 if settings.get('disable_kb_on_gamepad', False) else 0
        ]
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
                offset = 4 + i * 9
                hid_keycode = self._unpack_u16(resp, offset)
                flags = resp[offset + 8]
                keys.append({
                    'hid_keycode': hid_keycode,
                    'actuation_point_mm': resp[offset + 2] / 10.0,
                    'release_point_mm': resp[offset + 3] / 10.0,
                    'rapid_trigger_activation': resp[offset + 4] / 10.0,
                    'rapid_trigger_press': resp[offset + 5] / 100.0,
                    'rapid_trigger_release': resp[offset + 6] / 100.0,
                    'socd_pair': resp[offset + 7] if resp[offset + 7] != 255 else None,
                    'rapid_trigger_enabled': bool(flags & 0x01),
                    'disable_kb_on_gamepad': bool(flags & 0x02),
                })

            next_index += key_count

        return keys
    
    # --- Gamepad Settings Commands ---
    
    def get_gamepad_settings(self):
        """Get gamepad settings."""
        resp = self.send_command(Command.GET_GAMEPAD_SETTINGS)
        if resp and len(resp) >= 6 and resp[1] == Status.OK:
            return {
                'deadzone': resp[2],
                'curve_type': resp[3],  # 0=linear, 1=smooth, 2=aggressive
                'square_mode': bool(resp[4]),
                'snappy_mode': bool(resp[5])
            }
        return None
    
    def set_gamepad_settings(self, deadzone, curve_type, square_mode, snappy_mode):
        """Set gamepad settings."""
        data = [
            0,  # placeholder for status
            deadzone,
            curve_type,
            1 if square_mode else 0,
            1 if snappy_mode else 0
        ]
        resp = self.send_command(Command.SET_GAMEPAD_SETTINGS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def get_rotary_encoder_settings(self):
        """Get rotary encoder settings."""
        resp = self.send_command(Command.GET_ROTARY_ENCODER_SETTINGS)
        if resp and len(resp) >= 9 and resp[1] == Status.OK:
            return {
                'rotation_action': int(resp[2]),
                'button_action': int(resp[3]),
                'sensitivity': int(resp[4]),
                'invert_direction': bool(resp[5]),
                'rgb_behavior': int(resp[6]),
                'rgb_effect_mode': int(resp[7]),
                'rgb_step': int(resp[8]),
            }
        return None

    def set_rotary_encoder_settings(self, settings):
        """Set rotary encoder settings."""
        data = [
            0,
            int(settings.get('rotation_action', 0)) & 0xFF,
            int(settings.get('button_action', 0)) & 0xFF,
            int(settings.get('sensitivity', 1)) & 0xFF,
            1 if settings.get('invert_direction', False) else 0,
            int(settings.get('rgb_behavior', 0)) & 0xFF,
            int(settings.get('rgb_effect_mode', 4)) & 0xFF,
            int(settings.get('rgb_step', 8)) & 0xFF,
        ]
        resp = self.send_command(Command.SET_ROTARY_ENCODER_SETTINGS, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
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
    
    # --- Gamepad + Keyboard Mode ---
    
    def get_gamepad_with_keyboard(self):
        """Get gamepad+keyboard mode."""
        resp = self.send_command(Command.GET_GAMEPAD_WITH_KB)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return resp[2] != 0
        return None
    
    def set_gamepad_with_keyboard(self, enabled):
        """Set gamepad+keyboard mode."""
        data = [0, 1 if enabled else 0]
        resp = self.send_command(Command.SET_GAMEPAD_WITH_KB, data)
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
        """Get LED effect speed."""
        resp = self.send_command(Command.GET_LED_EFFECT_SPEED)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return resp[2]
        return None
    
    def set_led_effect_speed(self, speed):
        """Set LED effect speed."""
        data = [0, speed]  # placeholder, speed
        resp = self.send_command(Command.SET_LED_EFFECT_SPEED, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def set_led_effect_color(self, r, g, b):
        """Set LED effect color."""
        data = [0, r, g, b]  # placeholder, r, g, b
        resp = self.send_command(Command.SET_LED_EFFECT_COLOR, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK

    def get_led_effect_params(self, effect_mode):
        """Get persisted tuning params for one LED effect."""
        data = [0, int(effect_mode) & 0xFF]
        resp = self.send_command(Command.GET_LED_EFFECT_PARAMS, data)
        if resp and len(resp) >= 4 and resp[1] == Status.OK:
            count = min(int(resp[3]), LED_EFFECT_PARAM_COUNT, max(0, len(resp) - 4))
            return list(resp[4:4 + count])
        return None

    def set_led_effect_params(self, effect_mode, params):
        """Set persisted tuning params for one LED effect."""
        values = [int(v) & 0xFF for v in list(params)[:LED_EFFECT_PARAM_COUNT]]
        while len(values) < LED_EFFECT_PARAM_COUNT:
            values.append(0)
        data = [0, int(effect_mode) & 0xFF, *values]
        resp = self.send_command(Command.SET_LED_EFFECT_PARAMS, data)
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
    
    def get_led_diagnostic(self):
        """Get LED diagnostic mode (0=normal, 1=DMA stress, 2=CPU stress)."""
        resp = self.send_command(Command.GET_LED_DIAGNOSTIC)
        if resp and len(resp) >= 3 and resp[1] == Status.OK:
            return resp[2]
        return None
    
    def set_led_diagnostic(self, mode):
        """Set LED diagnostic mode.
        0 = Normal operation
        1 = DMA stress only (sends data without computing effects - tests if DMA causes ADC noise)
        2 = CPU stress only (computes effects but doesn't send to LEDs - tests if CPU load causes ADC issues)
        """
        data = [0, mode]
        resp = self.send_command(Command.SET_LED_DIAGNOSTIC, data)
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
        """Get ADC values for all keys (debug) with timing info.

        Supports both payload formats:
        - New: raw[6] + filtered[6] + timing
        - Legacy: adc[6] + timing
        """
        resp = self.send_command(Command.GET_ADC_VALUES)
        if resp and len(resp) >= 18 and resp[1] == Status.OK:
            # Legacy format candidate: adc (bytes 2..13), timing (14..17)
            legacy_values = []
            for i in range(6):
                legacy_values.append(resp[2 + i * 2] | (resp[3 + i * 2] << 8))

            legacy_scan_time_us = resp[14] | (resp[15] << 8)
            legacy_scan_rate_hz = resp[16] | (resp[17] << 8)

            # New format candidate: raw (2..13), filtered (14..25), timing (26..29)
            if len(resp) >= 30:
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

                # If timing fields are zero here but valid in legacy position,
                # we are most likely talking to legacy firmware.
                if (
                    scan_time_us == 0
                    and scan_rate_hz == 0
                    and (legacy_scan_time_us != 0 or legacy_scan_rate_hz != 0)
                ):
                    return {
                        'adc': legacy_values,
                        'adc_raw': legacy_values,
                        'adc_filtered': legacy_values,
                        'scan_time_us': legacy_scan_time_us,
                        'scan_rate_hz': legacy_scan_rate_hz,
                        'task_times_us': None,
                        'analog_monitor_us': None,
                        'adc_payload_format': 'legacy'
                    }

                return {
                    # Keep legacy key for existing UI code paths.
                    'adc': raw_values,
                    'adc_raw': raw_values,
                    'adc_filtered': filtered_values,
                    'scan_time_us': scan_time_us,
                    'scan_rate_hz': scan_rate_hz,
                    'task_times_us': task_times_us,
                    'analog_monitor_us': analog_monitor_us,
                    'adc_payload_format': 'extended'
                }

            return {
                'adc': legacy_values,
                'adc_raw': legacy_values,
                'adc_filtered': legacy_values,
                'scan_time_us': legacy_scan_time_us,
                'scan_rate_hz': legacy_scan_rate_hz,
                'task_times_us': None,
                'analog_monitor_us': None,
                'adc_payload_format': 'legacy'
            }
        return None

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
