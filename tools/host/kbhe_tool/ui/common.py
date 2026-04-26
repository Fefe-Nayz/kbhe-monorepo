import pathlib
import queue
import struct
import threading
import time

try:
    import tkinter as tk
    from tkinter import colorchooser, filedialog, messagebox, scrolledtext, ttk
    HAS_GUI = True
except ImportError:
    HAS_GUI = False
    tk = None
    ttk = None
    scrolledtext = None
    colorchooser = None
    filedialog = None
    messagebox = None

from ..firmware import perform_firmware_update, reconnect_device
from ..protocol import (
    GAMEPAD_AXES,
    GAMEPAD_AXIS_NAMES,
    GAMEPAD_BUTTONS,
    GAMEPAD_BUTTON_NAMES,
    GAMEPAD_DIRECTIONS,
    GAMEPAD_DIRECTION_NAMES,
    HID_KEYCODES,
    HID_KEYCODE_NAMES,
    LEDEffect,
    LED_EFFECT_NAMES,
    SOCD_RESOLUTIONS,
    SOCD_RESOLUTION_NAMES,
)

__all__ = [
    'HAS_GUI',
    'tk',
    'ttk',
    'scrolledtext',
    'colorchooser',
    'filedialog',
    'messagebox',
    'pathlib',
    'queue',
    'struct',
    'threading',
    'time',
    'perform_firmware_update',
    'reconnect_device',
    'HID_KEYCODES',
    'HID_KEYCODE_NAMES',
    'LEDEffect',
    'LED_EFFECT_NAMES',
    'SOCD_RESOLUTIONS',
    'SOCD_RESOLUTION_NAMES',
    'GAMEPAD_AXES',
    'GAMEPAD_AXIS_NAMES',
    'GAMEPAD_DIRECTIONS',
    'GAMEPAD_DIRECTION_NAMES',
    'GAMEPAD_BUTTONS',
    'GAMEPAD_BUTTON_NAMES',
]
