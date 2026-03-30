import struct
import threading
import time

import hid

from .protocol import (
    Command,
    GAMEPAD_AXES,
    GAMEPAD_BUTTONS,
    GAMEPAD_DIRECTIONS,
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
    
    def led_upload_all(self, pixels):
        """Upload all 64 LED pixels (192 bytes) in chunks."""
        # Split into 4 chunks of 48 bytes each
        for chunk_idx in range(4):
            offset = chunk_idx * 48
            chunk_size = min(48, len(pixels) - offset)
            if chunk_size <= 0:
                break
            
            # Packet format: [cmd] [status_placeholder] [chunk_index] [chunk_size] [data...]
            data = [0, chunk_idx, chunk_size] + list(pixels[offset:offset+chunk_size])
            resp = self.send_command(Command.SET_LED_ALL_CHUNK, data, timeout_ms=150)
            if not resp:
                print(f"  ❌ No response for chunk {chunk_idx}")
                return False
            if len(resp) < 2:
                print(f"  ❌ Short response for chunk {chunk_idx}: {list(resp)}")
                return False
            if resp[1] != Status.OK:
                print(f"  ❌ Error response for chunk {chunk_idx}: status={resp[1]}")
                return False
            time.sleep(0.02)  # Small delay between chunks
        return True
    
    def led_download_all(self):
        """Download all 64 LED pixels (192 bytes) from device."""
        pixels = []
        for row in range(8):
            row_data = self.led_get_row(row)
            if row_data:
                pixels.extend(row_data)
            else:
                pixels.extend([0] * 24)
        return pixels
    
    # --- Key Settings Commands ---
    
    def get_key_settings(self, key_index):
        """Get settings for a specific key (extended format)."""
        data = [0, key_index]  # 0 = placeholder for status
        resp = self.send_command(Command.GET_KEY_SETTINGS, data)
        if resp and len(resp) >= 12 and resp[1] == Status.OK:
            return {
                'key_index': resp[2],
                'hid_keycode': resp[3],
                'actuation_point_mm': resp[4] / 10.0,  # 0.1mm to mm
                'release_point_mm': resp[5] / 10.0,
                'rapid_trigger_activation': resp[6] / 10.0,  # 0.1mm
                'rapid_trigger_press': resp[7] / 100.0,  # 0.01mm to mm
                'rapid_trigger_release': resp[8] / 100.0,
                'socd_pair': resp[9] if resp[9] != 255 else None,
                'rapid_trigger_enabled': bool(resp[10]),
                'disable_kb_on_gamepad': bool(resp[11])
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
            settings.get('hid_keycode', 0x14),
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
        """Get settings for all 6 keys (extended format)."""
        resp = self.send_command(Command.GET_ALL_KEY_SETTINGS)
        if resp and len(resp) >= 56 and resp[1] == Status.OK:  # 2 header + 6*9 = 56 min
            keys = []
            for i in range(6):
                offset = 2 + i * 9  # Each key: 9 bytes
                keys.append({
                    'hid_keycode': resp[offset],
                    'actuation_point_mm': resp[offset + 1] / 10.0,
                    'release_point_mm': resp[offset + 2] / 10.0,
                    'rapid_trigger_activation': resp[offset + 3] / 10.0,
                    'rapid_trigger_press': resp[offset + 4] / 100.0,
                    'rapid_trigger_release': resp[offset + 5] / 100.0,
                    'socd_pair': resp[offset + 6] if resp[offset + 6] != 255 else None,
                    'rapid_trigger_enabled': bool(resp[offset + 7]),
                    'disable_kb_on_gamepad': bool(resp[offset + 8])
                })
            return keys
        return None
    
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
    
    # --- Calibration Commands ---
    
    def get_calibration(self):
        """Get calibration settings."""
        resp = self.send_command(Command.GET_CALIBRATION)
        if resp and len(resp) >= 16 and resp[1] == Status.OK:
            lut_zero = struct.unpack('<h', bytes(resp[2:4]))[0]  # int16
            key_zeros = []
            for i in range(6):
                val = struct.unpack('<h', bytes(resp[4 + i*2:6 + i*2]))[0]
                key_zeros.append(val)
            return {
                'lut_zero_value': lut_zero,
                'key_zero_values': key_zeros
            }
        return None
    
    def set_calibration(self, lut_zero, key_zeros):
        """Set calibration settings."""
        data = [0]  # placeholder
        data.extend(struct.pack('<h', lut_zero))  # int16
        for val in key_zeros[:6]:
            data.extend(struct.pack('<h', val))
        # Pad to expected size
        while len(data) < 16:
            data.append(0)
        resp = self.send_command(Command.SET_CALIBRATION, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def auto_calibrate(self, key_index=0xFF):
        """Auto-calibrate a key or all keys (0xFF = all)."""
        data = [0, key_index]  # placeholder, key_index
        resp = self.send_command(Command.AUTO_CALIBRATE, data)
        if resp and len(resp) >= 16 and resp[1] == Status.OK:
            # Return updated calibration
            lut_zero = struct.unpack('<h', bytes(resp[2:4]))[0]
            key_zeros = []
            for i in range(6):
                val = struct.unpack('<h', bytes(resp[4 + i*2:6 + i*2]))[0]
                key_zeros.append(val)
            return {
                'lut_zero_value': lut_zero,
                'key_zero_values': key_zeros
            }
        return None
    
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
        """Get ADC values for all keys (debug) with timing info."""
        resp = self.send_command(Command.GET_ADC_VALUES)
        if resp and len(resp) >= 18 and resp[1] == Status.OK:
            values = []
            for i in range(6):
                val = resp[2 + i*2] | (resp[3 + i*2] << 8)
                values.append(val)
            # Extract timing info (after 6 x uint16 ADC values = 14 bytes)
            scan_time_us = resp[14] | (resp[15] << 8)
            scan_rate_hz = resp[16] | (resp[17] << 8)
            return {
                'adc': values,
                'scan_time_us': scan_time_us,
                'scan_rate_hz': scan_rate_hz
            }
        return None
    
    def get_key_states(self):
        """Get key states (debug) with actual distances in mm."""
        resp = self.send_command(Command.GET_KEY_STATES)
        if resp and len(resp) >= 26 and resp[1] == Status.OK:
            states = list(resp[2:8])
            distances_norm = list(resp[8:14])  # Normalized 0-255
            # Extract distances in 0.01mm units (6 x uint16)
            distances_01mm = []
            distances_mm = []
            for i in range(6):
                val = resp[14 + i*2] | (resp[15 + i*2] << 8)
                distances_01mm.append(val)
                distances_mm.append(val / 100.0)  # Convert to mm float
            return {
                'states': states, 
                'distances': distances_norm,  # For progress bars
                'distances_01mm': distances_01mm,  # Raw units for graphing
                'distances_mm': distances_mm   # Actual mm values
            }
        return None

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
