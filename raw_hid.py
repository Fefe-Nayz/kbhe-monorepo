#!/usr/bin/env python3
"""
KBHE Raw HID Configuration Tool
Communicates with the keyboard via Raw HID to configure settings.
Includes LED Matrix editor with 8x8 grid UI.
"""

import hid
import time
import sys
import struct
import threading
import queue
from enum import IntEnum

try:
    import tkinter as tk
    from tkinter import ttk, colorchooser, filedialog, messagebox
    HAS_GUI = True
except ImportError:
    HAS_GUI = False
    print("Warning: tkinter not available, GUI features disabled")

# --- Configuration ---
VID = 0x9172
PID = 0x0002
PACKET_SIZE = 64

# LED Matrix constants
LED_MATRIX_SIZE = 64  # 8x8
LED_MATRIX_WIDTH = 8
LED_MATRIX_HEIGHT = 8

# --- Command IDs ---
class Command(IntEnum):
    # System commands
    GET_FIRMWARE_VERSION = 0x00
    REBOOT = 0x01
    ENTER_BOOTLOADER = 0x02
    FACTORY_RESET = 0x03
    
    # Settings commands
    GET_OPTIONS = 0x20
    SET_OPTIONS = 0x21
    GET_KEYBOARD_ENABLED = 0x22
    SET_KEYBOARD_ENABLED = 0x23
    GET_GAMEPAD_ENABLED = 0x24
    SET_GAMEPAD_ENABLED = 0x25
    SAVE_SETTINGS = 0x26
    GET_NKRO_ENABLED = 0x27
    SET_NKRO_ENABLED = 0x28
    
    # Key settings commands
    GET_KEY_SETTINGS = 0x40
    SET_KEY_SETTINGS = 0x41
    GET_ALL_KEY_SETTINGS = 0x42
    SET_ALL_KEY_SETTINGS = 0x43
    GET_GAMEPAD_SETTINGS = 0x44
    SET_GAMEPAD_SETTINGS = 0x45
    GET_CALIBRATION = 0x46
    SET_CALIBRATION = 0x47
    AUTO_CALIBRATE = 0x48
    GET_KEY_CURVE = 0x49
    SET_KEY_CURVE = 0x4A
    GET_KEY_GAMEPAD_MAP = 0x4B
    SET_KEY_GAMEPAD_MAP = 0x4C
    GET_GAMEPAD_WITH_KB = 0x4D
    SET_GAMEPAD_WITH_KB = 0x4E
    
    # LED Matrix commands
    GET_LED_ENABLED = 0x60
    SET_LED_ENABLED = 0x61
    GET_LED_BRIGHTNESS = 0x62
    SET_LED_BRIGHTNESS = 0x63
    GET_LED_PIXEL = 0x64
    SET_LED_PIXEL = 0x65
    GET_LED_ROW = 0x66
    SET_LED_ROW = 0x67
    GET_LED_ALL = 0x68
    SET_LED_ALL = 0x69
    SET_LED_ALL_CHUNK = 0x6A
    LED_CLEAR = 0x6B
    LED_FILL = 0x6C
    LED_TEST_RAINBOW = 0x6D
    GET_LED_EFFECT = 0x6E
    SET_LED_EFFECT = 0x6F
    GET_LED_EFFECT_SPEED = 0x70
    SET_LED_EFFECT_SPEED = 0x71
    SET_LED_EFFECT_COLOR = 0x72
    GET_LED_FPS_LIMIT = 0x73
    SET_LED_FPS_LIMIT = 0x74
    GET_LED_DIAGNOSTIC = 0x75
    SET_LED_DIAGNOSTIC = 0x76
    
    # ADC Filter commands
    GET_FILTER_ENABLED = 0x80
    SET_FILTER_ENABLED = 0x81
    GET_FILTER_PARAMS = 0x82
    SET_FILTER_PARAMS = 0x83
    
    # Debug commands
    GET_ADC_VALUES = 0xE0
    GET_KEY_STATES = 0xE1
    GET_LOCK_STATES = 0xE2
    
    # Echo
    ECHO = 0xFE

# LED Effect modes
class LEDEffect(IntEnum):
    NONE = 0
    RAINBOW = 1
    BREATHING = 2
    COLOR_CYCLE = 3
    WAVE = 4
    REACTIVE = 5

# HID Keycodes for remapping - comprehensive list
HID_KEYCODES = {
    # Letters
    'A': 0x04, 'B': 0x05, 'C': 0x06, 'D': 0x07, 'E': 0x08, 'F': 0x09,
    'G': 0x0A, 'H': 0x0B, 'I': 0x0C, 'J': 0x0D, 'K': 0x0E, 'L': 0x0F,
    'M': 0x10, 'N': 0x11, 'O': 0x12, 'P': 0x13, 'Q': 0x14, 'R': 0x15,
    'S': 0x16, 'T': 0x17, 'U': 0x18, 'V': 0x19, 'W': 0x1A, 'X': 0x1B,
    'Y': 0x1C, 'Z': 0x1D,
    # Numbers
    '1': 0x1E, '2': 0x1F, '3': 0x20, '4': 0x21, '5': 0x22,
    '6': 0x23, '7': 0x24, '8': 0x25, '9': 0x26, '0': 0x27,
    # Control keys
    'ENTER': 0x28, 'ESC': 0x29, 'BACKSPACE': 0x2A, 'TAB': 0x2B,
    'SPACE': 0x2C, 'MINUS': 0x2D, 'EQUAL': 0x2E, 'LEFTBRACE': 0x2F,
    'RIGHTBRACE': 0x30, 'BACKSLASH': 0x31, 'SEMICOLON': 0x33,
    'APOSTROPHE': 0x34, 'GRAVE': 0x35, 'COMMA': 0x36, 'DOT': 0x37,
    'SLASH': 0x38,
    # Lock keys
    'CAPSLOCK': 0x39, 'NUMLOCK': 0x53, 'SCROLLLOCK': 0x47,
    # Function keys
    'F1': 0x3A, 'F2': 0x3B, 'F3': 0x3C, 'F4': 0x3D, 'F5': 0x3E, 'F6': 0x3F,
    'F7': 0x40, 'F8': 0x41, 'F9': 0x42, 'F10': 0x43, 'F11': 0x44, 'F12': 0x45,
    # Navigation
    'PRINTSCREEN': 0x46, 'PAUSE': 0x48, 'INSERT': 0x49,
    'HOME': 0x4A, 'PAGEUP': 0x4B, 'DELETE': 0x4C, 'END': 0x4D,
    'PAGEDOWN': 0x4E, 'RIGHT': 0x4F, 'LEFT': 0x50, 'DOWN': 0x51, 'UP': 0x52,
    # Keypad
    'KP_DIVIDE': 0x54, 'KP_MULTIPLY': 0x55, 'KP_MINUS': 0x56, 'KP_PLUS': 0x57,
    'KP_ENTER': 0x58, 'KP_1': 0x59, 'KP_2': 0x5A, 'KP_3': 0x5B, 'KP_4': 0x5C,
    'KP_5': 0x5D, 'KP_6': 0x5E, 'KP_7': 0x5F, 'KP_8': 0x60, 'KP_9': 0x61,
    'KP_0': 0x62, 'KP_DOT': 0x63,
    # Modifiers
    'LCTRL': 0xE0, 'LSHIFT': 0xE1, 'LALT': 0xE2, 'LGUI': 0xE3,
    'RCTRL': 0xE4, 'RSHIFT': 0xE5, 'RALT': 0xE6, 'RGUI': 0xE7,
    # Media keys
    'MUTE': 0x7F, 'VOLUMEUP': 0x80, 'VOLUMEDOWN': 0x81,
}

# Reverse lookup
HID_KEYCODE_NAMES = {v: k for k, v in HID_KEYCODES.items()}

# --- Response Status ---
class Status(IntEnum):
    OK = 0x00
    ERROR = 0x01
    INVALID_CMD = 0x02
    INVALID_PARAM = 0x03

def find_device_path():
    """Find the Raw HID interface path."""
    print(f"Searching for device VID=0x{VID:04x} PID=0x{PID:04x}...")
    
    for d in hid.enumerate(VID, PID):
        print(f"  -> Found: {d['product_string']} (Interface: {d['interface_number']}, UsagePage: 0x{d['usage_page']:04x})")
        
        # Raw HID is on Interface 1 or Usage Page 0xFF00
        if d['interface_number'] == 1 or d['usage_page'] == 0xFF00:
            return d['path']
    
    return None

class KBHEDevice:
    """KBHE keyboard device communication class."""
    
    def __init__(self):
        self.device = None
        self.path = None
    
    def connect(self):
        """Connect to the device."""
        self.path = find_device_path()
        if self.path is None:
            raise Exception("Device not found")
        
        print(f"✅ Raw HID interface found: {self.path}")
        
        self.device = hid.device()
        self.device.open_path(self.path)
        self.device.set_nonblocking(1)
        
        return True
    
    def disconnect(self):
        """Disconnect from the device."""
        if self.device:
            self.device.close()
            self.device = None
    
    def send_command(self, cmd_id, data=None, timeout_ms=100):
        """Send a command and wait for response."""
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
        
        # Wait for response
        time.sleep(timeout_ms / 1000.0)
        response = self.device.read(PACKET_SIZE)
        
        return response
    
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
        resp = self.send_command(Command.SET_KEYBOARD_ENABLED, data)
        return resp and len(resp) >= 2 and resp[1] == Status.OK
    
    def set_gamepad_enabled(self, enabled):
        """Set gamepad enabled state."""
        data = [0, 1 if enabled else 0]  # 0 = placeholder for status byte
        resp = self.send_command(Command.SET_GAMEPAD_ENABLED, data)
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
        resp = self.send_command(Command.SET_NKRO_ENABLED, data)
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
            distances_mm = []
            for i in range(6):
                val = resp[14 + i*2] | (resp[15 + i*2] << 8)
                distances_mm.append(val / 100.0)  # Convert to mm float
            return {
                'states': states, 
                'distances': distances_norm,  # For progress bars
                'distances_mm': distances_mm   # Actual mm values
            }
        return None

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


# ============================================================================
# LED Matrix Editor GUI
# ============================================================================

if HAS_GUI:
    class KBHEConfigApp(tk.Tk):
        """KBHE Keyboard Configuration Application with LED Matrix Editor."""
        
        def __init__(self, device):
            super().__init__()
            
            self.device = device
            self.title("KBHE Keyboard Configuration")
            self.geometry("950x950")
            self.resizable(True, True)
            
            # Current color
            self.current_color = [255, 0, 0]  # Default: Red
            
            # Pixel data (8x8 RGB)
            self.pixels = [[0, 0, 0] for _ in range(64)]
            
            # Live update flag
            self.live_sensor_update = False
            self.sensor_update_job = None
            
            # Async sensor data queue
            self.sensor_queue = queue.Queue()
            self.sensor_thread = None
            self.sensor_thread_running = False
            
            # Timing tracking for performance display
            self.last_update_time = time.time()
            self.update_count = 0
            
            # GUI elements
            self.create_widgets()
            
            # Load current state from device
            self.load_from_device()
            
            # Handle window close
            self.protocol("WM_DELETE_WINDOW", self.on_close)
        
        def on_close(self):
            """Handle window close - stop sensor thread."""
            self.sensor_thread_running = False
            if self.sensor_thread and self.sensor_thread.is_alive():
                self.sensor_thread.join(timeout=0.5)
            self.destroy()
        
        def create_widgets(self):
            """Create all GUI elements."""
            
            # Create status variable early (used by many widget callbacks)
            self.status_var = tk.StringVar(value="Ready - Click pixels to set colors (changes are LIVE)")
            
            # Create notebook for tabs
            notebook = ttk.Notebook(self)
            notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
            
            # Tab 1: LED Matrix
            led_tab = ttk.Frame(notebook, padding="10")
            notebook.add(led_tab, text="LED Matrix")
            self.create_led_widgets(led_tab)
            
            # Tab 2: LED Effects
            effects_tab = ttk.Frame(notebook, padding="10")
            notebook.add(effects_tab, text="LED Effects")
            self.create_led_effects_widgets(effects_tab)
            
            # Tab 3: Key Settings
            keys_tab = ttk.Frame(notebook, padding="10")
            notebook.add(keys_tab, text="Key Settings")
            self.create_key_settings_widgets(keys_tab)
            
            # Tab 4: Gamepad Settings
            gamepad_tab = ttk.Frame(notebook, padding="10")
            notebook.add(gamepad_tab, text="Gamepad")
            self.create_gamepad_settings_widgets(gamepad_tab)
            
            # Tab 5: Calibration
            calibration_tab = ttk.Frame(notebook, padding="10")
            notebook.add(calibration_tab, text="Calibration")
            self.create_calibration_widgets(calibration_tab)
            
            # Tab 6: Settings & Info
            settings_tab = ttk.Frame(notebook, padding="10")
            notebook.add(settings_tab, text="Settings")
            self.create_settings_widgets(settings_tab)
            
            # Tab 7: Debug / Live View
            debug_tab = ttk.Frame(notebook, padding="10")
            notebook.add(debug_tab, text="Debug / Sensors")
            self.create_debug_widgets(debug_tab)
            
            # Tab 8: Live Graph
            graph_tab = ttk.Frame(notebook, padding="10")
            notebook.add(graph_tab, text="Live Graph")
            self.create_graph_widgets(graph_tab)
            
            # Status bar at bottom (status_var already created at top)
            status_frame = ttk.Frame(self)
            status_frame.pack(fill=tk.X, padx=5, pady=5)
            
            status_label = ttk.Label(status_frame, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W)
            status_label.pack(fill=tk.X)
        
        def create_led_widgets(self, parent):
            """Create LED Matrix tab widgets."""
            
            # Info banner
            info_frame = ttk.Frame(parent)
            info_frame.pack(fill=tk.X, pady=(0, 10))
            ttk.Label(info_frame, text="💡 Changes are sent LIVE to the keyboard. Use 'Save to Flash' to make them permanent.",
                      foreground="blue").pack()
            
            # Main container with two columns
            main_container = ttk.Frame(parent)
            main_container.pack(fill=tk.BOTH, expand=True)
            
            # Left column: LED Grid
            left_frame = ttk.Frame(main_container)
            left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            
            # --- LED Grid ---
            grid_frame = ttk.LabelFrame(left_frame, text="LED Matrix (8x8) - Click to paint", padding="5")
            grid_frame.pack(fill=tk.BOTH, expand=True, pady=5)
            
            self.led_buttons = []
            for y in range(8):
                row_buttons = []
                for x in range(8):
                    btn = tk.Button(
                        grid_frame,
                        width=5, height=2,
                        bg='#000000',
                        activebackground='#333333',
                        relief=tk.RAISED,
                        command=lambda idx=y*8+x: self.on_led_click(idx)
                    )
                    btn.grid(row=y, column=x, padx=1, pady=1, sticky='nsew')
                    row_buttons.append(btn)
                self.led_buttons.append(row_buttons)
            
            # Make grid cells expand
            for i in range(8):
                grid_frame.columnconfigure(i, weight=1)
                grid_frame.rowconfigure(i, weight=1)
            
            # Right column: Controls
            right_frame = ttk.Frame(main_container, width=250)
            right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=(10, 0))
            right_frame.pack_propagate(False)
            
            # --- Color Picker ---
            color_frame = ttk.LabelFrame(right_frame, text="Paint Color", padding="5")
            color_frame.pack(fill=tk.X, pady=5)
            
            self.color_preview = tk.Canvas(color_frame, width=220, height=40, bg='#ff0000', highlightthickness=1)
            self.color_preview.pack(pady=5)
            
            ttk.Button(color_frame, text="🎨 Pick Color...", command=self.pick_color).pack(fill=tk.X, pady=2)
            
            # Quick colors grid
            quick_frame = ttk.Frame(color_frame)
            quick_frame.pack(fill=tk.X, pady=5)
            
            quick_colors = [
                ('#ff0000', 'R'), ('#00ff00', 'G'), ('#0000ff', 'B'), ('#ffffff', 'W'),
                ('#ffff00', 'Y'), ('#ff00ff', 'M'), ('#00ffff', 'C'), ('#000000', 'X'),
                ('#ff8000', 'O'), ('#80ff00', 'L'), ('#0080ff', 'S'), ('#8000ff', 'P'),
            ]
            
            for i, (color, label) in enumerate(quick_colors):
                btn = tk.Button(
                    quick_frame, bg=color, width=3, height=1,
                    text=label, fg='white' if color != '#ffffff' else 'black',
                    command=lambda c=color: self.set_color_hex(c)
                )
                btn.grid(row=i//4, column=i%4, padx=2, pady=2, sticky='nsew')
            
            for i in range(4):
                quick_frame.columnconfigure(i, weight=1)
            
            # --- Brightness ---
            brightness_frame = ttk.LabelFrame(right_frame, text="Brightness (LIVE)", padding="5")
            brightness_frame.pack(fill=tk.X, pady=5)
            
            self.brightness_var = tk.IntVar(value=50)
            self.brightness_slider = ttk.Scale(
                brightness_frame, from_=0, to=255,
                variable=self.brightness_var,
                orient=tk.HORIZONTAL,
                command=self.on_brightness_change
            )
            self.brightness_slider.pack(fill=tk.X, padx=5)
            
            self.brightness_label = ttk.Label(brightness_frame, text="50", font=('Arial', 14, 'bold'))
            self.brightness_label.pack()
            
            # --- Quick Actions ---
            action_frame = ttk.LabelFrame(right_frame, text="Quick Actions (LIVE)", padding="5")
            action_frame.pack(fill=tk.X, pady=5)
            
            ttk.Button(action_frame, text="🗑️ Clear All", command=self.clear_all).pack(fill=tk.X, pady=2)
            ttk.Button(action_frame, text="🎨 Fill with Color", command=self.fill_color).pack(fill=tk.X, pady=2)
            ttk.Button(action_frame, text="🌈 Rainbow Test", command=self.rainbow_test).pack(fill=tk.X, pady=2)
            ttk.Button(action_frame, text="🔄 Reload from Device", command=self.load_from_device).pack(fill=tk.X, pady=2)
            
            # --- Persistence ---
            save_frame = ttk.LabelFrame(right_frame, text="💾 Persistence", padding="5")
            save_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(save_frame, text="Save to make changes\nsurvive power cycle:", 
                      justify=tk.CENTER).pack(pady=2)
            ttk.Button(save_frame, text="💾 SAVE TO FLASH", 
                       command=self.save_to_device, style='Accent.TButton').pack(fill=tk.X, pady=5)
            
            ttk.Separator(save_frame, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=5)
            
            btn_frame = ttk.Frame(save_frame)
            btn_frame.pack(fill=tk.X)
            ttk.Button(btn_frame, text="📁 Export", command=self.export_to_file, width=10).pack(side=tk.LEFT, expand=True, padx=2)
            ttk.Button(btn_frame, text="📂 Import", command=self.import_from_file, width=10).pack(side=tk.LEFT, expand=True, padx=2)
        
        def create_settings_widgets(self, parent):
            """Create Settings tab widgets."""
            
            # Device Info
            info_frame = ttk.LabelFrame(parent, text="📱 Device Information", padding="10")
            info_frame.pack(fill=tk.X, pady=5)
            
            self.firmware_label = ttk.Label(info_frame, text="Firmware Version: Loading...", font=('Arial', 10))
            self.firmware_label.pack(anchor=tk.W)
            
            # Refresh firmware version
            version = self.device.get_firmware_version()
            self.firmware_label.config(text=f"Firmware Version: {version if version else 'Unknown'}")
            
            # HID Interfaces
            interfaces_frame = ttk.LabelFrame(parent, text="🔌 HID Interfaces (LIVE changes)", padding="10")
            interfaces_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(interfaces_frame, text="⚠️ Changes take effect immediately but are NOT saved until you click 'Save to Flash'",
                      foreground="orange").pack(anchor=tk.W, pady=(0, 10))
            
            self.keyboard_enabled_var = tk.BooleanVar(value=False)
            kb_check = ttk.Checkbutton(
                interfaces_frame, text="⌨️ Keyboard HID (sends keypresses)",
                variable=self.keyboard_enabled_var,
                command=self.on_keyboard_enabled_change
            )
            kb_check.pack(anchor=tk.W, pady=2)
            
            self.gamepad_enabled_var = tk.BooleanVar(value=True)
            gp_check = ttk.Checkbutton(
                interfaces_frame, text="🎮 Gamepad HID (analog axes)",
                variable=self.gamepad_enabled_var,
                command=self.on_gamepad_enabled_change
            )
            gp_check.pack(anchor=tk.W, pady=2)
            
            self.led_enabled_var = tk.BooleanVar(value=True)
            led_check = ttk.Checkbutton(
                interfaces_frame, text="💡 LED Matrix (WS2812)",
                variable=self.led_enabled_var,
                command=self.on_led_enabled_change
            )
            led_check.pack(anchor=tk.W, pady=2)
            
            # NKRO Mode
            ttk.Separator(interfaces_frame, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=5)
            ttk.Label(interfaces_frame, text="Keyboard Mode:", font=('Arial', 9, 'bold')).pack(anchor=tk.W)
            
            self.nkro_enabled_var = tk.BooleanVar(value=False)
            nkro_check = ttk.Checkbutton(
                interfaces_frame, text="🔠 NKRO Mode (N-Key Rollover instead of 6KRO)",
                variable=self.nkro_enabled_var,
                command=self.on_nkro_enabled_change
            )
            nkro_check.pack(anchor=tk.W, pady=2)
            ttk.Label(interfaces_frame, text="    Uses independent HID interface for unlimited simultaneous keys",
                      foreground="gray").pack(anchor=tk.W)
            
            # Save section
            save_frame = ttk.LabelFrame(parent, text="💾 Save Settings to Flash", padding="10")
            save_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(save_frame, text="Click to save ALL settings (LED pattern, brightness, enabled states)\nto flash memory. Settings will persist after power cycle.",
                      justify=tk.LEFT).pack(anchor=tk.W, pady=5)
            
            ttk.Button(save_frame, text="💾 SAVE ALL SETTINGS TO FLASH", 
                       command=self.save_to_device).pack(fill=tk.X, pady=5)
            
            # Factory Reset
            reset_frame = ttk.LabelFrame(parent, text="🔧 Factory Reset", padding="10")
            reset_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(reset_frame, text="Reset all settings to factory defaults.\nThis will clear LED patterns and reset all options.",
                      foreground="red").pack(anchor=tk.W, pady=5)
            
            ttk.Button(reset_frame, text="⚠️ Factory Reset", command=self.factory_reset).pack(anchor=tk.W)
        
        def create_debug_widgets(self, parent):
            """Create Debug/Sensors tab widgets."""
            
            # ADC Live Values
            adc_frame = ttk.LabelFrame(parent, text="📊 ADC Sensor Values (Live)", padding="10")
            adc_frame.pack(fill=tk.X, pady=5)
            
            # Refresh rate control
            rate_frame = ttk.Frame(adc_frame)
            rate_frame.pack(fill=tk.X)
            
            self.live_update_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                rate_frame, text="Enable Live Updates",
                variable=self.live_update_var,
                command=self.toggle_live_update
            ).pack(side=tk.LEFT)
            
            ttk.Label(rate_frame, text="  Refresh (ms):").pack(side=tk.LEFT)
            self.refresh_rate_var = tk.IntVar(value=50)
            refresh_spin = ttk.Spinbox(rate_frame, from_=20, to=500, width=5, 
                                       textvariable=self.refresh_rate_var)
            refresh_spin.pack(side=tk.LEFT, padx=5)
            
            # LUT Parameters info
            ttk.Label(adc_frame, text="LUT: ADC 2100-2672 → Distance 0-4mm | ADC typical range: 2000-2700",
                      font=('Consolas', 8), foreground='gray').pack(anchor=tk.W, pady=(5,0))
            
            # Create ADC value displays
            adc_values_frame = ttk.Frame(adc_frame)
            adc_values_frame.pack(fill=tk.X, pady=10)
            
            self.adc_labels = []
            self.adc_bars = []
            self.distance_labels = []
            
            # Header row
            header_frame = ttk.Frame(adc_values_frame)
            header_frame.pack(fill=tk.X, pady=2)
            ttk.Label(header_frame, text="Key", width=6, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT)
            ttk.Label(header_frame, text="ADC Bar (2000-2700)", width=30, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT, padx=5)
            ttk.Label(header_frame, text="ADC", width=6, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT)
            ttk.Label(header_frame, text="Distance", width=10, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT)
            
            for i in range(6):
                row_frame = ttk.Frame(adc_values_frame)
                row_frame.pack(fill=tk.X, pady=2)
                
                ttk.Label(row_frame, text=f"Key {i+1}:", width=6, font=('Consolas', 9)).pack(side=tk.LEFT)
                
                # Progress bar for visual - optimized range 2000-2700
                bar = ttk.Progressbar(row_frame, length=250, maximum=700, mode='determinate')
                bar.pack(side=tk.LEFT, padx=5)
                self.adc_bars.append(bar)
                
                # Numeric ADC value
                label = ttk.Label(row_frame, text="----", width=6, font=('Consolas', 10))
                label.pack(side=tk.LEFT)
                self.adc_labels.append(label)
                
                # Distance value (calculated from LUT)
                dist_label = ttk.Label(row_frame, text="-.--mm", width=10, font=('Consolas', 10))
                dist_label.pack(side=tk.LEFT)
                self.distance_labels.append(dist_label)
            
            # Key States
            key_frame = ttk.LabelFrame(parent, text="🔘 Key States", padding="10")
            key_frame.pack(fill=tk.X, pady=5)
            
            key_states_frame = ttk.Frame(key_frame)
            key_states_frame.pack(fill=tk.X)
            
            self.key_state_labels = []
            self.key_distance_bars = []  # Mini bars for key distances
            for i in range(6):
                frame = ttk.Frame(key_states_frame)
                frame.pack(side=tk.LEFT, padx=10, expand=True)
                
                ttk.Label(frame, text=f"Key {i+1}", font=('Arial', 9)).pack()
                state_label = ttk.Label(frame, text="⬜", font=('Arial', 20))
                state_label.pack()
                self.key_state_labels.append(state_label)
                
                # Mini distance bar (0-255 normalized)
                dist_bar = ttk.Progressbar(frame, length=50, maximum=255, mode='determinate')
                dist_bar.pack(pady=2)
                self.key_distance_bars.append(dist_bar)
            
            # Timing/Performance info
            timing_frame = ttk.LabelFrame(parent, text="⏱️ Performance", padding="10")
            timing_frame.pack(fill=tk.X, pady=5)
            
            timing_info_frame = ttk.Frame(timing_frame)
            timing_info_frame.pack(fill=tk.X)
            
            ttk.Label(timing_info_frame, text="HID Poll Rate:", width=15).pack(side=tk.LEFT)
            self.hid_rate_label = ttk.Label(timing_info_frame, text="-- Hz", font=('Consolas', 10))
            self.hid_rate_label.pack(side=tk.LEFT, padx=10)
            
            ttk.Label(timing_info_frame, text="GUI Update Rate:", width=15).pack(side=tk.LEFT)
            self.gui_rate_label = ttk.Label(timing_info_frame, text="-- Hz", font=('Consolas', 10))
            self.gui_rate_label.pack(side=tk.LEFT, padx=10)
            
            # Timing tracking
            self.last_update_time = time.time()
            self.update_count = 0
            
            # Lock Indicators Status
            lock_frame = ttk.LabelFrame(parent, text="🔒 Lock Indicators", padding="10")
            lock_frame.pack(fill=tk.X, pady=5)
            
            lock_status_frame = ttk.Frame(lock_frame)
            lock_status_frame.pack(fill=tk.X)
            
            # Caps Lock indicator
            caps_frame = ttk.Frame(lock_status_frame)
            caps_frame.pack(side=tk.LEFT, padx=20)
            ttk.Label(caps_frame, text="Caps Lock", font=('Arial', 10)).pack()
            self.caps_lock_indicator = ttk.Label(caps_frame, text="⬜ OFF", font=('Arial', 14))
            self.caps_lock_indicator.pack()
            
            # Num Lock indicator
            num_frame = ttk.Frame(lock_status_frame)
            num_frame.pack(side=tk.LEFT, padx=20)
            ttk.Label(num_frame, text="Num Lock", font=('Arial', 10)).pack()
            self.num_lock_indicator = ttk.Label(num_frame, text="⬜ OFF", font=('Arial', 14))
            self.num_lock_indicator.pack()
            
            # Scroll Lock indicator
            scroll_frame = ttk.Frame(lock_status_frame)
            scroll_frame.pack(side=tk.LEFT, padx=20)
            ttk.Label(scroll_frame, text="Scroll Lock", font=('Arial', 10)).pack()
            self.scroll_lock_indicator = ttk.Label(scroll_frame, text="⬜ OFF", font=('Arial', 14))
            self.scroll_lock_indicator.pack()
            
            # PE0 LED Status
            pe0_frame = ttk.Frame(lock_status_frame)
            pe0_frame.pack(side=tk.LEFT, padx=20)
            ttk.Label(pe0_frame, text="PE0 LED", font=('Arial', 10)).pack()
            self.pe0_led_indicator = ttk.Label(pe0_frame, text="⬜ OFF", font=('Arial', 14))
            self.pe0_led_indicator.pack()
            
            # Manual lock toggle buttons (for testing)
            lock_buttons_frame = ttk.Frame(lock_frame)
            lock_buttons_frame.pack(fill=tk.X, pady=5)
            ttk.Button(lock_buttons_frame, text="Toggle Caps Lock", 
                       command=lambda: self.send_keypress(0x39)).pack(side=tk.LEFT, padx=5)
            ttk.Button(lock_buttons_frame, text="Toggle Num Lock", 
                       command=lambda: self.send_keypress(0x53)).pack(side=tk.LEFT, padx=5)
            ttk.Button(lock_buttons_frame, text="Toggle Scroll Lock", 
                       command=lambda: self.send_keypress(0x47)).pack(side=tk.LEFT, padx=5)
            
            # Current Configuration Summary
            config_frame = ttk.LabelFrame(parent, text="📋 Current Configuration", padding="10")
            config_frame.pack(fill=tk.X, pady=5)
            
            self.config_text = tk.Text(config_frame, height=10, width=80, font=('Consolas', 9))
            self.config_text.pack(fill=tk.X)
            
            ttk.Button(config_frame, text="🔄 Refresh Configuration", command=self.refresh_config_display).pack(pady=5)
            
            # ADC EMA Filter Settings
            filter_frame = ttk.LabelFrame(parent, text="🎚️ ADC EMA Filter Settings", padding="10")
            filter_frame.pack(fill=tk.X, pady=5)
            
            # Filter enable checkbox
            self.filter_enabled_var = tk.BooleanVar(value=True)
            ttk.Checkbutton(
                filter_frame, text="Enable ADC EMA Filter",
                variable=self.filter_enabled_var,
                command=self.on_filter_enabled_change
            ).pack(anchor=tk.W)
            
            ttk.Label(filter_frame, text="(Disabling filter shows raw ADC values, may be noisy)", 
                      foreground="gray").pack(anchor=tk.W, pady=(0, 10))
            
            # Filter parameters
            params_frame = ttk.Frame(filter_frame)
            params_frame.pack(fill=tk.X)
            
            # Noise Band
            nb_frame = ttk.Frame(params_frame)
            nb_frame.pack(fill=tk.X, pady=2)
            ttk.Label(nb_frame, text="Noise Band (ADC counts):", width=25).pack(side=tk.LEFT)
            self.filter_noise_band_var = tk.IntVar(value=30)
            ttk.Spinbox(nb_frame, from_=1, to=100, width=8, 
                        textvariable=self.filter_noise_band_var).pack(side=tk.LEFT, padx=5)
            ttk.Label(nb_frame, text="(default: 30)", foreground="gray").pack(side=tk.LEFT)
            
            # Alpha Min Denominator
            amin_frame = ttk.Frame(params_frame)
            amin_frame.pack(fill=tk.X, pady=2)
            ttk.Label(amin_frame, text="Alpha Min (1/N, slow):", width=25).pack(side=tk.LEFT)
            self.filter_alpha_min_var = tk.IntVar(value=32)
            ttk.Spinbox(amin_frame, from_=2, to=128, width=8, 
                        textvariable=self.filter_alpha_min_var).pack(side=tk.LEFT, padx=5)
            ttk.Label(amin_frame, text="(default: 32 → 1/32 = strong smoothing)", foreground="gray").pack(side=tk.LEFT)
            
            # Alpha Max Denominator
            amax_frame = ttk.Frame(params_frame)
            amax_frame.pack(fill=tk.X, pady=2)
            ttk.Label(amax_frame, text="Alpha Max (1/N, fast):", width=25).pack(side=tk.LEFT)
            self.filter_alpha_max_var = tk.IntVar(value=4)
            ttk.Spinbox(amax_frame, from_=1, to=32, width=8, 
                        textvariable=self.filter_alpha_max_var).pack(side=tk.LEFT, padx=5)
            ttk.Label(amax_frame, text="(default: 4 → 1/4 = fast response)", foreground="gray").pack(side=tk.LEFT)
            
            # Apply button
            btn_frame = ttk.Frame(filter_frame)
            btn_frame.pack(fill=tk.X, pady=10)
            ttk.Button(btn_frame, text="📤 Apply Filter Settings", 
                       command=self.apply_filter_settings).pack(side=tk.LEFT, padx=5)
            ttk.Button(btn_frame, text="📥 Reload From Device", 
                       command=self.load_filter_settings).pack(side=tk.LEFT, padx=5)
            ttk.Button(btn_frame, text="🔄 Reset to Defaults", 
                       command=self.reset_filter_defaults).pack(side=tk.LEFT, padx=5)
            
            # Load initial filter settings
            self.load_filter_settings()
            
            # LED Diagnostic Mode (for troubleshooting ADC noise)
            diag_frame = ttk.LabelFrame(parent, text="🔬 LED Diagnostic Mode (ADC Noise Testing)", padding="10")
            diag_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(diag_frame, text="Use this to diagnose if ADC noise is caused by LED DMA activity or CPU computation:",
                      foreground="gray").pack(anchor=tk.W)
            
            self.diagnostic_mode_var = tk.IntVar(value=0)
            
            ttk.Radiobutton(diag_frame, text="Normal Operation", 
                           variable=self.diagnostic_mode_var, value=0,
                           command=self.on_diagnostic_mode_change).pack(anchor=tk.W)
            
            ttk.Radiobutton(diag_frame, text="Mode 1: DMA Stress (sends LED data, no CPU computation)", 
                           variable=self.diagnostic_mode_var, value=1,
                           command=self.on_diagnostic_mode_change).pack(anchor=tk.W)
            ttk.Label(diag_frame, text="    → If noise appears: DMA/electrical interference is the cause",
                      foreground="blue", font=('Arial', 9)).pack(anchor=tk.W)
            
            ttk.Radiobutton(diag_frame, text="Mode 2: CPU Stress (computes effects, no LED updates)", 
                           variable=self.diagnostic_mode_var, value=2,
                           command=self.on_diagnostic_mode_change).pack(anchor=tk.W)
            ttk.Label(diag_frame, text="    → If noise appears: CPU load/interrupt latency is the cause",
                      foreground="blue", font=('Arial', 9)).pack(anchor=tk.W)
            
            ttk.Radiobutton(diag_frame, text="Mode 3: DMA + CPU (computes & sends data, LED pin disabled)", 
                           variable=self.diagnostic_mode_var, value=3,
                           command=self.on_diagnostic_mode_change).pack(anchor=tk.W)
            ttk.Label(diag_frame, text="    → If noise disappears: PWM pin switching is causing interference",
                      foreground="blue", font=('Arial', 9)).pack(anchor=tk.W)
            
            ttk.Label(diag_frame, text="\n💡 Tips: Also try moving LED cables away from sensor wires to test electrical coupling",
                      foreground="gray").pack(anchor=tk.W)
            
            # Initial config display
            self.refresh_config_display()
        
        def toggle_live_update(self):
            """Toggle live sensor updates using background thread."""
            if self.live_update_var.get():
                self.live_sensor_update = True
                self.last_update_time = time.time()
                self.update_count = 0
                
                # Start background thread for sensor reading
                self.sensor_thread_running = True
                self.sensor_thread = threading.Thread(target=self._sensor_reader_thread, daemon=True)
                self.sensor_thread.start()
                
                # Start GUI updater
                self._process_sensor_queue()
            else:
                self.live_sensor_update = False
                self.sensor_thread_running = False
                if self.sensor_update_job:
                    self.after_cancel(self.sensor_update_job)
                    self.sensor_update_job = None
        
        def _sensor_reader_thread(self):
            """Background thread that reads sensor data."""
            while self.sensor_thread_running:
                try:
                    adc_values = self.device.get_adc_values()
                    key_states = self.device.get_key_states()
                    lock_states = self.device.get_lock_states()
                    self.sensor_queue.put({'adc': adc_values, 'keys': key_states, 'locks': lock_states})
                except Exception as e:
                    self.sensor_queue.put({'error': str(e)})
                
                # Sleep based on refresh rate (convert ms to seconds)
                try:
                    refresh_ms = self.refresh_rate_var.get()
                except:
                    refresh_ms = 50
                time.sleep(refresh_ms / 1000.0)
        
        def _process_sensor_queue(self):
            """Process sensor data from queue and update GUI."""
            if not self.live_sensor_update:
                return
            
            try:
                # Process all queued data (get latest)
                data = None
                while not self.sensor_queue.empty():
                    data = self.sensor_queue.get_nowait()
                
                if data:
                    if 'error' in data:
                        self.status_var.set(f"Sensor error: {data['error']}")
                    else:
                        self._update_sensor_display(data.get('adc'), data.get('keys'))
                        self._update_lock_display(data.get('locks'))
                
            except queue.Empty:
                pass
            except Exception as e:
                self.status_var.set(f"GUI update error: {e}")
            
            # Schedule next GUI update (faster than sensor read for responsiveness)
            self.sensor_update_job = self.after(16, self._process_sensor_queue)  # ~60 FPS GUI
        
        def _update_sensor_display(self, adc_data, key_states):
            """Update GUI with sensor data (called from main thread)."""
            if adc_data:
                # adc_data is now a dict with 'adc', 'scan_time_us', 'scan_rate_hz'
                adc_values = adc_data.get('adc', [])
                for i, val in enumerate(adc_values):
                    # Progress bar range is 2000-2700, so subtract 2000 for bar value
                    bar_val = max(0, min(700, val - 2000))
                    self.adc_bars[i]['value'] = bar_val
                    self.adc_labels[i].config(text=f"{val:4d}")
                
                # Update MCU timing display
                scan_rate = adc_data.get('scan_rate_hz', 0)
                scan_time = adc_data.get('scan_time_us', 0)
                self.hid_rate_label.config(text=f"{scan_rate} Hz ({scan_time}µs)")
            
            if key_states:
                for i, state in enumerate(key_states['states']):
                    self.key_state_labels[i].config(
                        text="🟢" if state else "⬜",
                        foreground="green" if state else "gray"
                    )
                # Update distance bars from normalized distances
                if 'distances' in key_states:
                    for i, dist in enumerate(key_states['distances']):
                        self.key_distance_bars[i]['value'] = dist
                
                # Update distance in mm from MCU (actual LUT values)
                if 'distances_mm' in key_states:
                    for i, dist_mm in enumerate(key_states['distances_mm']):
                        self.distance_labels[i].config(text=f"{dist_mm:.2f}mm")
            
            # Update GUI timing stats
            self.update_count += 1
            now = time.time()
            elapsed = now - self.last_update_time
            if elapsed >= 1.0:
                gui_rate = self.update_count / elapsed
                self.gui_rate_label.config(text=f"{gui_rate:.1f} Hz")
                self.update_count = 0
                self.last_update_time = now
        
        def _update_lock_display(self, lock_states):
            """Update lock indicator display (called from main thread)."""
            if not lock_states:
                return
            
            caps = lock_states.get('caps_lock', False)
            num = lock_states.get('num_lock', False)
            scroll = lock_states.get('scroll_lock', False)
            
            # Update Caps Lock indicator
            self.caps_lock_indicator.config(
                text="🟢 ON" if caps else "⬜ OFF",
                foreground="green" if caps else "gray"
            )
            
            # Update Num Lock indicator
            self.num_lock_indicator.config(
                text="🟢 ON" if num else "⬜ OFF",
                foreground="green" if num else "gray"
            )
            
            # Update Scroll Lock indicator
            self.scroll_lock_indicator.config(
                text="🟢 ON" if scroll else "⬜ OFF",
                foreground="green" if scroll else "gray"
            )
            
            # Update PE0 LED status based on firmware behavior
            # Both ON = solid, Caps only = fast blink, Num only = slow blink, Both OFF = off
            if caps and num:
                pe0_text = "🟢 SOLID"
                pe0_color = "green"
            elif caps and not num:
                pe0_text = "🟡 FAST BLINK"
                pe0_color = "orange"
            elif num and not caps:
                pe0_text = "🟡 SLOW BLINK"
                pe0_color = "orange"
            else:
                pe0_text = "⬜ OFF"
                pe0_color = "gray"
            
            self.pe0_led_indicator.config(text=pe0_text, foreground=pe0_color)
        
        def send_keypress(self, keycode):
            """Send a single key press and release via the keyboard interface."""
            # This is just for testing - toggle locks via actual key press
            # For now, show a message that user needs to press physical key
            self.status_var.set(f"Press physical key with HID code 0x{keycode:02X} to toggle")
        
        # --- Filter Settings Methods ---
        
        def load_filter_settings(self):
            """Load filter settings from device."""
            try:
                enabled = self.device.get_filter_enabled()
                if enabled is not None:
                    self.filter_enabled_var.set(enabled)
                
                params = self.device.get_filter_params()
                if params:
                    self.filter_noise_band_var.set(params['noise_band'])
                    self.filter_alpha_min_var.set(params['alpha_min_denom'])
                    self.filter_alpha_max_var.set(params['alpha_max_denom'])
                    
                self.status_var.set("📥 Filter settings loaded from device")
            except Exception as e:
                self.status_var.set(f"❌ Error loading filter settings: {e}")
        
        def on_filter_enabled_change(self):
            """Handle filter enable/disable toggle."""
            enabled = self.filter_enabled_var.get()
            if self.device.set_filter_enabled(enabled):
                self.status_var.set(f"🎚️ Filter {'enabled' if enabled else 'disabled'} - LIVE")
            else:
                self.status_var.set("❌ Failed to update filter state")
        
        def apply_filter_settings(self):
            """Apply filter parameters to device."""
            noise_band = self.filter_noise_band_var.get()
            alpha_min = self.filter_alpha_min_var.get()
            alpha_max = self.filter_alpha_max_var.get()
            
            if self.device.set_filter_params(noise_band, alpha_min, alpha_max):
                self.status_var.set(f"📤 Filter params applied: band={noise_band}, αmin=1/{alpha_min}, αmax=1/{alpha_max}")
            else:
                self.status_var.set("❌ Failed to apply filter parameters")
        
        def reset_filter_defaults(self):
            """Reset filter to default values."""
            self.filter_enabled_var.set(True)
            self.filter_noise_band_var.set(30)
            self.filter_alpha_min_var.set(32)
            self.filter_alpha_max_var.set(4)
            
            # Apply defaults to device
            self.device.set_filter_enabled(True)
            self.device.set_filter_params(30, 32, 4)
            self.status_var.set("🔄 Filter reset to defaults")
        
        def on_diagnostic_mode_change(self):
            """Handle diagnostic mode change."""
            mode = self.diagnostic_mode_var.get()
            mode_names = {0: "Normal", 1: "DMA Stress", 2: "CPU Stress"}
            if self.device.set_led_diagnostic(mode):
                self.status_var.set(f"🔬 Diagnostic mode: {mode_names.get(mode, 'Unknown')}")
            else:
                self.status_var.set("❌ Failed to set diagnostic mode")
        
        def refresh_config_display(self):
            """Refresh the configuration display."""
            self.config_text.delete(1.0, tk.END)
            
            try:
                version = self.device.get_firmware_version()
                options = self.device.get_options()
                brightness = self.device.led_get_brightness()
                led_enabled = self.device.led_get_enabled()
                
                config_str = f"""=== KBHE Configuration ===
                
Firmware Version: {version if version else 'Unknown'}

HID Interfaces:
  - Keyboard: {'Enabled' if options and options['keyboard_enabled'] else 'Disabled'}
  - Gamepad:  {'Enabled' if options and options['gamepad_enabled'] else 'Disabled'}
  - Raw HID:  Always Enabled

LED Matrix:
  - Enabled:    {'Yes' if led_enabled else 'No'}
  - Brightness: {brightness if brightness is not None else 'Unknown'}

Note: Changes to toggles are sent immediately to the device
but NOT saved to flash until you click "Save to Flash".
"""
                self.config_text.insert(tk.END, config_str)
            except Exception as e:
                self.config_text.insert(tk.END, f"Error reading config: {e}")
        
        # --- LED Tab Methods ---
        
        def on_led_click(self, index):
            """Handle LED button click - sends immediately to device."""
            r, g, b = self.current_color
            self.pixels[index] = [r, g, b]
            self.update_led_display(index)
            
            # Send to device immediately
            self.device.led_set_pixel(index, r, g, b)
            self.status_var.set(f"✏️ Set LED {index} to RGB({r}, {g}, {b}) - LIVE (not saved)")
        
        def update_led_display(self, index=None):
            """Update LED button colors."""
            if index is not None:
                y, x = divmod(index, 8)
                r, g, b = self.pixels[index]
                color = f'#{r:02x}{g:02x}{b:02x}'
                self.led_buttons[y][x].config(bg=color)
            else:
                for idx in range(64):
                    y, x = divmod(idx, 8)
                    r, g, b = self.pixels[idx]
                    color = f'#{r:02x}{g:02x}{b:02x}'
                    self.led_buttons[y][x].config(bg=color)
        
        def pick_color(self):
            """Open color picker dialog."""
            color = colorchooser.askcolor(
                initialcolor=f'#{self.current_color[0]:02x}{self.current_color[1]:02x}{self.current_color[2]:02x}',
                title="Choose LED Color"
            )
            if color[0]:
                self.current_color = [int(c) for c in color[0]]
                self.color_preview.config(bg=color[1])
        
        def set_color_hex(self, hex_color):
            """Set current color from hex string."""
            hex_color = hex_color.lstrip('#')
            self.current_color = [int(hex_color[i:i+2], 16) for i in (0, 2, 4)]
            self.color_preview.config(bg=f'#{hex_color}')
        
        def on_brightness_change(self, value):
            """Handle brightness slider change - sends immediately."""
            brightness = int(float(value))
            self.brightness_label.config(text=str(brightness))
            self.device.led_set_brightness(brightness)
            self.status_var.set(f"💡 Brightness set to {brightness} - LIVE (not saved)")
        
        def clear_all(self):
            """Clear all LEDs - sends immediately."""
            self.pixels = [[0, 0, 0] for _ in range(64)]
            self.update_led_display()
            self.device.led_clear()
            self.status_var.set("🗑️ All LEDs cleared - LIVE (not saved)")
        
        def fill_color(self):
            """Fill all LEDs with current color - sends immediately."""
            r, g, b = self.current_color
            self.pixels = [[r, g, b] for _ in range(64)]
            self.update_led_display()
            self.device.led_fill(r, g, b)
            self.status_var.set(f"🎨 Filled all LEDs with RGB({r}, {g}, {b}) - LIVE (not saved)")
        
        def rainbow_test(self):
            """Show rainbow test pattern."""
            self.device.led_test_rainbow()
            self.status_var.set("🌈 Rainbow test pattern displayed")
            # Reload to show actual state
            self.after(200, self.load_from_device)
        
        def load_from_device(self):
            """Load current state from device."""
            try:
                # Load brightness
                brightness = self.device.led_get_brightness()
                if brightness is not None:
                    self.brightness_var.set(brightness)
                    self.brightness_label.config(text=str(brightness))
                
                # Load enabled states
                led_enabled = self.device.led_get_enabled()
                if led_enabled is not None:
                    self.led_enabled_var.set(led_enabled)
                
                options = self.device.get_options()
                if options:
                    self.keyboard_enabled_var.set(options['keyboard_enabled'])
                    self.gamepad_enabled_var.set(options['gamepad_enabled'])
                
                # Load NKRO state
                nkro_enabled = self.device.get_nkro_enabled()
                if nkro_enabled is not None and hasattr(self, 'nkro_enabled_var'):
                    self.nkro_enabled_var.set(nkro_enabled)
                
                # Load pixel data
                pixel_data = self.device.led_download_all()
                if pixel_data:
                    for i in range(64):
                        self.pixels[i] = [pixel_data[i*3], pixel_data[i*3+1], pixel_data[i*3+2]]
                    self.update_led_display()
                
                self.status_var.set("🔄 Loaded current state from device")
            except Exception as e:
                self.status_var.set(f"❌ Error loading: {e}")
        
        def save_to_device(self):
            """Save current state to device flash."""
            try:
                # Upload pixel data first
                pixel_data = []
                for pixel in self.pixels:
                    pixel_data.extend(pixel)
                
                self.status_var.set("💾 Uploading LED data...")
                self.update()
                
                if self.device.led_upload_all(pixel_data):
                    self.status_var.set("💾 Saving to flash...")
                    self.update()
                    
                    if self.device.save_settings():
                        self.status_var.set("✅ All settings saved to flash!")
                        messagebox.showinfo("Success", "Settings saved to device flash!\n\nYour LED pattern and settings will persist after power cycle.")
                    else:
                        self.status_var.set("❌ Failed to save to flash")
                        messagebox.showerror("Error", "Failed to save settings to flash")
                else:
                    self.status_var.set("❌ Failed to upload LED data")
                    messagebox.showerror("Error", "Failed to upload LED data to device")
            except Exception as e:
                self.status_var.set(f"❌ Error saving: {e}")
                messagebox.showerror("Error", f"Error saving to device: {e}")
        
        def export_to_file(self):
            """Export LED pattern to file."""
            filename = filedialog.asksaveasfilename(
                defaultextension=".led",
                filetypes=[("LED Pattern", "*.led"), ("All Files", "*.*")]
            )
            if filename:
                try:
                    with open(filename, 'wb') as f:
                        f.write(bytes([self.brightness_var.get()]))
                        for pixel in self.pixels:
                            f.write(bytes(pixel))
                    self.status_var.set(f"📁 Exported to {filename}")
                except Exception as e:
                    self.status_var.set(f"❌ Export error: {e}")
        
        def import_from_file(self):
            """Import LED pattern from file."""
            filename = filedialog.askopenfilename(
                filetypes=[("LED Pattern", "*.led"), ("All Files", "*.*")]
            )
            if filename:
                try:
                    with open(filename, 'rb') as f:
                        data = f.read()
                    
                    if len(data) >= 193:
                        brightness = data[0]
                        self.brightness_var.set(brightness)
                        self.brightness_label.config(text=str(brightness))
                        self.device.led_set_brightness(brightness)
                        
                        for i in range(64):
                            self.pixels[i] = [data[1 + i*3], data[1 + i*3 + 1], data[1 + i*3 + 2]]
                        
                        self.update_led_display()
                        
                        # Upload to device
                        pixel_data = list(data[1:193])
                        self.device.led_upload_all(pixel_data)
                        
                        self.status_var.set(f"📂 Imported from {filename} - LIVE (not saved)")
                    else:
                        self.status_var.set("❌ Invalid file format")
                except Exception as e:
                    self.status_var.set(f"❌ Import error: {e}")
        
        def on_led_enabled_change(self):
            """Handle LED enabled checkbox change."""
            enabled = self.led_enabled_var.get()
            self.device.led_set_enabled(enabled)
            self.status_var.set(f"💡 LED {'enabled' if enabled else 'disabled'} - LIVE (not saved)")
        
        def on_keyboard_enabled_change(self):
            """Handle keyboard enabled checkbox change."""
            enabled = self.keyboard_enabled_var.get()
            self.device.set_keyboard_enabled(enabled)
            self.status_var.set(f"⌨️ Keyboard {'enabled' if enabled else 'disabled'} - LIVE (not saved)")
        
        def on_gamepad_enabled_change(self):
            """Handle gamepad enabled checkbox change."""
            enabled = self.gamepad_enabled_var.get()
            self.device.set_gamepad_enabled(enabled)
            self.status_var.set(f"🎮 Gamepad {'enabled' if enabled else 'disabled'} - LIVE (not saved)")
        
        def on_nkro_enabled_change(self):
            """Handle NKRO mode checkbox change."""
            enabled = self.nkro_enabled_var.get()
            self.device.set_nkro_enabled(enabled)
            self.status_var.set(f"🔠 NKRO {'enabled' if enabled else 'disabled'} - LIVE (not saved)")
        
        # ===================================================================
        # LED Effects Tab
        # ===================================================================
        
        def create_led_effects_widgets(self, parent):
            """Create LED Effects tab widgets."""
            
            # Effect Mode Selection
            mode_frame = ttk.LabelFrame(parent, text="🎨 Effect Mode", padding="10")
            mode_frame.pack(fill=tk.X, pady=5)
            
            self.effect_mode_var = tk.IntVar(value=0)
            
            effect_modes = [
                (0, "None (Static Pattern)"),
                (1, "Rainbow Wave"),
                (2, "Breathing"),
                (3, "Static Rainbow"),
                (4, "Solid Color"),
                (5, "Plasma"),
                (6, "Fire"),
                (7, "Ocean Waves"),
                (8, "Matrix Rain"),
                (9, "Sparkle"),
                (10, "Breathing Rainbow"),
                (11, "Spiral"),
                (12, "Color Cycle"),
                (13, "Reactive (Key Press)")
            ]
            
            # Use a scrollable frame for effect modes
            effects_canvas = tk.Canvas(mode_frame, height=200)
            effects_scrollbar = ttk.Scrollbar(mode_frame, orient="vertical", command=effects_canvas.yview)
            effects_inner_frame = ttk.Frame(effects_canvas)
            
            effects_canvas.configure(yscrollcommand=effects_scrollbar.set)
            effects_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            effects_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
            
            effects_canvas_window = effects_canvas.create_window((0, 0), window=effects_inner_frame, anchor="nw")
            
            for val, text in effect_modes:
                rb = ttk.Radiobutton(
                    effects_inner_frame, text=text, variable=self.effect_mode_var,
                    value=val, command=self.on_effect_mode_change
                )
                rb.pack(anchor=tk.W, pady=2)
            
            def on_effects_frame_configure(event):
                effects_canvas.configure(scrollregion=effects_canvas.bbox("all"))
            effects_inner_frame.bind("<Configure>", on_effects_frame_configure)
            
            # Effect Speed
            speed_frame = ttk.LabelFrame(parent, text="⚡ Effect Speed", padding="10")
            speed_frame.pack(fill=tk.X, pady=5)
            
            self.effect_speed_var = tk.IntVar(value=128)
            speed_slider = ttk.Scale(
                speed_frame, from_=1, to=255,
                variable=self.effect_speed_var,
                orient=tk.HORIZONTAL,
                command=self.on_effect_speed_change
            )
            speed_slider.pack(fill=tk.X, padx=5)
            
            self.effect_speed_label = ttk.Label(speed_frame, text="128", font=('Arial', 12))
            self.effect_speed_label.pack()
            
            ttk.Label(speed_frame, text="(1 = Slow, 255 = Fast)", foreground="gray").pack()
            
            # FPS Limit
            fps_frame = ttk.LabelFrame(parent, text="🎬 FPS Limit", padding="10")
            fps_frame.pack(fill=tk.X, pady=5)
            
            self.fps_limit_var = tk.IntVar(value=60)
            fps_slider = ttk.Scale(
                fps_frame, from_=0, to=120,
                variable=self.fps_limit_var,
                orient=tk.HORIZONTAL,
                command=self.on_fps_limit_change
            )
            fps_slider.pack(fill=tk.X, padx=5)
            
            self.fps_limit_label = ttk.Label(fps_frame, text="60 FPS", font=('Arial', 12))
            self.fps_limit_label.pack()
            
            ttk.Label(fps_frame, text="(0 = Unlimited, 1-120 = Limit)", foreground="gray").pack()
            
            # Effect Color
            color_frame = ttk.LabelFrame(parent, text="🎨 Effect Color", padding="10")
            color_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(color_frame, text="Used by Breathing, Color Cycle, and Reactive effects:").pack(anchor=tk.W)
            
            self.effect_color = [0, 255, 0]  # Default green
            self.effect_color_preview = tk.Canvas(color_frame, width=200, height=40, bg='#00ff00', highlightthickness=1)
            self.effect_color_preview.pack(pady=5)
            
            ttk.Button(color_frame, text="🎨 Pick Effect Color...", command=self.pick_effect_color).pack(fill=tk.X, pady=2)
            
            # Quick colors
            quick_frame = ttk.Frame(color_frame)
            quick_frame.pack(fill=tk.X, pady=5)
            
            quick_colors = [
                ('#ff0000', 'Red'), ('#00ff00', 'Green'), ('#0000ff', 'Blue'), ('#ffffff', 'White'),
                ('#ffff00', 'Yellow'), ('#ff00ff', 'Magenta'), ('#00ffff', 'Cyan'), ('#ff8000', 'Orange'),
            ]
            
            for i, (color, label) in enumerate(quick_colors):
                btn = tk.Button(
                    quick_frame, bg=color, width=8, height=1,
                    text=label, fg='white' if color not in ['#ffffff', '#ffff00', '#00ffff'] else 'black',
                    command=lambda c=color: self.set_effect_color_hex(c)
                )
                btn.grid(row=i//4, column=i%4, padx=2, pady=2, sticky='nsew')
            
            for i in range(4):
                quick_frame.columnconfigure(i, weight=1)
            
            # Load current effect settings
            self.load_led_effect_settings()
        
        def load_led_effect_settings(self):
            """Load current LED effect settings from device."""
            try:
                mode = self.device.get_led_effect()
                if mode is not None:
                    self.effect_mode_var.set(mode)
                
                speed = self.device.get_led_effect_speed()
                if speed is not None:
                    self.effect_speed_var.set(speed)
                    self.effect_speed_label.config(text=str(speed))
                
                fps = self.device.get_led_fps_limit()
                if fps is not None:
                    self.fps_limit_var.set(fps)
                    self.fps_limit_label.config(text=f"{fps} FPS" if fps > 0 else "Unlimited")
            except Exception as e:
                self.status_var.set(f"❌ Error loading effect settings: {e}")
        
        def on_effect_mode_change(self):
            """Handle effect mode change."""
            mode = self.effect_mode_var.get()
            self.device.set_led_effect(mode)
            mode_names = ["None", "Rainbow", "Breathing", "Color Cycle", "Wave", "Reactive"]
            self.status_var.set(f"🎨 Effect mode set to {mode_names[mode]} - LIVE (not saved)")
        
        def on_effect_speed_change(self, value):
            """Handle effect speed change."""
            speed = int(float(value))
            self.effect_speed_label.config(text=str(speed))
            self.device.set_led_effect_speed(speed)
            self.status_var.set(f"⚡ Effect speed set to {speed} - LIVE (not saved)")
        
        def on_fps_limit_change(self, value):
            """Handle FPS limit change."""
            fps = int(float(value))
            self.fps_limit_label.config(text=f"{fps} FPS" if fps > 0 else "Unlimited")
            self.device.set_led_fps_limit(fps)
            self.status_var.set(f"🎬 FPS limit set to {fps if fps > 0 else 'Unlimited'} - LIVE (not saved)")
        
        def pick_effect_color(self):
            """Pick effect color."""
            color = colorchooser.askcolor(
                initialcolor=f'#{self.effect_color[0]:02x}{self.effect_color[1]:02x}{self.effect_color[2]:02x}',
                title="Choose Effect Color"
            )
            if color[0]:
                self.effect_color = [int(c) for c in color[0]]
                self.effect_color_preview.config(bg=color[1])
                self.device.set_led_effect_color(*self.effect_color)
                self.status_var.set(f"🎨 Effect color set to RGB({self.effect_color[0]}, {self.effect_color[1]}, {self.effect_color[2]}) - LIVE (not saved)")
        
        def set_effect_color_hex(self, hex_color):
            """Set effect color from hex."""
            hex_color = hex_color.lstrip('#')
            self.effect_color = [int(hex_color[i:i+2], 16) for i in (0, 2, 4)]
            self.effect_color_preview.config(bg=f'#{hex_color}')
            self.device.set_led_effect_color(*self.effect_color)
            self.status_var.set(f"🎨 Effect color set - LIVE (not saved)")
        
        # ===================================================================
        # Key Settings Tab
        # ===================================================================
        
        def create_key_settings_widgets(self, parent):
            """Create Key Settings tab widgets."""
            
            # Info banner
            ttk.Label(parent, text="⌨️ Configure per-key settings: actuation point, rapid trigger, SOCD, and keycodes",
                      foreground="blue").pack(anchor=tk.W, pady=(0, 10))
            
            # Key selector
            selector_frame = ttk.Frame(parent)
            selector_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(selector_frame, text="Select Key:", font=('Arial', 10, 'bold')).pack(side=tk.LEFT)
            
            self.selected_key_var = tk.IntVar(value=0)
            for i in range(6):
                rb = ttk.Radiobutton(
                    selector_frame, text=f"Key {i+1}",
                    variable=self.selected_key_var, value=i,
                    command=lambda idx=i: self.load_selected_key_settings(idx)
                )
                rb.pack(side=tk.LEFT, padx=10)
            
            # Create scrollable container for settings
            canvas = tk.Canvas(parent, highlightthickness=0)
            scrollbar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
            scrollable_frame = ttk.Frame(canvas)
            
            scrollable_frame.bind(
                "<Configure>",
                lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
            )
            
            canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
            canvas.configure(yscrollcommand=scrollbar.set)
            
            canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, pady=5)
            scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
            
            # === HID Keycode ===
            keycode_frame = ttk.LabelFrame(scrollable_frame, text="🔤 HID Keycode", padding="10")
            keycode_frame.pack(fill=tk.X, pady=5, padx=5)
            
            keycode_inner = ttk.Frame(keycode_frame)
            keycode_inner.pack(fill=tk.X, pady=5)
            
            ttk.Label(keycode_inner, text="Keycode:", width=15).pack(side=tk.LEFT)
            self.key_hid_keycode_var = tk.StringVar(value="Q")
            keycode_combo = ttk.Combobox(
                keycode_inner, textvariable=self.key_hid_keycode_var,
                values=list(HID_KEYCODES.keys()), width=15
            )
            keycode_combo.pack(side=tk.LEFT, padx=5)
            
            # === Fixed Actuation Settings (when Rapid Trigger disabled) ===
            fixed_frame = ttk.LabelFrame(scrollable_frame, text="📍 Fixed Actuation (when Rapid Trigger disabled)", padding="10")
            fixed_frame.pack(fill=tk.X, pady=5, padx=5)
            
            # Actuation Point
            actuation_row = ttk.Frame(fixed_frame)
            actuation_row.pack(fill=tk.X, pady=3)
            
            ttk.Label(actuation_row, text="Actuation Point:", width=20).pack(side=tk.LEFT)
            self.key_actuation_var = tk.DoubleVar(value=2.0)
            actuation_slider = ttk.Scale(
                actuation_row, from_=0.1, to=4.0,
                variable=self.key_actuation_var,
                orient=tk.HORIZONTAL, length=200,
                command=lambda v: self.key_actuation_label.config(text=f"{float(v):.1f}mm")
            )
            actuation_slider.pack(side=tk.LEFT, padx=5)
            self.key_actuation_label = ttk.Label(actuation_row, text="2.0mm", width=8)
            self.key_actuation_label.pack(side=tk.LEFT)
            
            # Release Point
            release_row = ttk.Frame(fixed_frame)
            release_row.pack(fill=tk.X, pady=3)
            
            ttk.Label(release_row, text="Release Point:", width=20).pack(side=tk.LEFT)
            self.key_release_var = tk.DoubleVar(value=1.8)
            release_slider = ttk.Scale(
                release_row, from_=0.1, to=4.0,
                variable=self.key_release_var,
                orient=tk.HORIZONTAL, length=200,
                command=lambda v: self.key_release_label.config(text=f"{float(v):.1f}mm")
            )
            release_slider.pack(side=tk.LEFT, padx=5)
            self.key_release_label = ttk.Label(release_row, text="1.8mm", width=8)
            self.key_release_label.pack(side=tk.LEFT)
            
            ttk.Label(fixed_frame, text="💡 Key activates at Actuation Point, releases at Release Point", 
                      foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)
            
            # === Rapid Trigger Settings ===
            rapid_frame = ttk.LabelFrame(scrollable_frame, text="⚡ Rapid Trigger", padding="10")
            rapid_frame.pack(fill=tk.X, pady=5, padx=5)
            
            # Enable checkbox
            self.key_rapid_enabled_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                rapid_frame, text="Enable Rapid Trigger for this key",
                variable=self.key_rapid_enabled_var,
                command=self.on_rapid_trigger_toggle
            ).pack(anchor=tk.W)
            
            # Rapid trigger sub-frame (visible when enabled)
            self.rapid_settings_frame = ttk.Frame(rapid_frame)
            self.rapid_settings_frame.pack(fill=tk.X, pady=5)
            
            # Activation Distance
            activation_row = ttk.Frame(self.rapid_settings_frame)
            activation_row.pack(fill=tk.X, pady=3)
            
            ttk.Label(activation_row, text="Activation Distance:", width=20).pack(side=tk.LEFT)
            self.key_rapid_activation_var = tk.DoubleVar(value=0.5)
            activation_slider = ttk.Scale(
                activation_row, from_=0.1, to=2.0,
                variable=self.key_rapid_activation_var,
                orient=tk.HORIZONTAL, length=200,
                command=lambda v: self.key_rapid_activation_label.config(text=f"{float(v):.2f}mm")
            )
            activation_slider.pack(side=tk.LEFT, padx=5)
            self.key_rapid_activation_label = ttk.Label(activation_row, text="0.50mm", width=8)
            self.key_rapid_activation_label.pack(side=tk.LEFT)
            
            # Press Sensitivity
            press_row = ttk.Frame(self.rapid_settings_frame)
            press_row.pack(fill=tk.X, pady=3)
            
            ttk.Label(press_row, text="Press Sensitivity:", width=20).pack(side=tk.LEFT)
            self.key_rapid_press_var = tk.DoubleVar(value=0.3)
            press_slider = ttk.Scale(
                press_row, from_=0.1, to=1.0,
                variable=self.key_rapid_press_var,
                orient=tk.HORIZONTAL, length=200,
                command=lambda v: self.key_rapid_press_label.config(text=f"{float(v):.2f}mm")
            )
            press_slider.pack(side=tk.LEFT, padx=5)
            self.key_rapid_press_label = ttk.Label(press_row, text="0.30mm", width=8)
            self.key_rapid_press_label.pack(side=tk.LEFT)
            
            # Separate press/release sensitivity option
            self.key_separate_sensitivity_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                self.rapid_settings_frame, text="Use separate Press/Release sensitivity",
                variable=self.key_separate_sensitivity_var,
                command=self.on_separate_sensitivity_toggle
            ).pack(anchor=tk.W, pady=3)
            
            # Release Sensitivity (visible when separate sensitivity enabled)
            self.release_sens_frame = ttk.Frame(self.rapid_settings_frame)
            
            ttk.Label(self.release_sens_frame, text="Release Sensitivity:", width=20).pack(side=tk.LEFT)
            self.key_rapid_release_var = tk.DoubleVar(value=0.3)
            release_sens_slider = ttk.Scale(
                self.release_sens_frame, from_=0.1, to=1.0,
                variable=self.key_rapid_release_var,
                orient=tk.HORIZONTAL, length=200,
                command=lambda v: self.key_rapid_release_label.config(text=f"{float(v):.2f}mm")
            )
            release_sens_slider.pack(side=tk.LEFT, padx=5)
            self.key_rapid_release_label = ttk.Label(self.release_sens_frame, text="0.30mm", width=8)
            self.key_rapid_release_label.pack(side=tk.LEFT)
            
            ttk.Label(rapid_frame, text="💡 Rapid trigger activates when key moves down by sensitivity amount after initial activation", 
                      foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)
            
            # Initialize visibility
            self.on_rapid_trigger_toggle()
            
            # === SOCD Settings ===
            socd_frame = ttk.LabelFrame(scrollable_frame, text="🔀 SOCD (Simultaneous Opposing Cardinal Directions)", padding="10")
            socd_frame.pack(fill=tk.X, pady=5, padx=5)
            
            socd_row = ttk.Frame(socd_frame)
            socd_row.pack(fill=tk.X, pady=5)
            
            ttk.Label(socd_row, text="Paired Key:", width=15).pack(side=tk.LEFT)
            self.key_socd_var = tk.StringVar(value="None")
            socd_combo = ttk.Combobox(
                socd_row, textvariable=self.key_socd_var,
                values=["None", "Key 1", "Key 2", "Key 3", "Key 4", "Key 5", "Key 6"],
                width=10, state="readonly"
            )
            socd_combo.pack(side=tk.LEFT, padx=5)
            
            ttk.Label(socd_frame, text="💡 When both keys pressed, last pressed wins (Last Input Priority)", 
                      foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)
            
            # === Gamepad Options ===
            gamepad_key_frame = ttk.LabelFrame(scrollable_frame, text="🎮 Per-Key Gamepad Options", padding="10")
            gamepad_key_frame.pack(fill=tk.X, pady=5, padx=5)
            
            self.key_disable_kb_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                gamepad_key_frame, text="Disable keyboard output when this key is in gamepad mode",
                variable=self.key_disable_kb_var
            ).pack(anchor=tk.W)
            
            # === Action Buttons ===
            button_frame = ttk.Frame(scrollable_frame)
            button_frame.pack(fill=tk.X, pady=10, padx=5)
            
            ttk.Button(button_frame, text="📥 Load Settings", command=self.load_selected_key_settings).pack(side=tk.LEFT, padx=5)
            ttk.Button(button_frame, text="📤 Apply Settings", command=self.apply_key_settings).pack(side=tk.LEFT, padx=5)
            ttk.Button(button_frame, text="🔄 Apply to All Keys", command=self.apply_to_all_keys).pack(side=tk.LEFT, padx=5)
            
            # Load initial
            self.load_selected_key_settings()
        
        def on_rapid_trigger_toggle(self):
            """Toggle rapid trigger settings visibility."""
            if self.key_rapid_enabled_var.get():
                self.rapid_settings_frame.pack(fill=tk.X, pady=5)
            else:
                self.rapid_settings_frame.pack_forget()
        
        def on_separate_sensitivity_toggle(self):
            """Toggle separate release sensitivity visibility."""
            if self.key_separate_sensitivity_var.get():
                self.release_sens_frame.pack(fill=tk.X, pady=3)
            else:
                self.release_sens_frame.pack_forget()
        
        def load_selected_key_settings(self, key_idx=None):
            """Load settings for the selected key."""
            if key_idx is None:
                key_idx = self.selected_key_var.get()
            try:
                settings = self.device.get_key_settings(key_idx)
                if settings:
                    # Find keycode name
                    keycode = settings.get('hid_keycode', 0x14)
                    keyname = HID_KEYCODE_NAMES.get(keycode, 'Q')
                    self.key_hid_keycode_var.set(keyname)
                    
                    # Fixed actuation settings
                    actuation_mm = settings.get('actuation_point_mm', 2.0)
                    release_mm = settings.get('release_point_mm', 1.8)
                    self.key_actuation_var.set(actuation_mm)
                    self.key_release_var.set(release_mm)
                    self.key_actuation_label.config(text=f"{actuation_mm:.1f}mm")
                    self.key_release_label.config(text=f"{release_mm:.1f}mm")
                    
                    # Rapid trigger settings
                    rapid_enabled = settings.get('rapid_trigger_enabled', False)
                    self.key_rapid_enabled_var.set(rapid_enabled)
                    self.on_rapid_trigger_toggle()
                    
                    rapid_activation = settings.get('rapid_trigger_activation', 0.5)
                    rapid_press = settings.get('rapid_trigger_press', 0.3)
                    rapid_release = settings.get('rapid_trigger_release', 0.3)
                    
                    self.key_rapid_activation_var.set(rapid_activation)
                    self.key_rapid_press_var.set(rapid_press)
                    self.key_rapid_release_var.set(rapid_release)
                    self.key_rapid_activation_label.config(text=f"{rapid_activation:.2f}mm")
                    self.key_rapid_press_label.config(text=f"{rapid_press:.2f}mm")
                    self.key_rapid_release_label.config(text=f"{rapid_release:.2f}mm")
                    
                    # Separate sensitivity
                    separate = rapid_press != rapid_release
                    self.key_separate_sensitivity_var.set(separate)
                    self.on_separate_sensitivity_toggle()
                    
                    # SOCD
                    socd = settings.get('socd_pair')
                    if socd is not None and socd < 6:
                        self.key_socd_var.set(f"Key {socd + 1}")
                    else:
                        self.key_socd_var.set("None")
                    
                    # Disable KB on gamepad
                    self.key_disable_kb_var.set(settings.get('disable_kb_on_gamepad', False))
                    
                    self.status_var.set(f"📥 Loaded settings for Key {key_idx + 1}")
            except Exception as e:
                self.status_var.set(f"❌ Error loading key settings: {e}")
        
        def apply_key_settings(self):
            """Apply settings for the selected key."""
            key_idx = self.selected_key_var.get()
            try:
                keycode = HID_KEYCODES.get(self.key_hid_keycode_var.get(), 0x14)
                
                # Get SOCD pair
                socd_str = self.key_socd_var.get()
                if socd_str == "None":
                    socd = 255  # No pair
                else:
                    socd = int(socd_str.split()[1]) - 1
                
                # Build settings dict
                settings = {
                    'hid_keycode': keycode,
                    'actuation_point_mm': self.key_actuation_var.get(),
                    'release_point_mm': self.key_release_var.get(),
                    'rapid_trigger_enabled': self.key_rapid_enabled_var.get(),
                    'rapid_trigger_activation': self.key_rapid_activation_var.get(),
                    'rapid_trigger_press': self.key_rapid_press_var.get(),
                    'rapid_trigger_release': self.key_rapid_release_var.get() if self.key_separate_sensitivity_var.get() else self.key_rapid_press_var.get(),
                    'socd_pair': socd,
                    'disable_kb_on_gamepad': self.key_disable_kb_var.get()
                }
                
                success = self.device.set_key_settings_extended(key_idx, settings)
                
                if success:
                    self.status_var.set(f"📤 Applied settings for Key {key_idx + 1} - LIVE (not saved)")
                else:
                    self.status_var.set(f"❌ Failed to apply key settings")
            except Exception as e:
                self.status_var.set(f"❌ Error applying key settings: {e}")
        
        def apply_to_all_keys(self):
            """Apply current settings to all keys."""
            try:
                for key_idx in range(6):
                    keycode = HID_KEYCODES.get(self.key_hid_keycode_var.get(), 0x14)
                    
                    socd_str = self.key_socd_var.get()
                    if socd_str == "None":
                        socd = 255
                    else:
                        socd = int(socd_str.split()[1]) - 1
                    
                    settings = {
                        'hid_keycode': keycode,
                        'actuation_point_mm': self.key_actuation_var.get(),
                        'release_point_mm': self.key_release_var.get(),
                        'rapid_trigger_enabled': self.key_rapid_enabled_var.get(),
                        'rapid_trigger_activation': self.key_rapid_activation_var.get(),
                        'rapid_trigger_press': self.key_rapid_press_var.get(),
                        'rapid_trigger_release': self.key_rapid_release_var.get() if self.key_separate_sensitivity_var.get() else self.key_rapid_press_var.get(),
                        'socd_pair': socd,
                        'disable_kb_on_gamepad': self.key_disable_kb_var.get()
                    }
                    
                    self.device.set_key_settings_extended(key_idx, settings)
                
                self.status_var.set(f"📤 Applied settings to all 6 keys - LIVE (not saved)")
            except Exception as e:
                self.status_var.set(f"❌ Error applying to all keys: {e}")
        
        # ===================================================================
        # Gamepad Settings Tab
        # ===================================================================
        
        def create_gamepad_settings_widgets(self, parent):
            """Create Gamepad Settings tab widgets."""
            
            # Create a scrollable container
            canvas = tk.Canvas(parent, highlightthickness=0)
            scrollbar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
            scrollable_frame = ttk.Frame(canvas)
            
            scrollable_frame.bind(
                "<Configure>",
                lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
            )
            
            canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
            canvas.configure(yscrollcommand=scrollbar.set)
            
            canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
            
            # Info banner
            ttk.Label(scrollable_frame, text="🎮 Configure gamepad analog behavior and per-key mapping",
                      foreground="blue").pack(anchor=tk.W, pady=(0, 10), padx=5)
            
            # Two-column layout
            main_frame = ttk.Frame(scrollable_frame)
            main_frame.pack(fill=tk.BOTH, expand=True)
            
            left_col = ttk.Frame(main_frame)
            left_col.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)
            
            right_col = ttk.Frame(main_frame)
            right_col.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)
            
            # === LEFT COLUMN: Global Settings ===
            
            # Deadzone
            deadzone_frame = ttk.LabelFrame(left_col, text="📏 Deadzone", padding="10")
            deadzone_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(deadzone_frame, text="Deadzone (0-50%):").pack(anchor=tk.W)
            self.gamepad_deadzone_var = tk.IntVar(value=10)
            deadzone_slider = ttk.Scale(
                deadzone_frame, from_=0, to=50,
                variable=self.gamepad_deadzone_var,
                orient=tk.HORIZONTAL,
                command=self.on_gamepad_deadzone_change
            )
            deadzone_slider.pack(fill=tk.X, padx=5)
            self.gamepad_deadzone_label = ttk.Label(deadzone_frame, text="10%", font=('Arial', 12))
            self.gamepad_deadzone_label.pack()
            
            # Curve Type
            curve_frame = ttk.LabelFrame(left_col, text="📈 Analog Curve", padding="10")
            curve_frame.pack(fill=tk.X, pady=5)
            
            self.gamepad_curve_var = tk.IntVar(value=0)
            
            curves = [
                (0, "Linear - Direct 1:1 mapping"),
                (1, "Smooth - Gentle acceleration"),
                (2, "Aggressive - Quick response")
            ]
            
            for val, text in curves:
                rb = ttk.Radiobutton(
                    curve_frame, text=text, variable=self.gamepad_curve_var,
                    value=val, command=self.apply_gamepad_settings
                )
                rb.pack(anchor=tk.W, pady=2)
            
            # Joystick Options
            options_frame = ttk.LabelFrame(left_col, text="🔲 Joystick Options", padding="10")
            options_frame.pack(fill=tk.X, pady=5)
            
            self.gamepad_square_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                options_frame, text="Square Mode - Circular to square",
                variable=self.gamepad_square_var,
                command=self.apply_gamepad_settings
            ).pack(anchor=tk.W, pady=2)
            
            self.gamepad_snappy_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                options_frame, text="Snappy Mode - Faster centering",
                variable=self.gamepad_snappy_var,
                command=self.apply_gamepad_settings
            ).pack(anchor=tk.W, pady=2)
            
            self.gamepad_kb_always_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                options_frame, text="Send keyboard along with gamepad",
                variable=self.gamepad_kb_always_var,
                command=self.apply_gamepad_settings
            ).pack(anchor=tk.W, pady=2)
            
            # === RIGHT COLUMN: Visualization ===
            
            viz_frame = ttk.LabelFrame(right_col, text="📺 Live Gamepad Preview", padding="10")
            viz_frame.pack(fill=tk.BOTH, expand=True, pady=5)
            
            # Gamepad visualization canvas
            self.gamepad_canvas = tk.Canvas(viz_frame, width=200, height=200, bg='#2d2d2d')
            self.gamepad_canvas.pack(pady=5)
            
            # Key axis display
            axis_frame = ttk.Frame(viz_frame)
            axis_frame.pack(fill=tk.X, pady=5)
            
            self.gamepad_axis_labels = []
            for i in range(6):
                frame = ttk.Frame(axis_frame)
                frame.pack(fill=tk.X, pady=1)
                ttk.Label(frame, text=f"Key {i+1}:", width=8).pack(side=tk.LEFT)
                bar = ttk.Progressbar(frame, length=100, maximum=100, mode='determinate')
                bar.pack(side=tk.LEFT, padx=5)
                label = ttk.Label(frame, text="0%", width=6)
                label.pack(side=tk.LEFT)
                self.gamepad_axis_labels.append((bar, label))
            
            # Start visualization update
            self.gamepad_viz_enabled = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                viz_frame, text="Enable live visualization",
                variable=self.gamepad_viz_enabled,
                command=self.toggle_gamepad_viz
            ).pack()
            
            # Draw initial state
            self.draw_gamepad_viz(0, 0)
            
            # === PER-KEY MAPPING ===
            
            mapping_frame = ttk.LabelFrame(scrollable_frame, text="🎯 Per-Key Gamepad Mapping", padding="10")
            mapping_frame.pack(fill=tk.X, pady=5, padx=5)
            
            ttk.Label(mapping_frame, text="Configure what gamepad input each key produces:", 
                      foreground="gray").pack(anchor=tk.W)
            
            # Mapping grid
            mapping_grid = ttk.Frame(mapping_frame)
            mapping_grid.pack(fill=tk.X, pady=5)
            
            # Headers
            ttk.Label(mapping_grid, text="Key", width=6, font=('Arial', 9, 'bold')).grid(row=0, column=0)
            ttk.Label(mapping_grid, text="Axis", width=12, font=('Arial', 9, 'bold')).grid(row=0, column=1)
            ttk.Label(mapping_grid, text="Direction", width=10, font=('Arial', 9, 'bold')).grid(row=0, column=2)
            ttk.Label(mapping_grid, text="Button", width=10, font=('Arial', 9, 'bold')).grid(row=0, column=3)
            
            self.key_gamepad_vars = []
            axis_options = ["Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "Trigger L", "Trigger R"]
            dir_options = ["+", "-"]
            button_options = ["None", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Select"]
            
            for i in range(6):
                ttk.Label(mapping_grid, text=f"Key {i+1}").grid(row=i+1, column=0, pady=2)
                
                axis_var = tk.StringVar(value=axis_options[i % len(axis_options)])
                axis_combo = ttk.Combobox(mapping_grid, textvariable=axis_var, 
                                          values=axis_options, width=12, state='readonly')
                axis_combo.grid(row=i+1, column=1, padx=2)
                
                dir_var = tk.StringVar(value=dir_options[i % 2])
                dir_combo = ttk.Combobox(mapping_grid, textvariable=dir_var, 
                                         values=dir_options, width=8, state='readonly')
                dir_combo.grid(row=i+1, column=2, padx=2)
                
                btn_var = tk.StringVar(value="None")
                btn_combo = ttk.Combobox(mapping_grid, textvariable=btn_var, 
                                         values=button_options, width=8, state='readonly')
                btn_combo.grid(row=i+1, column=3, padx=2)
                
                self.key_gamepad_vars.append((axis_var, dir_var, btn_var))
            
            # Buttons
            btn_frame = ttk.Frame(scrollable_frame)
            btn_frame.pack(fill=tk.X, pady=10, padx=5)
            
            ttk.Button(btn_frame, text="🔄 Reload Settings", 
                       command=self.load_gamepad_settings).pack(side=tk.LEFT, padx=5)
            ttk.Button(btn_frame, text="📤 Apply Mapping", 
                       command=self.apply_gamepad_mapping).pack(side=tk.LEFT, padx=5)
            
            # Load initial
            self.load_gamepad_settings()
        
        def draw_gamepad_viz(self, left_x, left_y):
            """Draw gamepad joystick visualization."""
            canvas = self.gamepad_canvas
            canvas.delete('all')
            
            w = canvas.winfo_width() or 200
            h = canvas.winfo_height() or 200
            
            # Center
            cx, cy = w // 2, h // 2
            radius = min(w, h) // 2 - 20
            
            # Draw outer circle (joystick boundary)
            canvas.create_oval(cx - radius, cy - radius, cx + radius, cy + radius, 
                               outline='#555', width=2)
            
            # Draw grid lines
            canvas.create_line(cx - radius, cy, cx + radius, cy, fill='#444')
            canvas.create_line(cx, cy - radius, cx, cy + radius, fill='#444')
            
            # Draw deadzone circle
            deadzone = self.gamepad_deadzone_var.get() / 100
            dz_radius = int(radius * deadzone)
            if dz_radius > 0:
                canvas.create_oval(cx - dz_radius, cy - dz_radius, 
                                   cx + dz_radius, cy + dz_radius, 
                                   outline='#800', width=1, dash=(2, 2))
            
            # Draw joystick position
            stick_x = cx + int(left_x * radius)
            stick_y = cy - int(left_y * radius)  # Y inverted
            
            # Stick indicator
            canvas.create_oval(stick_x - 10, stick_y - 10, stick_x + 10, stick_y + 10, 
                               fill='#0af', outline='white', width=2)
            
            # Labels
            canvas.create_text(cx, 10, text="Joystick", fill='white', font=('Arial', 9))
            x_pct = int(left_x * 100)
            y_pct = int(left_y * 100)
            canvas.create_text(cx, h - 10, text=f"X:{x_pct:+d}% Y:{y_pct:+d}%", fill='white', font=('Arial', 8))
        
        def toggle_gamepad_viz(self):
            """Toggle live gamepad visualization."""
            if self.gamepad_viz_enabled.get():
                self.update_gamepad_viz()
            
        def update_gamepad_viz(self):
            """Update gamepad visualization with current key states."""
            if not self.gamepad_viz_enabled.get():
                return
            
            try:
                # Get key states from device
                key_states = self.device.get_key_states()
                if key_states and 'distances' in key_states:
                    distances = key_states['distances']
                    
                    # Update axis bars
                    for i, (bar, label) in enumerate(self.gamepad_axis_labels):
                        if i < len(distances):
                            pct = int(distances[i] * 100 / 255)
                            bar['value'] = pct
                            label.config(text=f"{pct}%")
                    
                    # Calculate joystick position from keys
                    # Assuming key 0,2,4 are + and 1,3,5 are -
                    # Simple mapping: keys 0-3 for left stick
                    left_x = 0
                    left_y = 0
                    if len(distances) >= 4:
                        # Keys 0,1 = X axis, Keys 2,3 = Y axis
                        left_x = (distances[1] - distances[0]) / 255.0
                        left_y = (distances[3] - distances[2]) / 255.0
                    
                    self.draw_gamepad_viz(left_x, left_y)
            except Exception as e:
                pass
            
            # Schedule next update
            if self.gamepad_viz_enabled.get():
                self.after(50, self.update_gamepad_viz)
        
        def load_gamepad_settings(self):
            """Load gamepad settings from device."""
            try:
                settings = self.device.get_gamepad_settings()
                if settings:
                    self.gamepad_deadzone_var.set(settings.get('deadzone', 10))
                    self.gamepad_deadzone_label.config(text=f"{settings.get('deadzone', 10)}%")
                    self.gamepad_curve_var.set(settings.get('curve_type', 0))
                    self.gamepad_square_var.set(settings.get('square_mode', False))
                    self.gamepad_snappy_var.set(settings.get('snappy_mode', False))
                
                # Also load per-key gamepad mapping
                self.load_gamepad_mapping()
                
                # Load gamepad+keyboard mode
                gp_kb = self.device.get_gamepad_with_keyboard()
                if gp_kb is not None:
                    self.gamepad_kb_always_var.set(gp_kb)
                
                self.status_var.set("🔄 Loaded gamepad settings")
            except Exception as e:
                self.status_var.set(f"❌ Error loading gamepad settings: {e}")
        
        def on_gamepad_deadzone_change(self, value):
            """Handle deadzone slider change."""
            deadzone = int(float(value))
            self.gamepad_deadzone_label.config(text=f"{deadzone}%")
            self.apply_gamepad_settings()
        
        def apply_gamepad_settings(self):
            """Apply gamepad settings to device."""
            try:
                deadzone = self.gamepad_deadzone_var.get()
                curve = self.gamepad_curve_var.get()
                square = self.gamepad_square_var.get()
                snappy = self.gamepad_snappy_var.get()
                
                success = self.device.set_gamepad_settings(deadzone, curve, square, snappy)
                
                # Also send gamepad+keyboard mode
                gp_kb = self.gamepad_kb_always_var.get()
                self.device.set_gamepad_with_keyboard(gp_kb)
                
                if success:
                    self.status_var.set("🎮 Gamepad settings applied - LIVE (not saved)")
            except Exception as e:
                self.status_var.set(f"❌ Error applying gamepad settings: {e}")
        
        def apply_gamepad_mapping(self):
            """Apply per-key gamepad mapping to device."""
            if not self.device:
                self.status_var.set("❌ Not connected to device")
                return
            
            try:
                success_count = 0
                for i, (axis_var, dir_var, btn_var) in enumerate(self.key_gamepad_vars):
                    axis = axis_var.get()
                    direction = dir_var.get()
                    button = btn_var.get()
                    
                    success = self.device.set_key_gamepad_map(i, axis, direction, button)
                    if success:
                        success_count += 1
                
                if success_count == len(self.key_gamepad_vars):
                    self.status_var.set(f"✅ Gamepad mapping applied for all {success_count} keys")
                else:
                    self.status_var.set(f"⚠️ Gamepad mapping: {success_count}/{len(self.key_gamepad_vars)} keys updated")
            except Exception as e:
                self.status_var.set(f"❌ Error applying gamepad mapping: {e}")
        
        def load_gamepad_mapping(self):
            """Load per-key gamepad mapping from device."""
            if not self.device:
                return
            
            try:
                for i, (axis_var, dir_var, btn_var) in enumerate(self.key_gamepad_vars):
                    mapping = self.device.get_key_gamepad_map(i)
                    if mapping:
                        axis_var.set(mapping.get('axis', 0))
                        dir_var.set(mapping.get('direction', 0))
                        btn_var.set(mapping.get('button', 0))
            except Exception as e:
                print(f"Error loading gamepad mapping: {e}")
        
        # ===================================================================
        # Calibration Tab
        # ===================================================================
        
        def create_calibration_widgets(self, parent):
            """Create Calibration tab widgets."""
            
            # Info banner
            ttk.Label(parent, text="🔧 ADC Calibration - Calibrate sensor zero points",
                      foreground="blue").pack(anchor=tk.W, pady=(0, 10))
            
            # Manual calibration values entry
            manual_frame = ttk.LabelFrame(parent, text="📝 Manual Calibration Values", padding="10")
            manual_frame.pack(fill=tk.X, pady=5)
            
            # LUT Zero Value
            lut_frame = ttk.Frame(manual_frame)
            lut_frame.pack(fill=tk.X, pady=2)
            ttk.Label(lut_frame, text="LUT Zero Value:", width=20).pack(side=tk.LEFT)
            self.cal_lut_zero_entry = ttk.Entry(lut_frame, width=10)
            self.cal_lut_zero_entry.pack(side=tk.LEFT)
            self.cal_lut_zero_var = tk.StringVar(value="--")
            ttk.Label(lut_frame, textvariable=self.cal_lut_zero_var, font=('Consolas', 9), foreground='gray').pack(side=tk.LEFT, padx=10)
            
            # Key Zero Values
            self.cal_key_entries = []
            self.cal_key_vars = []
            for i in range(6):
                key_frame = ttk.Frame(manual_frame)
                key_frame.pack(fill=tk.X, pady=2)
                ttk.Label(key_frame, text=f"Key {i+1} Zero Value:", width=20).pack(side=tk.LEFT)
                entry = ttk.Entry(key_frame, width=10)
                entry.pack(side=tk.LEFT)
                self.cal_key_entries.append(entry)
                var = tk.StringVar(value="--")
                self.cal_key_vars.append(var)
                ttk.Label(key_frame, textvariable=var, font=('Consolas', 9), foreground='gray').pack(side=tk.LEFT, padx=10)
            
            # Manual buttons
            manual_btn_frame = ttk.Frame(manual_frame)
            manual_btn_frame.pack(fill=tk.X, pady=5)
            ttk.Button(manual_btn_frame, text="📥 Load from Device", 
                       command=self.load_calibration).pack(side=tk.LEFT, padx=5)
            ttk.Button(manual_btn_frame, text="📤 Apply Manual Values", 
                       command=self.apply_manual_calibration).pack(side=tk.LEFT, padx=5)
            
            # Auto-calibration
            auto_frame = ttk.LabelFrame(parent, text="🤖 Auto-Calibration", padding="10")
            auto_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(auto_frame, text="⚠️ Make sure all keys are fully released before calibrating!",
                      foreground="orange").pack(anchor=tk.W, pady=5)
            
            btn_frame = ttk.Frame(auto_frame)
            btn_frame.pack(fill=tk.X, pady=5)
            
            ttk.Button(btn_frame, text="🔧 Calibrate All Keys", 
                       command=lambda: self.auto_calibrate(0xFF)).pack(side=tk.LEFT, padx=5)
            
            # Individual key calibration
            ttk.Label(auto_frame, text="Calibrate individual key:").pack(anchor=tk.W, pady=(10, 5))
            
            key_btn_frame = ttk.Frame(auto_frame)
            key_btn_frame.pack(fill=tk.X)
            
            for i in range(6):
                ttk.Button(key_btn_frame, text=f"Key {i+1}", width=8,
                           command=lambda idx=i: self.auto_calibrate(idx)).pack(side=tk.LEFT, padx=2)
            
            # Analog Curve Builder
            self.create_curve_builder_frame(parent)
            
            # Load initial
            self.load_calibration()
        
        def load_calibration(self):
            """Load calibration values from device."""
            try:
                cal = self.device.get_calibration()
                if cal:
                    lut_zero = cal.get('lut_zero_value', 0)
                    self.cal_lut_zero_var.set(f"(Current: {lut_zero})")
                    self.cal_lut_zero_entry.delete(0, tk.END)
                    self.cal_lut_zero_entry.insert(0, str(lut_zero))
                    
                    key_zeros = cal.get('key_zero_values', [])
                    for i, var in enumerate(self.cal_key_vars):
                        if i < len(key_zeros):
                            var.set(f"(Current: {key_zeros[i]})")
                            self.cal_key_entries[i].delete(0, tk.END)
                            self.cal_key_entries[i].insert(0, str(key_zeros[i]))
                        else:
                            var.set("--")
                    self.status_var.set("🔄 Loaded calibration values")
            except Exception as e:
                self.status_var.set(f"❌ Error loading calibration: {e}")
        
        def apply_manual_calibration(self):
            """Apply manually entered calibration values."""
            try:
                lut_zero = int(self.cal_lut_zero_entry.get())
                key_zeros = []
                for entry in self.cal_key_entries:
                    key_zeros.append(int(entry.get()))
                
                success = self.device.set_calibration(lut_zero, key_zeros)
                if success:
                    self.load_calibration()  # Refresh display
                    self.status_var.set("✅ Manual calibration values applied - LIVE (not saved)")
                else:
                    self.status_var.set("❌ Failed to apply calibration values")
            except ValueError as e:
                self.status_var.set(f"❌ Invalid value: {e}")
            except Exception as e:
                self.status_var.set(f"❌ Error applying calibration: {e}")
            except Exception as e:
                self.status_var.set(f"❌ Error loading calibration: {e}")
        
        def auto_calibrate(self, key_index):
            """Auto-calibrate a key or all keys."""
            try:
                if key_index == 0xFF:
                    msg = "all keys"
                else:
                    msg = f"Key {key_index + 1}"
                
                self.status_var.set(f"🔧 Calibrating {msg}...")
                self.update()
                
                result = self.device.auto_calibrate(key_index)
                if result:
                    self.load_calibration()
                    self.status_var.set(f"✅ Calibrated {msg} - LIVE (not saved)")
                else:
                    self.status_var.set(f"❌ Failed to calibrate {msg}")
            except Exception as e:
                self.status_var.set(f"❌ Error during calibration: {e}")
        
        # ===================================================================
        # Analog Curve Builder
        # ===================================================================
        
        def create_curve_builder_frame(self, parent):
            """Create a 4-point bezier curve builder widget."""
            
            # Main frame for curve builder
            curve_frame = ttk.LabelFrame(parent, text="📈 Analog Response Curve", padding="10")
            curve_frame.pack(fill=tk.BOTH, expand=True, pady=5)
            
            # Canvas for drawing the curve
            self.curve_canvas = tk.Canvas(curve_frame, width=300, height=300, bg='white', 
                                          highlightthickness=1, highlightbackground='gray')
            self.curve_canvas.pack(side=tk.LEFT, padx=5)
            
            # Control points (normalized 0-1)
            # P0 = start (0,0), P3 = end (1,1), P1 and P2 are control points
            self.curve_points = [
                (0.0, 0.0),   # P0 - start (fixed)
                (0.3, 0.0),   # P1 - control point 1
                (0.7, 1.0),   # P2 - control point 2
                (1.0, 1.0)    # P3 - end (fixed)
            ]
            
            # Draw handles tracking
            self.dragging_point = None
            
            # Bind mouse events
            self.curve_canvas.bind('<Button-1>', self.on_curve_click)
            self.curve_canvas.bind('<B1-Motion>', self.on_curve_drag)
            self.curve_canvas.bind('<ButtonRelease-1>', self.on_curve_release)
            
            # Control panel
            control_panel = ttk.Frame(curve_frame)
            control_panel.pack(side=tk.LEFT, fill=tk.Y, padx=10)
            
            ttk.Label(control_panel, text="Control Points:", font=('Arial', 10, 'bold')).pack(anchor=tk.W)
            
            # Preset curves
            preset_frame = ttk.LabelFrame(control_panel, text="Presets", padding="5")
            preset_frame.pack(fill=tk.X, pady=5)
            
            ttk.Button(preset_frame, text="Linear", 
                       command=lambda: self.set_curve_preset('linear')).pack(fill=tk.X, pady=1)
            ttk.Button(preset_frame, text="Smooth", 
                       command=lambda: self.set_curve_preset('smooth')).pack(fill=tk.X, pady=1)
            ttk.Button(preset_frame, text="Aggressive", 
                       command=lambda: self.set_curve_preset('aggressive')).pack(fill=tk.X, pady=1)
            ttk.Button(preset_frame, text="Delayed", 
                       command=lambda: self.set_curve_preset('delayed')).pack(fill=tk.X, pady=1)
            
            # Manual P1/P2 entry
            manual_frame = ttk.LabelFrame(control_panel, text="Manual Entry", padding="5")
            manual_frame.pack(fill=tk.X, pady=5)
            
            # P1
            p1_frame = ttk.Frame(manual_frame)
            p1_frame.pack(fill=tk.X, pady=2)
            ttk.Label(p1_frame, text="P1 X:", width=6).pack(side=tk.LEFT)
            self.curve_p1x_var = tk.DoubleVar(value=0.3)
            ttk.Spinbox(p1_frame, from_=0, to=1, increment=0.05, width=6, 
                        textvariable=self.curve_p1x_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)
            ttk.Label(p1_frame, text="Y:", width=3).pack(side=tk.LEFT)
            self.curve_p1y_var = tk.DoubleVar(value=0.0)
            ttk.Spinbox(p1_frame, from_=0, to=1, increment=0.05, width=6, 
                        textvariable=self.curve_p1y_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)
            
            # P2
            p2_frame = ttk.Frame(manual_frame)
            p2_frame.pack(fill=tk.X, pady=2)
            ttk.Label(p2_frame, text="P2 X:", width=6).pack(side=tk.LEFT)
            self.curve_p2x_var = tk.DoubleVar(value=0.7)
            ttk.Spinbox(p2_frame, from_=0, to=1, increment=0.05, width=6, 
                        textvariable=self.curve_p2x_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)
            ttk.Label(p2_frame, text="Y:", width=3).pack(side=tk.LEFT)
            self.curve_p2y_var = tk.DoubleVar(value=1.0)
            ttk.Spinbox(p2_frame, from_=0, to=1, increment=0.05, width=6, 
                        textvariable=self.curve_p2y_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)
            
            # Apply button
            ttk.Button(control_panel, text="📤 Apply Curve to Device", 
                       command=self.apply_analog_curve).pack(fill=tk.X, pady=10)
            
            ttk.Label(control_panel, text="💡 Drag the red dots to\nadjust the curve shape", 
                      foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)
            
            # Draw initial curve
            self.draw_curve()
        
        def set_curve_preset(self, preset):
            """Set curve to a preset shape."""
            presets = {
                'linear': [(0, 0), (0.33, 0.33), (0.67, 0.67), (1, 1)],
                'smooth': [(0, 0), (0.4, 0.0), (0.6, 1.0), (1, 1)],
                'aggressive': [(0, 0), (0.7, 0.0), (0.3, 1.0), (1, 1)],
                'delayed': [(0, 0), (0.1, 0.0), (0.9, 1.0), (1, 1)]
            }
            if preset in presets:
                self.curve_points = list(presets[preset])
                self.curve_p1x_var.set(self.curve_points[1][0])
                self.curve_p1y_var.set(self.curve_points[1][1])
                self.curve_p2x_var.set(self.curve_points[2][0])
                self.curve_p2y_var.set(self.curve_points[2][1])
                self.draw_curve()
        
        def update_curve_from_inputs(self, *args):
            """Update curve from manual input fields."""
            try:
                p1x = self.curve_p1x_var.get()
                p1y = self.curve_p1y_var.get()
                p2x = self.curve_p2x_var.get()
                p2y = self.curve_p2y_var.get()
                self.curve_points[1] = (max(0, min(1, p1x)), max(0, min(1, p1y)))
                self.curve_points[2] = (max(0, min(1, p2x)), max(0, min(1, p2y)))
                self.draw_curve()
            except:
                pass
        
        def draw_curve(self):
            """Draw the bezier curve and control points."""
            canvas = self.curve_canvas
            canvas.delete('all')
            
            w = canvas.winfo_width() or 300
            h = canvas.winfo_height() or 300
            margin = 30
            
            # Drawing area
            x0, y0 = margin, margin
            x1, y1 = w - margin, h - margin
            
            # Draw grid
            for i in range(5):
                t = i / 4
                gx = x0 + t * (x1 - x0)
                gy = y0 + t * (y1 - y0)
                canvas.create_line(gx, y0, gx, y1, fill='#ddd')
                canvas.create_line(x0, gy, x1, gy, fill='#ddd')
            
            # Draw axes
            canvas.create_line(x0, y1, x1, y1, fill='black', width=2)  # X axis
            canvas.create_line(x0, y1, x0, y0, fill='black', width=2)  # Y axis
            
            # Labels
            canvas.create_text(x0 - 15, y1, text="0", font=('Arial', 8))
            canvas.create_text(x1, y1 + 15, text="Input", font=('Arial', 8))
            canvas.create_text(x0 - 15, y0, text="1", font=('Arial', 8))
            canvas.create_text(x0, y0 - 10, text="Output", font=('Arial', 8))
            
            # Draw bezier curve
            points = []
            for t in range(101):
                t_norm = t / 100.0
                # Cubic bezier formula
                p0, p1, p2, p3 = self.curve_points
                x = ((1-t_norm)**3 * p0[0] + 
                     3*(1-t_norm)**2*t_norm * p1[0] + 
                     3*(1-t_norm)*t_norm**2 * p2[0] + 
                     t_norm**3 * p3[0])
                y = ((1-t_norm)**3 * p0[1] + 
                     3*(1-t_norm)**2*t_norm * p1[1] + 
                     3*(1-t_norm)*t_norm**2 * p2[1] + 
                     t_norm**3 * p3[1])
                # Convert to canvas coords (y inverted)
                cx = x0 + x * (x1 - x0)
                cy = y1 - y * (y1 - y0)
                points.append((cx, cy))
            
            # Draw curve line
            for i in range(len(points) - 1):
                canvas.create_line(points[i][0], points[i][1], 
                                   points[i+1][0], points[i+1][1], 
                                   fill='blue', width=2)
            
            # Draw control point lines
            p0_canvas = (x0 + self.curve_points[0][0] * (x1 - x0), 
                         y1 - self.curve_points[0][1] * (y1 - y0))
            p1_canvas = (x0 + self.curve_points[1][0] * (x1 - x0), 
                         y1 - self.curve_points[1][1] * (y1 - y0))
            p2_canvas = (x0 + self.curve_points[2][0] * (x1 - x0), 
                         y1 - self.curve_points[2][1] * (y1 - y0))
            p3_canvas = (x0 + self.curve_points[3][0] * (x1 - x0), 
                         y1 - self.curve_points[3][1] * (y1 - y0))
            
            canvas.create_line(p0_canvas[0], p0_canvas[1], p1_canvas[0], p1_canvas[1], 
                               fill='red', dash=(2, 2))
            canvas.create_line(p3_canvas[0], p3_canvas[1], p2_canvas[0], p2_canvas[1], 
                               fill='red', dash=(2, 2))
            
            # Draw control points
            r = 6
            # P1 (draggable)
            canvas.create_oval(p1_canvas[0]-r, p1_canvas[1]-r, 
                               p1_canvas[0]+r, p1_canvas[1]+r, 
                               fill='red', outline='darkred', tags='p1')
            # P2 (draggable)
            canvas.create_oval(p2_canvas[0]-r, p2_canvas[1]-r, 
                               p2_canvas[0]+r, p2_canvas[1]+r, 
                               fill='red', outline='darkred', tags='p2')
            # P0 and P3 (fixed)
            canvas.create_oval(p0_canvas[0]-4, p0_canvas[1]-4, 
                               p0_canvas[0]+4, p0_canvas[1]+4, 
                               fill='gray', outline='black')
            canvas.create_oval(p3_canvas[0]-4, p3_canvas[1]-4, 
                               p3_canvas[0]+4, p3_canvas[1]+4, 
                               fill='gray', outline='black')
        
        def on_curve_click(self, event):
            """Handle click on curve canvas."""
            w = self.curve_canvas.winfo_width()
            h = self.curve_canvas.winfo_height()
            margin = 30
            x0, y0 = margin, margin
            x1, y1 = w - margin, h - margin
            
            # Check if clicking on P1 or P2
            for i, pt_tag in [(1, 'p1'), (2, 'p2')]:
                px = x0 + self.curve_points[i][0] * (x1 - x0)
                py = y1 - self.curve_points[i][1] * (y1 - y0)
                if abs(event.x - px) < 10 and abs(event.y - py) < 10:
                    self.dragging_point = i
                    return
        
        def on_curve_drag(self, event):
            """Handle drag on curve canvas."""
            if self.dragging_point is None:
                return
            
            w = self.curve_canvas.winfo_width()
            h = self.curve_canvas.winfo_height()
            margin = 30
            x0, y0 = margin, margin
            x1, y1 = w - margin, h - margin
            
            # Convert canvas coords to normalized coords
            nx = (event.x - x0) / (x1 - x0)
            ny = (y1 - event.y) / (y1 - y0)
            
            # Clamp to 0-1
            nx = max(0, min(1, nx))
            ny = max(0, min(1, ny))
            
            self.curve_points[self.dragging_point] = (nx, ny)
            
            # Update input fields
            if self.dragging_point == 1:
                self.curve_p1x_var.set(round(nx, 2))
                self.curve_p1y_var.set(round(ny, 2))
            elif self.dragging_point == 2:
                self.curve_p2x_var.set(round(nx, 2))
                self.curve_p2y_var.set(round(ny, 2))
            
            self.draw_curve()
        
        def on_curve_release(self, event):
            """Handle mouse release on curve canvas."""
            self.dragging_point = None
        
        def apply_analog_curve(self):
            """Apply the analog curve to the device for the selected key."""
            if not self.device:
                self.status_var.set("❌ Not connected to device")
                return
            
            try:
                # Get the currently selected key from key_var
                key_index = self.key_var.get()
                
                # Convert normalized curve points to 0-255 range
                p1 = self.curve_points[1]
                p2 = self.curve_points[2]
                p1_x = int(p1[0] * 255)
                p1_y = int(p1[1] * 255)
                p2_x = int(p2[0] * 255)
                p2_y = int(p2[1] * 255)
                
                # Enable curve and send to device
                success = self.device.set_key_curve(key_index, True, p1_x, p1_y, p2_x, p2_y)
                
                if success:
                    self.status_var.set(f"✅ Applied curve to Key {key_index}: P1({p1_x},{p1_y}) P2({p2_x},{p2_y})")
                else:
                    self.status_var.set(f"❌ Failed to apply curve to Key {key_index}")
            except Exception as e:
                self.status_var.set(f"❌ Error applying curve: {e}")
        
        def load_key_curve(self, key_index):
            """Load curve from device for the specified key."""
            if not self.device:
                return
            
            try:
                curve = self.device.get_key_curve(key_index)
                if curve:
                    # Convert 0-255 to normalized 0-1
                    p1_x = curve['p1_x'] / 255.0
                    p1_y = curve['p1_y'] / 255.0
                    p2_x = curve['p2_x'] / 255.0
                    p2_y = curve['p2_y'] / 255.0
                    
                    self.curve_points[1] = (p1_x, p1_y)
                    self.curve_points[2] = (p2_x, p2_y)
                    
                    # Update input fields
                    self.curve_p1x_var.set(round(p1_x, 2))
                    self.curve_p1y_var.set(round(p1_y, 2))
                    self.curve_p2x_var.set(round(p2_x, 2))
                    self.curve_p2y_var.set(round(p2_y, 2))
                    
                    self.draw_curve()
            except Exception as e:
                print(f"Error loading curve: {e}")
        
        # ===================================================================
        # Live Graph Tab
        # ===================================================================
        
        def create_graph_widgets(self, parent):
            """Create Live Graph tab widgets."""
            
            # Info banner
            ttk.Label(parent, text="📊 Live data visualization - ADC values, key states, and analog axes",
                      foreground="blue").pack(anchor=tk.W, pady=(0, 10))
            
            # Controls frame
            control_frame = ttk.Frame(parent)
            control_frame.pack(fill=tk.X, pady=5)
            
            self.graph_live_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(
                control_frame, text="📊 Enable Live Graph",
                variable=self.graph_live_var,
                command=self.toggle_graph_update
            ).pack(side=tk.LEFT)
            
            ttk.Label(control_frame, text="  Data Points:").pack(side=tk.LEFT)
            self.graph_points_var = tk.IntVar(value=200)
            ttk.Spinbox(control_frame, from_=50, to=1000, width=6,
                        textvariable=self.graph_points_var).pack(side=tk.LEFT, padx=5)
            
            ttk.Label(control_frame, text="  Update (ms):").pack(side=tk.LEFT)
            self.graph_update_var = tk.IntVar(value=50)
            ttk.Spinbox(control_frame, from_=10, to=500, width=5,
                        textvariable=self.graph_update_var).pack(side=tk.LEFT, padx=5)
            
            ttk.Button(control_frame, text="🗑️ Clear", command=self.clear_graph_data).pack(side=tk.LEFT, padx=5)
            
            # Data type selection
            dtype_frame = ttk.LabelFrame(parent, text="📈 Data Type", padding="5")
            dtype_frame.pack(fill=tk.X, pady=5)
            
            self.graph_dtype_var = tk.StringVar(value='adc')
            dtypes = [
                ('adc', 'ADC Raw (2000-2700)'),
                ('distance', 'Distance (0-4mm)'),
                ('normalized', 'Normalized (0-100%)')
            ]
            for val, text in dtypes:
                rb = ttk.Radiobutton(dtype_frame, text=text, variable=self.graph_dtype_var, 
                                     value=val, command=self.on_graph_dtype_change)
                rb.pack(side=tk.LEFT, padx=10)
            
            # Channel selection
            channel_frame = ttk.LabelFrame(parent, text="📡 Channels", padding="5")
            channel_frame.pack(fill=tk.X, pady=5)
            
            self.graph_channel_vars = []
            colors = ['#ff0000', '#00ff00', '#0000ff', '#ffff00', '#ff00ff', '#00ffff']
            for i in range(6):
                var = tk.BooleanVar(value=True)
                self.graph_channel_vars.append(var)
                cb = ttk.Checkbutton(channel_frame, text=f"Key {i+1}", variable=var)
                cb.pack(side=tk.LEFT, padx=5)
                # Color indicator
                color_label = tk.Label(channel_frame, text="●", fg=colors[i], font=('Arial', 12))
                color_label.pack(side=tk.LEFT)
            
            # Zoom and pan controls
            zoom_frame = ttk.LabelFrame(parent, text="🔍 View Controls", padding="5")
            zoom_frame.pack(fill=tk.X, pady=5)
            
            ttk.Label(zoom_frame, text="Y-Range:").pack(side=tk.LEFT)
            self.graph_ymin_var = tk.IntVar(value=2000)
            ttk.Spinbox(zoom_frame, from_=0, to=4000, width=6,
                        textvariable=self.graph_ymin_var).pack(side=tk.LEFT, padx=2)
            ttk.Label(zoom_frame, text="-").pack(side=tk.LEFT)
            self.graph_ymax_var = tk.IntVar(value=2700)
            ttk.Spinbox(zoom_frame, from_=0, to=4000, width=6,
                        textvariable=self.graph_ymax_var).pack(side=tk.LEFT, padx=2)
            
            ttk.Button(zoom_frame, text="Auto Y", command=self.auto_y_range).pack(side=tk.LEFT, padx=5)
            ttk.Button(zoom_frame, text="Reset View", command=self.reset_graph_view).pack(side=tk.LEFT, padx=5)
            
            # Graph canvas frame
            graph_frame = ttk.LabelFrame(parent, text="📈 Graph", padding="5")
            graph_frame.pack(fill=tk.BOTH, expand=True, pady=5)
            
            self.graph_canvas = tk.Canvas(graph_frame, bg='#1a1a1a', height=350)
            self.graph_canvas.pack(fill=tk.BOTH, expand=True)
            
            # Tooltip label
            self.graph_tooltip = ttk.Label(graph_frame, text="", background='#ffffe0', 
                                           relief='solid', borderwidth=1)
            self.graph_tooltip_visible = False
            
            # Bind mouse events for tooltips and pan
            self.graph_canvas.bind('<Motion>', self.on_graph_mouse_move)
            self.graph_canvas.bind('<Leave>', self.on_graph_mouse_leave)
            self.graph_canvas.bind('<Button-1>', self.on_graph_click)
            self.graph_canvas.bind('<B1-Motion>', self.on_graph_drag)
            self.graph_canvas.bind('<MouseWheel>', self.on_graph_scroll)
            
            # Initialize data buffers
            self.graph_data = {i: [] for i in range(6)}
            self.graph_colors = colors
            self.graph_job = None
            self.graph_pan_start = None
            
            # Statistics labels
            stats_frame = ttk.Frame(parent)
            stats_frame.pack(fill=tk.X, pady=5)
            
            self.graph_stats_labels = []
            for i in range(6):
                lbl = ttk.Label(stats_frame, text=f"K{i+1}: --", font=('Consolas', 9), foreground=colors[i])
                lbl.pack(side=tk.LEFT, padx=10)
                self.graph_stats_labels.append(lbl)
            
            # Draw grid
            self.draw_graph_grid()
        
        def on_graph_dtype_change(self):
            """Handle data type change."""
            dtype = self.graph_dtype_var.get()
            if dtype == 'adc':
                self.graph_ymin_var.set(2000)
                self.graph_ymax_var.set(2700)
            elif dtype == 'distance':
                self.graph_ymin_var.set(0)
                self.graph_ymax_var.set(400)  # 0.01mm units
            else:  # normalized
                self.graph_ymin_var.set(0)
                self.graph_ymax_var.set(100)
            self.clear_graph_data()
        
        def auto_y_range(self):
            """Auto-adjust Y range based on data."""
            all_vals = []
            for data in self.graph_data.values():
                all_vals.extend(data)
            
            if all_vals:
                min_val = min(all_vals)
                max_val = max(all_vals)
                margin = (max_val - min_val) * 0.1
                self.graph_ymin_var.set(int(min_val - margin))
                self.graph_ymax_var.set(int(max_val + margin))
        
        def reset_graph_view(self):
            """Reset graph view to defaults."""
            self.on_graph_dtype_change()
        
        def on_graph_mouse_move(self, event):
            """Show tooltip on mouse hover."""
            w = self.graph_canvas.winfo_width()
            h = self.graph_canvas.winfo_height()
            
            if w < 10 or h < 10:
                return
            
            # Find closest data point
            max_points = self.graph_points_var.get()
            data_idx = int(event.x / w * max_points)
            
            ymin = self.graph_ymin_var.get()
            ymax = self.graph_ymax_var.get()
            y_val = ymax - (event.y / h) * (ymax - ymin)
            
            # Build tooltip text
            tooltip_lines = [f"Sample: {data_idx}", f"Y: {y_val:.1f}"]
            for i in range(6):
                if self.graph_channel_vars[i].get() and data_idx < len(self.graph_data[i]):
                    val = self.graph_data[i][data_idx]
                    tooltip_lines.append(f"Key {i+1}: {val}")
            
            # Show tooltip
            self.graph_tooltip.config(text="\n".join(tooltip_lines))
            self.graph_tooltip.place(x=event.x + 15, y=event.y + 15)
            self.graph_tooltip_visible = True
        
        def on_graph_mouse_leave(self, event):
            """Hide tooltip when mouse leaves."""
            if self.graph_tooltip_visible:
                self.graph_tooltip.place_forget()
                self.graph_tooltip_visible = False
        
        def on_graph_click(self, event):
            """Handle click for pan start."""
            self.graph_pan_start = (event.x, event.y)
        
        def on_graph_drag(self, event):
            """Handle drag for panning."""
            if self.graph_pan_start is None:
                return
            
            dx = event.x - self.graph_pan_start[0]
            dy = event.y - self.graph_pan_start[1]
            self.graph_pan_start = (event.x, event.y)
            
            # Pan Y range
            h = self.graph_canvas.winfo_height()
            ymin = self.graph_ymin_var.get()
            ymax = self.graph_ymax_var.get()
            y_range = ymax - ymin
            
            dy_val = int(-dy / h * y_range)
            self.graph_ymin_var.set(ymin + dy_val)
            self.graph_ymax_var.set(ymax + dy_val)
            
            self.draw_graph()
        
        def on_graph_scroll(self, event):
            """Handle mouse wheel for zoom."""
            ymin = self.graph_ymin_var.get()
            ymax = self.graph_ymax_var.get()
            y_center = (ymin + ymax) / 2
            y_range = ymax - ymin
            
            # Zoom in/out
            if event.delta > 0:
                zoom = 0.9  # Zoom in
            else:
                zoom = 1.1  # Zoom out
            
            new_range = y_range * zoom
            self.graph_ymin_var.set(int(y_center - new_range / 2))
            self.graph_ymax_var.set(int(y_center + new_range / 2))
            
            self.draw_graph_grid()
            self.draw_graph()
        
        def draw_graph_grid(self):
            """Draw graph grid lines."""
            if not hasattr(self, 'graph_canvas'):
                return
            self.graph_canvas.delete("grid")
            w = self.graph_canvas.winfo_width()
            h = self.graph_canvas.winfo_height()
            
            if w < 10 or h < 10:
                # Widget not yet sized
                self.after(100, self.draw_graph_grid)
                return
            
            ymin = self.graph_ymin_var.get()
            ymax = self.graph_ymax_var.get()
            y_range = ymax - ymin
            
            # Calculate grid step
            step = max(1, y_range // 10)
            # Round to nice values
            if step >= 100:
                step = (step // 100) * 100
            elif step >= 10:
                step = (step // 10) * 10
            
            # Horizontal grid lines
            val = (ymin // step) * step
            while val <= ymax:
                y = h - ((val - ymin) / y_range * h)
                if 0 <= y <= h:
                    self.graph_canvas.create_line(0, y, w, y, fill='#333333', tags="grid")
                    self.graph_canvas.create_text(5, y, text=str(val), anchor=tk.W, 
                                                   fill='#666666', font=('Consolas', 8), tags="grid")
                val += step
        
        def toggle_graph_update(self):
            """Toggle live graph updates."""
            if self.graph_live_var.get():
                self.update_graph()
            else:
                if self.graph_job:
                    self.after_cancel(self.graph_job)
                    self.graph_job = None
        
        def update_graph(self):
            """Update graph with new data."""
            if not self.graph_live_var.get():
                return
            
            try:
                dtype = self.graph_dtype_var.get()
                max_points = self.graph_points_var.get()
                
                if dtype == 'adc':
                    # Get raw ADC data
                    adc_data = self.device.get_adc_values()
                    if adc_data and 'adc' in adc_data:
                        for i, val in enumerate(adc_data['adc']):
                            self.graph_data[i].append(val)
                            if len(self.graph_data[i]) > max_points:
                                self.graph_data[i] = self.graph_data[i][-max_points:]
                elif dtype == 'distance':
                    # Get distance in mm (as 0.01mm units)
                    key_states = self.device.get_key_states()
                    if key_states and 'distances_mm' in key_states:
                        for i, dist_mm in enumerate(key_states['distances_mm']):
                            val = int(dist_mm * 100)  # Convert mm to 0.01mm
                            self.graph_data[i].append(val)
                            if len(self.graph_data[i]) > max_points:
                                self.graph_data[i] = self.graph_data[i][-max_points:]
                else:  # normalized
                    key_states = self.device.get_key_states()
                    if key_states and 'distances' in key_states:
                        for i, dist_norm in enumerate(key_states['distances']):
                            val = int(dist_norm * 100 / 255)  # 0-255 to 0-100%
                            self.graph_data[i].append(val)
                            if len(self.graph_data[i]) > max_points:
                                self.graph_data[i] = self.graph_data[i][-max_points:]
                
                # Update statistics
                self.update_graph_stats()
                self.draw_graph()
            except Exception as e:
                pass  # Silently ignore errors during graph update
            
            # Schedule next update
            update_ms = self.graph_update_var.get()
            self.graph_job = self.after(update_ms, self.update_graph)
        
        def update_graph_stats(self):
            """Update graph statistics labels."""
            for i, label in enumerate(self.graph_stats_labels):
                data = self.graph_data[i]
                if data:
                    current = data[-1]
                    avg = sum(data) / len(data)
                    label.config(text=f"K{i+1}: {current} (avg:{avg:.0f})")
                else:
                    label.config(text=f"K{i+1}: --")
        
        def draw_graph(self):
            """Draw the graph lines."""
            self.graph_canvas.delete("data")
            w = self.graph_canvas.winfo_width()
            h = self.graph_canvas.winfo_height()
            
            if w < 10 or h < 10:
                return
            
            ymin = self.graph_ymin_var.get()
            ymax = self.graph_ymax_var.get()
            y_range = max(1, ymax - ymin)
            
            for i in range(6):
                if not self.graph_channel_vars[i].get():
                    continue
                
                data = self.graph_data[i]
                if len(data) < 2:
                    continue
                
                points = []
                for j, val in enumerate(data):
                    x = j / len(data) * w
                    # Map value to canvas height using dynamic range
                    y = h - ((val - ymin) / y_range * h)
                    y = max(0, min(h, y))  # Clamp
                    points.append((x, y))
                
                # Draw line
                for j in range(len(points) - 1):
                    self.graph_canvas.create_line(
                        points[j][0], points[j][1],
                        points[j+1][0], points[j+1][1],
                        fill=self.graph_colors[i], width=1, tags="data"
                    )
        
        def clear_graph_data(self):
            """Clear graph data."""
            self.graph_data = {i: [] for i in range(6)}
            if hasattr(self, 'graph_canvas'):
                self.graph_canvas.delete("data")
            if hasattr(self, 'status_var'):
                self.status_var.set("🗑️ Graph data cleared")
        
        def factory_reset(self):
            """Reset to factory defaults."""
            if messagebox.askyesno("Factory Reset", 
                    "Are you sure you want to reset ALL settings to factory defaults?\n\n"
                    "This will:\n"
                    "- Clear all LED patterns\n"
                    "- Reset brightness to default\n"
                    "- Reset all interface settings\n\n"
                    "This cannot be undone!"):
                if self.device.factory_reset():
                    self.status_var.set("🔧 Factory reset complete!")
                    messagebox.showinfo("Success", "Factory reset complete!\n\nReloading settings...")
                    self.load_from_device()
                else:
                    self.status_var.set("❌ Factory reset failed")
                    messagebox.showerror("Error", "Factory reset failed")
    
    # Keep the old class name as alias for compatibility
    LEDMatrixEditor = KBHEConfigApp


# ============================================================================
# Command Line Interface
# ============================================================================

def print_status(device):
    """Print current device status."""
    print("\n--- Device Status ---")
    
    version = device.get_firmware_version()
    print(f"Firmware Version: {version if version else 'Unknown'}")
    
    options = device.get_options()
    if options:
        print(f"Keyboard Enabled: {'Yes' if options['keyboard_enabled'] else 'No'}")
        print(f"Gamepad Enabled:  {'Yes' if options['gamepad_enabled'] else 'No'}")
    
    led_enabled = device.led_get_enabled()
    brightness = device.led_get_brightness()
    print(f"LED Enabled:      {'Yes' if led_enabled else 'No'}")
    print(f"LED Brightness:   {brightness if brightness is not None else 'Unknown'}")
    
    adc = device.get_adc_values()
    if adc:
        print(f"ADC Values: {adc}")

def interactive_menu(device):
    """Interactive menu for device configuration."""
    while True:
        print("\n=== KBHE Configuration Menu ===")
        print("1. Show status")
        print("2. Toggle keyboard (enabled/disabled)")
        print("3. Toggle gamepad (enabled/disabled)")
        print("4. Toggle LED matrix (enabled/disabled)")
        print("5. Set LED brightness")
        print("6. LED test (rainbow)")
        print("7. LED clear")
        print("8. Open LED Matrix Editor (GUI)")
        print("9. Save settings to flash")
        print("0. Exit")
        
        choice = input("\nChoice: ").strip()
        
        if choice == '1':
            print_status(device)
        
        elif choice == '2':
            options = device.get_options()
            if options:
                new_state = not options['keyboard_enabled']
                if device.set_keyboard_enabled(new_state):
                    print(f"✅ Keyboard {'enabled' if new_state else 'disabled'}")
                else:
                    print("❌ Failed")
        
        elif choice == '3':
            options = device.get_options()
            if options:
                new_state = not options['gamepad_enabled']
                if device.set_gamepad_enabled(new_state):
                    print(f"✅ Gamepad {'enabled' if new_state else 'disabled'}")
                else:
                    print("❌ Failed")
        
        elif choice == '4':
            led_enabled = device.led_get_enabled()
            if led_enabled is not None:
                new_state = not led_enabled
                if device.led_set_enabled(new_state):
                    print(f"✅ LED matrix {'enabled' if new_state else 'disabled'}")
                else:
                    print("❌ Failed")
        
        elif choice == '5':
            try:
                brightness = int(input("Brightness (0-255): ").strip())
                if device.led_set_brightness(brightness):
                    print(f"✅ Brightness set to {brightness}")
                else:
                    print("❌ Failed")
            except ValueError:
                print("Invalid input")
        
        elif choice == '6':
            if device.led_test_rainbow():
                print("✅ Rainbow test pattern displayed")
            else:
                print("❌ Failed")
        
        elif choice == '7':
            if device.led_clear():
                print("✅ LEDs cleared")
            else:
                print("❌ Failed")
        
        elif choice == '8':
            if HAS_GUI:
                print("Opening LED Matrix Editor...")
                editor = LEDMatrixEditor(device)
                editor.mainloop()
            else:
                print("❌ GUI not available (tkinter not installed)")
        
        elif choice == '9':
            if device.save_settings():
                print("✅ Settings saved to flash")
            else:
                print("❌ Failed to save")
        
        elif choice == '0':
            break
        
        else:
            print("Invalid choice")

def main():
    print("=== KBHE Raw HID Configuration Tool ===\n")
    
    device = KBHEDevice()
    
    try:
        device.connect()
        
        # Show initial status
        print_status(device)
        
        # Check for GUI mode argument
        if len(sys.argv) > 1 and sys.argv[1] == '--gui':
            if HAS_GUI:
                editor = LEDMatrixEditor(device)
                editor.mainloop()
            else:
                print("❌ GUI not available")
                interactive_menu(device)
        else:
            # Interactive menu
            interactive_menu(device)
        
    except Exception as ex:
        print(f"❌ Error: {ex}")
        return 1
    finally:
        device.disconnect()
        print("\nDisconnected.")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())