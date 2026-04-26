from __future__ import annotations

from PySide6.QtCore import QSignalBlocker, Qt, QTimer
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QButtonGroup,
    QCheckBox,
    QColorDialog,
    QComboBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QRadioButton,
    QScrollArea,
    QSlider,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ...protocol import (
    LEDEffect,
    LED_EFFECT_NAMES,
    LED_EFFECT_PARAM_COLOR_B,
    LED_EFFECT_PARAM_COLOR_G,
    LED_EFFECT_PARAM_COLOR_R,
    LED_EFFECT_PARAM_COUNT,
    LED_EFFECT_PARAM_SPEED,
    ParamType,
)
from ..widgets import PageScaffold, SectionCard, StatusChip, SubCard, make_secondary_button

EFFECT_GROUPS = [
    ("Software & Static", [(0, "Matrix (Software)"), (7, "Third-Party Live"), (23, "Solid Color"), (27, "Breathing"), (26, "Gradient Left-Right"), (25, "Gradient Up-Down"), (24, "Alpha Mods")]),
    ("Ambient Motion", [(1, "Plasma"), (2, "Fire"), (3, "Ocean Waves"), (4, "Sparkle"), (5, "Breathing Rainbow"), (6, "Color Cycle"), (28, "Colorband Sat"), (29, "Colorband Val"), (30, "Colorband Pinwheel Sat"), (31, "Colorband Pinwheel Val"), (32, "Colorband Spiral Sat"), (33, "Colorband Spiral Val"), (39, "Flower Blooming"), (40, "Raindrops"), (41, "Jellybean Raindrops"), (42, "Pixel Rain"), (43, "Pixel Flow"), (44, "Pixel Fractal")]),
    ("Cycling / Rainbow", [(13, "Cycle Pinwheel"), (14, "Cycle Spiral"), (15, "Cycle Out-In Dual"), (16, "Rainbow Beacon"), (17, "Rainbow Pinwheels"), (18, "Rainbow Moving Chevron"), (19, "Hue Breathing"), (20, "Hue Pendulum"), (21, "Hue Wave"), (22, "Riverflow"), (34, "Cycle All"), (35, "Cycle Left-Right"), (36, "Cycle Up-Down"), (37, "Cycle Out-In"), (38, "Dual Beacon")]),
    ("Reactive + Audio", [(8, "Sensor Distance"), (9, "Impact Rainbow"), (10, "Reactive Ghost"), (11, "Audio Spectrum"), (12, "Key State Demo"), (45, "Typing Heatmap"), (46, "Digital Rain"), (54, "Starlight Smooth"), (55, "Starlight"), (56, "Starlight Dual Sat"), (57, "Starlight Dual Hue")]),
    ("Solid Reactive", [(47, "Solid Reactive Simple"), (48, "Solid Reactive"), (49, "Solid Reactive Wide"), (58, "Solid Reactive Multi Wide"), (50, "Solid Reactive Cross"), (59, "Solid Reactive Multi Cross"), (51, "Solid Reactive Nexus"), (60, "Solid Reactive Multi Nexus")]),
    ("Splash", [(52, "Splash"), (61, "Multi Splash"), (53, "Solid Splash"), (62, "Solid Multi Splash")]),
]

EFFECT_METADATA = {
    0: ("Matrix (Software)", "Uses the editable matrix pattern from the Lighting tab."),
    1: ("Rainbow Wave", "Animated rainbow sweep with angle and curvature controls."),
    2: ("Breathing", "Pulses the selected color."),
    3: ("Static Rainbow", "Rainbow colors without motion."),
    4: ("Solid Color", "A single steady color fill."),
    5: ("Plasma", "Fluid plasma motion."),
    6: ("Fire", "Animated fire simulation."),
    7: ("Ocean Waves", "Layered wave motion with crest and depth controls."),
    8: ("Matrix Rain", "Digital rain effect."),
    9: ("Sparkle", "Random sparkles over a configurable ambient tint."),
    10: ("Breathing Rainbow", "Breathing plus hue drift."),
    11: ("Spiral", "Improved spiral pattern with better center-angle rotation."),
    12: ("Color Cycle", "Continuous cycling through hues."),
    13: ("Reactive (Wave)", "Expanding multi-wave ripples from each key press."),
    14: ("Third-Party Live", "Shows the live frame controlled externally."),
    15: ("Sensor Distance", "Each key color follows its travel distance."),
    16: ("Rainbow Dome", "Curved dome-style rainbow centered on the keyboard."),
    17: ("Sphere", "Spherical pulse from a configurable center point."),
    18: ("Dual Sphere", "Two spherical emitters with independent centers and blend."),
    19: ("Strip Spin + Zoom", "Rotating color strips with zoom and twist."),
    20: ("Impact Rainbow", "Rainbow wave with transient key/audio impact boosts."),
    21: ("Reactive Heatmap", "Presses accumulate and fade as a heatmap."),
    22: ("Reactive Ghost", "Soft ghost trails from key presses."),
    23: ("Audio Spectrum", "Host audio-driven visualizer with selectable rendering modes and impact accents."),
    24: ("Key State Demo", "Digital key-state colors (pressed vs released)."),
    25: ("Cycle Pinwheel", "angular hue pinwheel around center."),
    26: ("Cycle Spiral", "radial spiral hue sweep."),
    27: ("Cycle Out-In Dual", "dual out-in hue cycle."),
    28: ("Rainbow Beacon", "rotating rainbow beacon."),
    29: ("Rainbow Pinwheels", "multi-pinwheel rainbow rotation."),
    30: ("Rainbow Moving Chevron", "moving chevron rainbow bands."),
    31: ("Hue Breathing", "hue breathing modulation."),
    32: ("Hue Pendulum", "pendulum hue wave."),
    33: ("Hue Wave", "horizontal hue wave."),
    34: ("Riverflow", "per-key phase riverflow."),
    35: ("Solid Color", "solid color fill."),
    36: ("Alpha Mods", "alpha/modifier split brightness look."),
    37: ("Gradient Up Down", "vertical hue gradient."),
    38: ("Gradient Left Right", "horizontal hue gradient."),
    39: ("Breathing", "color breathing pulse."),
    40: ("Colorband Sat", "moving saturation colorband."),
    41: ("Colorband Val", "moving brightness colorband."),
    42: ("Colorband Pinwheel Sat", "pinwheel saturation band."),
    43: ("Colorband Pinwheel Val", "pinwheel brightness band."),
    44: ("Colorband Spiral Sat", "spiral saturation band."),
    45: ("Colorband Spiral Val", "spiral brightness band."),
    46: ("Cycle All", "global hue cycle."),
    47: ("Cycle Left Right", "left-to-right hue cycle."),
    48: ("Cycle Up Down", "up-down hue cycle."),
    49: ("Cycle Out In", "center radial hue cycle."),
    50: ("Dual Beacon", "dual rotating beacon."),
    51: ("Flower Blooming", "blooming petal pattern."),
    52: ("Raindrops", "ripple-style raindrops."),
    53: ("Jellybean Raindrops", "raindrops with random hues."),
    54: ("Pixel Rain", "falling pixel streaks."),
    55: ("Pixel Flow", "flowing horizontal pixels."),
    56: ("Pixel Fractal", "fractal-like pixel motion."),
    57: ("Typing Heatmap", "typing heatmap."),
    58: ("Digital Rain", "digital rain matrix pattern."),
    59: ("Solid Reactive Simple", "simple solid reactive effect."),
    60: ("Solid Reactive", "radial solid reactive effect."),
    61: ("Solid Reactive Wide", "wide reactive spread."),
    62: ("Solid Reactive Cross", "cross reactive spread."),
    63: ("Solid Reactive Nexus", "nexus reactive blend."),
    64: ("Splash", "colorful splash waves."),
    65: ("Solid Splash", "single-color splash."),
    66: ("Starlight Smooth", "smooth twinkling starlight."),
    67: ("Starlight", "hard twinkling starlight."),
    68: ("Starlight Dual Sat", "dual-color smooth starlight."),
    69: ("Starlight Dual Hue", "dual-hue starlight flashes."),
    70: ("Solid Reactive Multi Wide", "wide reactive spread using all remembered key hits."),
    71: ("Solid Reactive Multi Cross", "cross reactive spread using all remembered key hits."),
    72: ("Solid Reactive Multi Nexus", "nexus reactive blend using all remembered key hits."),
    73: ("Multi Splash", "colorful splash waves from all remembered key hits."),
    74: ("Solid Multi Splash", "single-color splash waves from all remembered key hits."),
}

EFFECT_PARAM_METADATA = {
    1: [{"index": 0, "label": "Horizontal Scale", "kind": "slider", "default": 160},
        {"index": 1, "label": "Vertical Scale", "kind": "slider", "default": 96},
        {"index": 2, "label": "Drift", "kind": "slider", "default": 160},
        {"index": 3, "label": "Saturation", "kind": "slider", "default": 255},
        {"index": 4, "label": "Angle", "kind": "slider", "default": 0},
        {"index": 5, "label": "Curvature", "kind": "slider", "default": 0}],
    2: [{"index": 0, "label": "Brightness Floor", "kind": "slider", "default": 24},
        {"index": 1, "label": "Brightness Ceiling", "kind": "slider", "default": 255},
        {"index": 2, "label": "Plateau", "kind": "slider", "default": 48}],
    3: [{"index": 0, "label": "Horizontal Scale", "kind": "slider", "default": 160},
        {"index": 1, "label": "Vertical Scale", "kind": "slider", "default": 120},
        {"index": 2, "label": "Saturation", "kind": "slider", "default": 144},
        {"index": 3, "label": "Brightness", "kind": "slider", "default": 255}],
    4: [{"index": 0, "label": "Effect Brightness", "kind": "slider", "default": 255}],
    5: [{"index": 0, "label": "Motion Depth", "kind": "slider", "default": 96},
        {"index": 1, "label": "Saturation", "kind": "slider", "default": 192},
        {"index": 2, "label": "Radial Warp", "kind": "slider", "default": 128},
        {"index": 3, "label": "Brightness", "kind": "slider", "default": 255}],
    6: [{"index": 0, "label": "Heat Boost", "kind": "slider", "default": 160},
        {"index": 1, "label": "Ember Floor", "kind": "slider", "default": 96},
        {"index": 2, "label": "Cooling", "kind": "slider", "default": 96},
        {"index": 3, "label": "Palette", "kind": "select", "default": 0, "options": [(0, "Classic"), (1, "Magma"), (2, "Electric Blue")]}],
    7: [{"index": 0, "label": "Hue Bias", "kind": "slider", "default": 160},
        {"index": 1, "label": "Depth Dimming", "kind": "slider", "default": 64},
        {"index": 2, "label": "Foam Highlight", "kind": "toggle", "default": 1},
        {"index": 3, "label": "Crest Speed", "kind": "slider", "default": 160}],
    8: [{"index": 0, "label": "Trail Length", "kind": "slider", "default": 64},
        {"index": 1, "label": "Head Size", "kind": "slider", "default": 160},
        {"index": 2, "label": "Density", "kind": "slider", "default": 96},
        {"index": 3, "label": "White Heads", "kind": "toggle", "default": 1},
        {"index": 4, "label": "Hue Bias", "kind": "slider", "default": 0}],
    9: [{"index": 0, "label": "Density", "kind": "slider", "default": 48},
        {"index": 1, "label": "Sparkle Brightness", "kind": "slider", "default": 224},
        {"index": 2, "label": "Rainbow Mix", "kind": "slider", "default": 160},
        {"index": 3, "label": "Ambient Glow", "kind": "slider", "default": 0}],
    10: [{"index": 0, "label": "Brightness Floor", "kind": "slider", "default": 24},
         {"index": 1, "label": "Hue Drift", "kind": "slider", "default": 192},
         {"index": 2, "label": "Saturation", "kind": "slider", "default": 255}],
    11: [{"index": 0, "label": "Twist", "kind": "slider", "default": 160},
         {"index": 1, "label": "Radial Scale", "kind": "slider", "default": 96},
         {"index": 2, "label": "Orbit Speed", "kind": "slider", "default": 128},
         {"index": 3, "label": "Saturation", "kind": "slider", "default": 255}],
    12: [{"index": 0, "label": "Hue Step", "kind": "slider", "default": 64},
         {"index": 1, "label": "Saturation", "kind": "slider", "default": 255},
         {"index": 2, "label": "Brightness", "kind": "slider", "default": 255},
         {"index": 3, "label": "Color Mix", "kind": "slider", "default": 0}],
    13: [{"label": "Reactive Color", "kind": "color"},
         {"index": 0, "label": "Decay", "kind": "slider", "default": 72},
         {"index": 1, "label": "Spread", "kind": "slider", "default": 128},
            {"index": 2, "label": "Base Glow", "kind": "slider", "default": 8, "max": 96},
         {"index": 3, "label": "White Core", "kind": "toggle", "default": 1},
            {"index": 4, "label": "Gain", "kind": "slider", "default": 224},
            {"index": 5, "label": "Palette", "kind": "select", "default": 0, "options": [(0, "Custom Color"), (1, "Rainbow Wave")]}],
    15: [{"index": 0, "label": "Brightness Floor", "kind": "slider", "default": 32},
         {"index": 1, "label": "Hue Span", "kind": "slider", "default": 170, "min": 1},
         {"index": 2, "label": "Saturation", "kind": "slider", "default": 255},
         {"index": 3, "label": "Reverse Gradient", "kind": "toggle", "default": 0}],
        16: [{"index": 0, "label": "Angular Scale", "kind": "slider", "default": 140},
            {"index": 1, "label": "Radial Scale", "kind": "slider", "default": 96},
            {"index": 2, "label": "Drift", "kind": "slider", "default": 168},
            {"index": 3, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 4, "label": "Angle", "kind": "slider", "default": 12},
            {"index": 5, "label": "Dome Curvature", "kind": "slider", "default": 176},
            {"index": 6, "label": "Center X", "kind": "slider", "default": 128},
            {"index": 7, "label": "Center Y", "kind": "slider", "default": 128}],
        17: [{"label": "Sphere Color", "kind": "color"},
            {"index": 0, "label": "Center X", "kind": "slider", "default": 128},
            {"index": 1, "label": "Center Y", "kind": "slider", "default": 128},
            {"index": 2, "label": "Radius Scale", "kind": "slider", "default": 132},
            {"index": 3, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 4, "label": "Speed", "kind": "slider", "default": 176},
            {"index": 5, "label": "Reverse", "kind": "toggle", "default": 0}],
        18: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Center 1 X", "kind": "slider", "default": 72},
            {"index": 1, "label": "Center 1 Y", "kind": "slider", "default": 128},
            {"index": 2, "label": "Center 2 X", "kind": "slider", "default": 184},
            {"index": 3, "label": "Center 2 Y", "kind": "slider", "default": 128},
            {"index": 4, "label": "Saturation", "kind": "slider", "default": 176},
            {"index": 5, "label": "Speed", "kind": "slider", "default": 176},
            {"index": 6, "label": "Blend", "kind": "slider", "default": 132}],
        19: [{"index": 0, "label": "Stripe Width", "kind": "slider", "default": 120},
            {"index": 1, "label": "Spin Speed", "kind": "slider", "default": 172},
            {"index": 2, "label": "Zoom", "kind": "slider", "default": 168},
            {"index": 3, "label": "Saturation", "kind": "slider", "default": 220},
            {"index": 4, "label": "Brightness", "kind": "slider", "default": 255}],
        20: [{"index": 0, "label": "Boost Mode", "kind": "select", "default": 2, "options": [(0, "Speed"), (1, "White/Brightness"), (2, "Both")]},
            {"index": 1, "label": "Boost Decay", "kind": "slider", "default": 184},
            {"index": 2, "label": "Key Boost", "kind": "slider", "default": 96},
            {"index": 3, "label": "Audio Boost", "kind": "slider", "default": 160},
            {"index": 4, "label": "Max Boost", "kind": "slider", "default": 208},
            {"index": 5, "label": "Angle", "kind": "slider", "default": 16},
            {"index": 6, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 7, "label": "Wave Drift", "kind": "slider", "default": 168}],
        21: [{"index": 0, "label": "Heat Gain", "kind": "slider", "default": 224},
            {"index": 1, "label": "Decay", "kind": "slider", "default": 168},
            {"index": 2, "label": "Diffusion", "kind": "slider", "default": 88},
            {"index": 3, "label": "Floor", "kind": "slider", "default": 48}],
        22: [{"label": "Ghost Color", "kind": "color"},
            {"index": 0, "label": "Decay", "kind": "slider", "default": 100},
            {"index": 1, "label": "Spread", "kind": "slider", "default": 156},
            {"index": 2, "label": "Trail", "kind": "slider", "default": 184},
            {"index": 3, "label": "Gain", "kind": "slider", "default": 208}],
        23: [{"label": "Spectrum Base Color", "kind": "color"},
            {"index": 0, "label": "Hue Span", "kind": "slider", "default": 128},
            {"index": 1, "label": "Glow Floor", "kind": "slider", "default": 0, "max": 96},
            {"index": 2, "label": "Peak Gain", "kind": "slider", "default": 255},
            {"index": 3, "label": "Mirror", "kind": "toggle", "default": 0},
            {"index": 4, "label": "Release", "kind": "slider", "default": 104},
            {"index": 5, "label": "Visualizer Mode", "kind": "select", "default": 0,
             "options": [(0, "Vertical Bars"), (1, "Center Mirror"), (2, "Horizontal Sweep"), (3, "Peak Focus")]},
            {"index": 6, "label": "Contrast", "kind": "slider", "default": 236}],
        24: [{"index": 0, "label": "Invert Mapping", "kind": "toggle", "default": 0},
            {"index": 1, "label": "Pressed Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Released Brightness", "kind": "slider", "default": 96}],
        25: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Spin", "kind": "slider", "default": 176}],
        26: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Spin", "kind": "slider", "default": 176}],
        27: [{"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Speed Factor", "kind": "slider", "default": 176}],
        28: [{"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Spin", "kind": "slider", "default": 176}],
        29: [{"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Spin", "kind": "slider", "default": 176}],
        30: [{"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Speed Factor", "kind": "slider", "default": 176}],
        31: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Hue Delta", "kind": "slider", "default": 12},
            {"index": 1, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 3, "label": "Breath Speed", "kind": "slider", "default": 160}],
        32: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Hue Delta", "kind": "slider", "default": 12},
            {"index": 1, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 3, "label": "Wave Speed", "kind": "slider", "default": 160}],
        33: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Hue Delta", "kind": "slider", "default": 24},
            {"index": 1, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 3, "label": "Wave Speed", "kind": "slider", "default": 160}],
        34: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Flow Speed", "kind": "slider", "default": 176},
            {"index": 1, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255}],
        35: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Brightness Trim", "kind": "slider", "default": 255}],
        36: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Alpha Brightness", "kind": "slider", "default": 255},
            {"index": 1, "label": "Modifier Brightness", "kind": "slider", "default": 96}],
        37: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Hue Span", "kind": "slider", "default": 96},
            {"index": 1, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255}],
        38: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Hue Span", "kind": "slider", "default": 96},
            {"index": 1, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255}],
        39: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Brightness Floor", "kind": "slider", "default": 24},
            {"index": 1, "label": "Brightness Ceiling", "kind": "slider", "default": 255}],
        40: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation Min", "kind": "slider", "default": 24},
            {"index": 1, "label": "Saturation Max", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255}],
        41: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Brightness Min", "kind": "slider", "default": 24},
            {"index": 1, "label": "Brightness Max", "kind": "slider", "default": 255},
            {"index": 2, "label": "Saturation", "kind": "slider", "default": 255}],
        42: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation Min", "kind": "slider", "default": 24},
            {"index": 1, "label": "Saturation Max", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255}],
        43: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Brightness Min", "kind": "slider", "default": 24},
            {"index": 1, "label": "Brightness Max", "kind": "slider", "default": 255},
            {"index": 2, "label": "Saturation", "kind": "slider", "default": 255}],
        44: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation Min", "kind": "slider", "default": 24},
            {"index": 1, "label": "Saturation Max", "kind": "slider", "default": 255},
            {"index": 2, "label": "Brightness", "kind": "slider", "default": 255}],
        45: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Brightness Min", "kind": "slider", "default": 24},
            {"index": 1, "label": "Brightness Max", "kind": "slider", "default": 255},
            {"index": 2, "label": "Saturation", "kind": "slider", "default": 255}],
        46: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255}],
        47: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255}],
        48: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255}],
        49: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255}],
        50: [{"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Spin", "kind": "slider", "default": 176}],
        51: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Petal Count", "kind": "slider", "default": 160}],
        52: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Drop Density", "kind": "slider", "default": 96}],
        53: [{"index": 0, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 1, "label": "Drop Density", "kind": "slider", "default": 104}],
        54: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255},
            {"index": 2, "label": "Rain Width", "kind": "slider", "default": 96}],
        55: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255}],
        56: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Saturation", "kind": "slider", "default": 255},
            {"index": 1, "label": "Brightness", "kind": "slider", "default": 255}],
        57: [],
        58: [{"index": 0, "label": "Trail Length", "kind": "slider", "default": 64},
            {"index": 1, "label": "Head Size", "kind": "slider", "default": 160},
            {"index": 2, "label": "Density", "kind": "slider", "default": 96},
            {"index": 3, "label": "White Heads", "kind": "toggle", "default": 1},
            {"index": 4, "label": "Hue Bias", "kind": "slider", "default": 0}],
        59: [{"label": "Primary Color", "kind": "color"}],
        60: [{"label": "Primary Color", "kind": "color"}],
        61: [{"label": "Primary Color", "kind": "color"}],
        62: [{"label": "Primary Color", "kind": "color"}],
        63: [{"label": "Primary Color", "kind": "color"}],
        64: [{"label": "Primary Color", "kind": "color"}],
        65: [{"label": "Primary Color", "kind": "color"}],
        66: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Twinkle Density", "kind": "slider", "default": 88},
            {"index": 1, "label": "Base Brightness", "kind": "slider", "default": 18}],
        67: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Twinkle Density", "kind": "slider", "default": 64},
            {"index": 1, "label": "Base Brightness", "kind": "slider", "default": 12}],
        68: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Twinkle Density", "kind": "slider", "default": 92},
            {"index": 1, "label": "Base Brightness", "kind": "slider", "default": 16},
            {"index": 11, "label": "Secondary Red", "kind": "slider", "default": 0},
            {"index": 12, "label": "Secondary Green", "kind": "slider", "default": 160},
            {"index": 13, "label": "Secondary Blue", "kind": "slider", "default": 255}],
        69: [{"label": "Primary Color", "kind": "color"},
            {"index": 0, "label": "Twinkle Density", "kind": "slider", "default": 84},
            {"index": 1, "label": "Base Brightness", "kind": "slider", "default": 12},
            {"index": 11, "label": "Secondary Red", "kind": "slider", "default": 0},
            {"index": 12, "label": "Secondary Green", "kind": "slider", "default": 160},
            {"index": 13, "label": "Secondary Blue", "kind": "slider", "default": 255}],
        70: [{"label": "Primary Color", "kind": "color"}],
        71: [{"label": "Primary Color", "kind": "color"}],
        72: [{"label": "Primary Color", "kind": "color"}],
        73: [{"label": "Primary Color", "kind": "color"}],
        74: [{"label": "Primary Color", "kind": "color"}],
}

DEFAULT_EFFECT_COLOR = [255, 0, 0]
_SPEEDLESS_EFFECTS = {0, 14, 57}

_NONE_EFFECT_ID = int(LEDEffect.NONE)
_THIRD_PARTY_EFFECT_ID = int(LEDEffect.THIRD_PARTY)
_AUDIO_EFFECT_ID = int(LEDEffect.AUDIO_SPECTRUM)

_SCHEMA_TYPE_U8 = int(ParamType.U8)
_SCHEMA_TYPE_BOOL = int(ParamType.BOOL)
_SCHEMA_TYPE_HUE = int(ParamType.HUE)
_SCHEMA_TYPE_COLOR = int(ParamType.COLOR)

_AUDIO_VISUALIZER_MODE_OPTIONS = [
    (0, "Vertical Bars"),
    (1, "Center Mirror"),
    (2, "Horizontal Sweep"),
    (3, "Peak Focus"),
]

_SCHEMA_PARAM_LABELS = {
    int(LEDEffect.PLASMA): {
        0: "Motion Depth",
        1: "Saturation",
        2: "Radial Warp",
        3: "Value",
    },
    int(LEDEffect.FIRE): {
        0: "Heat Boost",
        1: "Ember Floor",
        2: "Cooling",
        3: "Palette",
    },
    int(LEDEffect.OCEAN): {
        0: "Hue Bias",
        1: "Depth Dimming",
        2: "Foam Highlight",
        3: "Crest Speed",
    },
    int(LEDEffect.SPARKLE): {
        0: "Density",
        1: "Sparkle Brightness",
        2: "Rainbow Mix",
        3: "Ambient Glow",
    },
    int(LEDEffect.BREATHING_RAINBOW): {
        0: "Brightness Floor",
        1: "Hue Drift",
        2: "Saturation",
    },
    int(LEDEffect.COLOR_CYCLE): {
        0: "Hue Step",
        1: "Saturation",
        2: "Value",
        3: "Effect-Color Mix",
    },
    int(LEDEffect.DISTANCE_SENSOR): {
        0: "Brightness Floor",
        1: "Hue Span",
        2: "Saturation",
        3: "Reverse Gradient",
    },
    int(LEDEffect.IMPACT_RAINBOW): {
        0: "Boost Mode",
        1: "Boost Decay",
        2: "Key Boost",
        3: "Audio Boost",
        4: "Max Boost",
        5: "Angle",
        6: "Saturation",
        7: "Wave Drift",
    },
    int(LEDEffect.REACTIVE_GHOST): {
        0: "Decay",
        1: "Spread",
        2: "Trail",
        3: "Gain",
    },
    int(LEDEffect.AUDIO_SPECTRUM): {
        0: "Hue Span",
        1: "Base Floor",
        2: "Peak Gain",
        3: "Mirror",
        4: "Decay",
        5: "Visualizer Mode",
        6: "Contrast",
    },
    int(LEDEffect.KEY_STATE_DEMO): {
        0: "Invert Mapping",
        1: "Pressed Brightness",
        2: "Released Brightness",
    },
    int(LEDEffect.SOLID_COLOR): {
        0: "Brightness Trim",
    },
    int(LEDEffect.BREATHING): {
        0: "Brightness Floor",
        1: "Brightness Ceiling",
        2: "Plateau",
    },
    int(LEDEffect.GRADIENT_LEFT_RIGHT): {
        0: "Horizontal Scale",
        1: "Vertical Scale",
        2: "Saturation",
        3: "Value",
    },
    int(LEDEffect.CYCLE_LEFT_RIGHT): {
        0: "Horizontal Scale",
        1: "Vertical Scale",
        2: "Drift",
        3: "Saturation",
        4: "Angle",
        5: "Curvature",
    },
    int(LEDEffect.DIGITAL_RAIN): {
        0: "Trail Length",
        1: "Head Size",
        2: "Density",
        3: "White Heads",
        4: "Hue Bias",
    },
    int(LEDEffect.TYPING_HEATMAP): {
        0: "Heat Gain",
        1: "Decay",
        2: "Diffusion",
        3: "Floor",
    },
    int(LEDEffect.SPLASH): {
        0: "Decay",
        1: "Spread",
        2: "Base Glow",
        3: "White Core",
        4: "Gain",
        5: "Palette",
    },
    int(LEDEffect.MULTI_SPLASH): {
        0: "Decay",
        1: "Spread",
        2: "Base Glow",
        3: "White Core",
        4: "Gain",
        5: "Palette",
    },
}

_SCHEMA_SELECT_OPTIONS = {
    (int(LEDEffect.FIRE), 3): [(0, "Fire"), (1, "Ice")],
    (int(LEDEffect.IMPACT_RAINBOW), 0): [
        (0, "Speed"),
        (1, "White/Brightness"),
        (2, "Both"),
    ],
    (int(LEDEffect.AUDIO_SPECTRUM), 5): list(_AUDIO_VISUALIZER_MODE_OPTIONS),
    (int(LEDEffect.SPLASH), 5): [(0, "Custom Color"), (1, "Rainbow")],
    (int(LEDEffect.MULTI_SPLASH), 5): [(0, "Custom Color"), (1, "Rainbow")],
}

_EFFECT_DESCRIPTIONS = {
    _NONE_EFFECT_ID: "Uses the editable matrix pattern from the Lighting tab.",
    _THIRD_PARTY_EFFECT_ID: "Shows the live frame controlled externally.",
    _AUDIO_EFFECT_ID: "Host audio-driven visualizer with live spectrum feed.",
}

def _clamp(value: int) -> int:
    return max(0, min(255, int(value)))


def _effect_name(mode: int) -> str:
    try:
        effect = LEDEffect(int(mode))
        return LED_EFFECT_NAMES.get(effect, effect.name.replace("_", " ").title())
    except Exception:
        return f"Mode {int(mode)}"


def _effect_description(mode: int) -> str:
    return _EFFECT_DESCRIPTIONS.get(int(mode), "Dynamic tuning is provided by the firmware schema.")


def _schema_param_label(mode: int, param_id: int, param_type: int) -> str:
    if param_id == LED_EFFECT_PARAM_SPEED:
        return "Speed"

    if param_type == _SCHEMA_TYPE_COLOR:
        if param_id == LED_EFFECT_PARAM_COLOR_R:
            return "Primary Color"
        if param_id == LED_EFFECT_PARAM_COLOR_R + 3:
            return "Secondary Color"
        return f"Color {param_id}"

    labels = _SCHEMA_PARAM_LABELS.get(int(mode), {})
    if param_id in labels:
        return labels[param_id]

    if param_type == _SCHEMA_TYPE_HUE:
        return f"Hue {param_id}"
    if param_type == _SCHEMA_TYPE_BOOL:
        return f"Flag {param_id}"

    return f"Param {param_id}"


def _sanitize_schema_descriptor(raw: dict):
    try:
        param_id = int(raw.get("id", -1))
        param_type = int(raw.get("type", _SCHEMA_TYPE_U8))
        param_min = int(raw.get("min", 0))
        param_max = int(raw.get("max", 255))
        param_default = int(raw.get("default", 0))
        param_step = int(raw.get("step", 1))
    except Exception:
        return None

    if param_id < 0 or param_id >= LED_EFFECT_PARAM_COUNT:
        return None

    if param_type not in (_SCHEMA_TYPE_U8, _SCHEMA_TYPE_BOOL, _SCHEMA_TYPE_HUE, _SCHEMA_TYPE_COLOR):
        param_type = _SCHEMA_TYPE_U8

    param_min = _clamp(param_min)
    param_max = _clamp(param_max)
    if param_max < param_min:
        param_min, param_max = param_max, param_min

    if param_type == _SCHEMA_TYPE_BOOL:
        param_min = 0
        param_max = 1
        param_default = 1 if param_default else 0
        param_step = 1
    else:
        param_default = _clamp(param_default)
        if param_default < param_min:
            param_default = param_min
        if param_default > param_max:
            param_default = param_max
        param_step = max(0, int(param_step))

    return {
        "id": param_id,
        "type": param_type,
        "min": param_min,
        "max": param_max,
        "default": param_default,
        "step": param_step,
    }


def _load_effect_schema(device, mode: int) -> list[dict]:
    getter = getattr(device, "get_led_effect_schema", None)
    if not callable(getter):
        return []

    try:
        schema = getter(int(mode))
    except Exception:
        return []

    if not isinstance(schema, dict):
        return []

    descriptors = schema.get("descriptors", [])
    if not isinstance(descriptors, list):
        return []

    sanitized = []
    for raw in descriptors:
        if not isinstance(raw, dict):
            continue
        desc = _sanitize_schema_descriptor(raw)
        if desc is not None:
            sanitized.append(desc)

    sanitized.sort(key=lambda entry: entry["id"])
    return sanitized


def _default_effect_params(mode: int, schema: list[dict] | None = None) -> list[int]:
    defaults = [0] * LED_EFFECT_PARAM_COUNT

    for desc in schema or []:
        defaults[int(desc["id"])] = int(desc["default"])

    if int(mode) not in (_NONE_EFFECT_ID, _THIRD_PARTY_EFFECT_ID) and defaults[LED_EFFECT_PARAM_SPEED] <= 0:
        defaults[LED_EFFECT_PARAM_SPEED] = 50

    return defaults


def _effect_specs(mode: int, schema: list[dict] | None = None) -> list[dict]:
    specs = []
    primary_color_added = False

    for desc in schema or []:
        param_id = int(desc["id"])
        param_type = int(desc["type"])
        label = _schema_param_label(mode, param_id, param_type)

        if param_type == _SCHEMA_TYPE_COLOR:
            if param_id == LED_EFFECT_PARAM_COLOR_R and not primary_color_added:
                specs.append({"label": label, "kind": "color"})
                primary_color_added = True
            continue

        select_options = _SCHEMA_SELECT_OPTIONS.get((int(mode), param_id))
        if select_options is not None:
            specs.append(
                {
                    "index": param_id,
                    "label": label,
                    "kind": "select",
                    "default": int(desc["default"]),
                    "options": list(select_options),
                }
            )
            continue

        if param_type == _SCHEMA_TYPE_BOOL:
            specs.append(
                {
                    "index": param_id,
                    "label": label,
                    "kind": "toggle",
                    "default": int(desc["default"]),
                }
            )
            continue

        specs.append(
            {
                "index": param_id,
                "label": label,
                "kind": "slider",
                "default": int(desc["default"]),
                "min": int(desc["min"]),
                "max": int(desc["max"]),
            }
        )

    if not specs:
        if int(mode) not in (_NONE_EFFECT_ID, _THIRD_PARTY_EFFECT_ID):
            specs.append(
                {
                    "index": LED_EFFECT_PARAM_SPEED,
                    "label": "Speed",
                    "kind": "slider",
                    "default": 50,
                    "min": 1,
                    "max": 255,
                }
            )

    return specs


class _AudioSpectrumPreview(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("AudioSpectrumPreview")
        self.setMinimumHeight(132)
        self.levels = [0] * 16
        self.peaks = [0] * 16
        self.impact = 0
        self.visualizer_mode = 0
        self.contrast = 236

    def set_render_config(self, visualizer_mode: int, contrast: int) -> None:
        self.visualizer_mode = int(visualizer_mode) & 0x03
        self.contrast = _clamp(contrast)
        self.update()

    def set_levels(self, levels: list[int], impact: int) -> None:
        clamped = [_clamp(value) for value in levels[:16]]
        if len(clamped) < 16:
            clamped.extend([0] * (16 - len(clamped)))

        for i in range(16):
            level = clamped[i]
            if level >= self.peaks[i]:
                self.peaks[i] = level
            else:
                self.peaks[i] = max(level, self.peaks[i] - 8)

        self.levels = clamped
        self.impact = _clamp(impact)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)

        bounds = self.rect().adjusted(1, 1, -1, -1)
        painter.setPen(QPen(QColor(90, 95, 106, 120), 1))
        painter.setBrush(QColor(18, 22, 30, 255))
        painter.drawRoundedRect(bounds, 10, 10)

        inner = bounds.adjusted(10, 8, -10, -10)
        band_count = len(self.levels)
        if band_count <= 0 or inner.width() <= 0 or inner.height() <= 0:
            painter.end()
            return

        mode = self.visualizer_mode & 0x03
        lane_alpha = 26 + int((self.contrast / 255.0) * 18.0)
        bar_max_h = max(1, inner.height())
        base_hue = 108
        hue_span = 120

        if mode == 2:
            for i in range(band_count):
                level = self.levels[i]
                peak = self.peaks[i]
                y0 = inner.top() + int((i * inner.height()) / band_count)
                y1 = inner.top() + int(((i + 1) * inner.height()) / band_count)
                row_h = max(2, y1 - y0 - 1)
                row_w = max(2, int((level / 255.0) * inner.width()))
                peak_x = inner.left() + max(0, int((peak / 255.0) * inner.width()) - 1)
                hue = base_hue - int((i / max(1, band_count - 1)) * hue_span)
                color = QColor.fromHsv(max(0, hue), 255, 255)

                painter.setPen(Qt.NoPen)
                painter.setBrush(QColor(9, 12, 18, lane_alpha))
                painter.drawRoundedRect(inner.left(), y0, inner.width(), row_h, 2, 2)

                if row_w > 0:
                    painter.setBrush(color)
                    painter.drawRoundedRect(inner.left(), y0, row_w, row_h, 2, 2)

                painter.setBrush(QColor.fromHsv(28, 255, 255, 232))
                painter.drawRect(peak_x, y0, 2, row_h)
        else:
            for i in range(band_count):
                level = self.levels[i]
                peak = self.peaks[i]
                x0 = inner.left() + int((i * inner.width()) / band_count)
                x1 = inner.left() + int(((i + 1) * inner.width()) / band_count)
                band_w = max(2, x1 - x0 - 1)
                x = x0
                hue = base_hue - int((i / max(1, band_count - 1)) * hue_span)
                color = QColor.fromHsv(max(0, hue), 255, 255)

                painter.setPen(Qt.NoPen)
                painter.setBrush(QColor(9, 12, 18, lane_alpha))
                painter.drawRoundedRect(x, inner.top(), band_w, inner.height(), 2, 2)

                if mode == 1:
                    half_h = max(1, int((level / 255.0) * (bar_max_h * 0.5)))
                    center_y = inner.top() + (bar_max_h // 2)
                    painter.setBrush(color)
                    painter.drawRoundedRect(x, center_y - half_h, band_w, half_h, 2, 2)
                    painter.drawRoundedRect(x, center_y, band_w, half_h, 2, 2)

                    peak_half = max(1, int((peak / 255.0) * (bar_max_h * 0.5)))
                    painter.setBrush(QColor.fromHsv(28, 255, 255, 232))
                    painter.drawRect(x, center_y - peak_half, band_w, 2)
                    painter.drawRect(x, center_y + peak_half, band_w, 2)
                else:
                    h = max(1, int((level / 255.0) * bar_max_h))
                    y = inner.bottom() - h + 1
                    if mode == 3:
                        stem_h = max(1, int(h * 0.50))
                        stem_y = inner.bottom() - stem_h + 1
                        painter.setBrush(color)
                        painter.drawRoundedRect(x, stem_y, band_w, stem_h, 2, 2)
                    else:
                        painter.setBrush(color)
                        painter.drawRoundedRect(x, y, band_w, h, 2, 2)

                    if peak > 2:
                        peak_h = max(1, int((peak / 255.0) * bar_max_h))
                        peak_y = inner.bottom() - peak_h + 1
                        painter.setBrush(QColor.fromHsv(28, 255, 255, 232))
                        painter.drawRect(x, peak_y, band_w, 2)

        if self.impact > 0:
            alpha = max(24, int((self.impact / 255.0) * 120))
            painter.setPen(QPen(QColor(255, 255, 255, alpha), 1))
            y = inner.top() + int((1.0 - (self.impact / 255.0)) * max(1, inner.height() - 1))
            painter.drawLine(inner.left(), y, inner.right(), y)

        painter.end()


class EffectsPage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self._loading = False
        self.effect_color = DEFAULT_EFFECT_COLOR[:]
        self.effect_params = [0] * LED_EFFECT_PARAM_COUNT
        self.effect_schema = []
        self.param_rows = []
        self.audio_preview_timer = QTimer(self)
        self.audio_preview_timer.setInterval(40)
        self.audio_preview_timer.timeout.connect(self._on_audio_preview_tick)
        self._build_ui()
        self.reload()
        self.audio_preview_timer.start()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        scaffold = PageScaffold(
            "Effects",
            "Choose a mode, tune it per effect, and set the color used by color-aware effects. "
            "Changes apply live and autosave after a short idle.",
        )
        root.addWidget(scaffold, 1)
        self.effect_summary = QLabel()
        self.effect_summary.setObjectName("Muted")
        self.effect_summary.setWordWrap(True)
        scaffold.content_layout.addWidget(self.effect_summary)
        columns = QHBoxLayout()
        columns.setSpacing(14)
        scaffold.content_layout.addLayout(columns)
        columns.addWidget(self._build_mode_card(), 2)
        columns.addLayout(self._build_right_column(), 1)
        self.status_chip = StatusChip("Effects page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _build_mode_card(self) -> QWidget:
        card = SectionCard(
            "Effect Mode",
            "Choose a mode. The device applies the change immediately and queues it for autosave.",
        )
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        host = QWidget()
        layout = QVBoxLayout(host)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)
        scroll.setWidget(host)
        self.effect_mode_buttons = {}
        self._mode_group = QButtonGroup(self)
        self._mode_group.setExclusive(True)
        for group_name, modes in EFFECT_GROUPS:
            sub = SubCard()
            title = QLabel(group_name)
            title.setObjectName("CardTitle")
            sub.layout.addWidget(title)
            for value, label in modes:
                btn = QRadioButton(label)
                btn.toggled.connect(lambda checked, m=value: checked and not self._loading and self.on_effect_mode_change(m))
                sub.layout.addWidget(btn)
                self.effect_mode_buttons[value] = btn
                self._mode_group.addButton(btn)
            layout.addWidget(sub)
        layout.addStretch(1)
        card.body_layout.addWidget(scroll, 1)
        return card

    def _build_right_column(self) -> QVBoxLayout:
        col = QVBoxLayout()
        col.setSpacing(14)
        col.addWidget(self._build_tuning_card())
        col.addWidget(self._build_audio_preview_card())
        col.addWidget(self._build_fps_card())
        col.addStretch(1)
        return col

    def _build_audio_preview_card(self) -> QWidget:
        card = SectionCard(
            "Audio Spectrum Preview",
            "Shows host audio bands that are currently sent to the keyboard for Audio Spectrum mode.",
        )
        self.audio_preview_card = card
        self.audio_preview_widget = _AudioSpectrumPreview()
        card.body_layout.addWidget(self.audio_preview_widget)
        self.audio_preview_label = QLabel("Waiting for host audio data...")
        self.audio_preview_label.setObjectName("Muted")
        card.body_layout.addWidget(self.audio_preview_label)
        card.setVisible(False)
        return card

    def _build_tuning_card(self) -> QWidget:
        card = SectionCard(
            "Effect Tuning",
            "Each effect can expose sliders, toggles, and selects. These controls apply live and autosave after a short idle.",
        )
        self.param_container = QWidget()
        self.param_layout = QVBoxLayout(self.param_container)
        self.param_layout.setContentsMargins(0, 0, 0, 0)
        self.param_layout.setSpacing(10)
        card.body_layout.addWidget(self.param_container)
        return card

    def _build_fps_card(self) -> QWidget:
        card = SectionCard(
            "FPS Limit",
            "Caps the frame rate when the firmware supports throttling. Changes apply live and persist automatically.",
        )
        self.fps_slider = QSlider(Qt.Horizontal)
        self.fps_slider.setRange(0, 120)
        self.fps_slider.valueChanged.connect(self.on_fps_limit_change)
        card.body_layout.addWidget(self.fps_slider)
        self.fps_value = QLabel("60 FPS")
        self.fps_value.setAlignment(Qt.AlignCenter)
        self.fps_value.setObjectName("CardTitle")
        card.body_layout.addWidget(self.fps_value)
        return card

    def _update_status(self, message: str, kind: str = "info") -> None:
        self.status_chip.set_text_and_level(message, {"success": "ok", "warning": "warn", "error": "bad", "info": "info"}.get(kind, "neutral"))
        if self.controller and hasattr(self.controller, "set_status"):
            try:
                self.controller.set_status(message, kind)
            except TypeError:
                self.controller.set_status(message)

    def _set_effect_color(self, rgb) -> None:
        self.effect_color = [_clamp(ch) for ch in rgb[:3]]
        hex_color = "#{:02x}{:02x}{:02x}".format(*self.effect_color)
        if hasattr(self, "effect_color_preview"):
            self.effect_color_preview.setStyleSheet(
                f"border-radius: 12px; border: 1px solid palette(mid); background: {hex_color};"
            )
        if hasattr(self, "effect_color_label"):
            self.effect_color_label.setText(
                f"RGB({self.effect_color[0]}, {self.effect_color[1]}, {self.effect_color[2]})"
            )
        for preview in self.findChildren(QFrame):
            if preview.property("effectInlineColor") is True:
                preview.setStyleSheet(
                    f"border-radius: 10px; border: 1px solid palette(mid); background: {hex_color};"
                )

    def _apply_effect_color(self, rgb) -> None:
        color = [_clamp(channel) for channel in list(rgb)[:3]]
        while len(color) < 3:
            color.append(0)

        while len(self.effect_params) < LED_EFFECT_PARAM_COUNT:
            self.effect_params.append(0)

        self.effect_params[LED_EFFECT_PARAM_COLOR_R] = color[0]
        self.effect_params[LED_EFFECT_PARAM_COLOR_G] = color[1]
        self.effect_params[LED_EFFECT_PARAM_COLOR_B] = color[2]
        self._set_effect_color(color)

        try:
            ok = self.device.set_led_effect_params(self._current_mode(), self.effect_params)
        except Exception as exc:
            self._update_status(f"Failed to set effect color: {exc}", "error")
            return

        if ok:
            self._update_status("Effect color updated for the active mode.", "success")
        else:
            self._update_status("Device rejected effect color update.", "error")

    def _update_audio_preview_visibility(self, mode: int) -> None:
        visible = int(mode) == _AUDIO_EFFECT_ID
        self.audio_preview_card.setVisible(visible)
        if not visible:
            self.audio_preview_widget.set_levels([0] * 16, 0)
            self.audio_preview_label.setText("Preview appears when Audio Spectrum mode is active.")

    def _on_audio_preview_tick(self) -> None:
        mode = self._current_mode()
        if int(mode) != _AUDIO_EFFECT_ID:
            return

        if not self.controller or not getattr(self.controller, "session", None):
            self.audio_preview_widget.set_levels([0] * 16, 0)
            self.audio_preview_label.setText("Controller unavailable.")
            return

        if not self.controller.session.connected:
            self.audio_preview_widget.set_levels([0] * 16, 0)
            self.audio_preview_label.setText("Device disconnected.")
            return

        spectrum = getattr(self.controller, "_last_audio_spectrum", None)
        if not spectrum:
            self.audio_preview_widget.set_levels([0] * 16, 0)
            self.audio_preview_label.setText("No live spectrum yet. Play audio to feed the effect.")
            return

        levels = [_clamp(v) for v in list(spectrum)[:16]]
        if len(levels) < 16:
            levels.extend([0] * (16 - len(levels)))
        impact = min(255, int((max(levels) if levels else 0) * 1.6))
        viz_mode = int(self.effect_params[5]) if len(self.effect_params) > 5 else 0
        viz_contrast = int(self.effect_params[6]) if len(self.effect_params) > 6 else 236
        source = str(getattr(self.controller, "_last_audio_spectrum_source", "unknown"))
        if source == "fft":
            source_text = "Source: FFT loopback"
        elif source == "peak":
            source_text = "Source: output loudness fallback"
        else:
            source_text = "Source: unknown"
        self.audio_preview_widget.set_render_config(viz_mode, viz_contrast)
        self.audio_preview_widget.set_levels(levels, impact)
        self.audio_preview_label.setText(
            f"{source_text}  |  Max {max(levels)}  |  Avg {int(sum(levels) / max(1, len(levels)))}  |  Impact {impact}"
        )

    def _current_mode(self) -> int:
        for value, btn in self.effect_mode_buttons.items():
            if btn.isChecked():
                return int(value)
        return 0

    def _rebuild_param_ui(self, mode: int) -> None:
        while self.param_layout.count():
            item = self.param_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        metadata = _effect_specs(mode, self.effect_schema)
        if not metadata:
            label = QLabel("This effect has no additional tuning controls.")
            label.setObjectName("Muted")
            label.setWordWrap(True)
            self.param_layout.addWidget(label)
            self.param_layout.addStretch(1)
            return
        for spec in metadata:
            card = SubCard()
            title = QLabel(str(spec["label"]))
            title.setObjectName("CardTitle")
            card.layout.addWidget(title)
            kind = str(spec.get("kind", "slider"))
            if kind == "slider":
                index = int(spec["index"])
                value = int(self.effect_params[index])
                row = QHBoxLayout()
                slider = QSlider(Qt.Horizontal)
                slider.setRange(int(spec.get("min", 0)), int(spec.get("max", 255)))
                spin = QSpinBox()
                spin.setRange(int(spec.get("min", 0)), int(spec.get("max", 255)))
                slider.setValue(value)
                spin.setValue(value)
                value_label = QLabel(str(value))
                value_label.setObjectName("Muted")
                slider.valueChanged.connect(lambda new_value, s=spec, peer=spin, label=value_label: self._on_slider_value_changed(s, new_value, peer, label))
                spin.valueChanged.connect(lambda new_value, s=spec, peer=slider, label=value_label: self._on_slider_value_changed(s, new_value, peer, label))
                row.addWidget(slider, 1)
                row.addWidget(spin)
                row.addWidget(value_label)
                card.layout.addLayout(row)
            elif kind == "toggle":
                checkbox = QCheckBox("Enabled")
                value = int(self.effect_params[int(spec["index"])])
                checkbox.setChecked(bool(value))
                checkbox.toggled.connect(lambda checked, s=spec: self._on_param_value_changed(s, 1 if checked else 0))
                card.layout.addWidget(checkbox)
            elif kind == "select":
                value = int(self.effect_params[int(spec["index"])])
                combo = QComboBox()
                for opt_value, opt_label in spec.get("options", []):
                    combo.addItem(str(opt_label), int(opt_value))
                combo.setCurrentIndex(max(0, combo.findData(value)))
                combo.currentIndexChanged.connect(lambda _=0, s=spec, c=combo: self._on_param_value_changed(s, int(c.currentData())))
                card.layout.addWidget(combo)
            elif kind == "color":
                preview = QFrame()
                preview.setProperty("effectInlineColor", True)
                preview.setFixedHeight(36)
                hex_color = "#{:02x}{:02x}{:02x}".format(*self.effect_color)
                preview.setStyleSheet(
                    f"border-radius: 10px; border: 1px solid palette(mid); background: {hex_color};"
                )
                card.layout.addWidget(preview)
                card.layout.addWidget(make_secondary_button("Choose Color...", self.pick_effect_color))
            self.param_layout.addWidget(card)
        self.param_layout.addStretch(1)

    def _on_slider_value_changed(self, spec, value, peer, value_label):
        if self._loading:
            return
        value = _clamp(value)
        with QSignalBlocker(peer):
            peer.setValue(value)
        value_label.setText(str(value))
        self._on_param_value_changed(spec, value)

    def _on_param_value_changed(self, spec, value):
        if self._loading:
            return
        self.effect_params[int(spec["index"])] = _clamp(value)
        mode = self._current_mode()
        try:
            ok = self.device.set_led_effect_params(mode, self.effect_params)
        except Exception as exc:
            self._update_status(f"Failed to set effect tuning: {exc}", "error")
            return
        if ok:
            self._update_status(f"{spec['label']} set for {_effect_name(mode)}.", "success")
        else:
            self._update_status("Device rejected effect tuning values.", "error")

    def _load_effect_params(self, mode: int) -> None:
        self.effect_schema = _load_effect_schema(self.device, mode)
        defaults = _default_effect_params(mode, self.effect_schema)
        try:
            params = self.device.get_led_effect_params(mode)
        except Exception as exc:
            self._update_status(f"Failed to load effect params: {exc}", "warning")
            params = None
        if params:
            for i in range(min(LED_EFFECT_PARAM_COUNT, len(params))):
                defaults[i] = int(params[i])
        self.effect_params = defaults
        if len(self.effect_params) > LED_EFFECT_PARAM_COLOR_B:
            self._set_effect_color(
                [
                    int(self.effect_params[LED_EFFECT_PARAM_COLOR_R]),
                    int(self.effect_params[LED_EFFECT_PARAM_COLOR_G]),
                    int(self.effect_params[LED_EFFECT_PARAM_COLOR_B]),
                ]
            )
        self._rebuild_param_ui(mode)

    def on_effect_mode_change(self, mode: int) -> None:
        try:
            ok = self.device.set_led_effect(int(mode))
        except Exception as exc:
            self._update_status(f"Failed to set effect mode: {exc}", "error")
            self.reload()
            return
        if not ok:
            self._update_status("Device rejected the effect mode.", "error")
            self.reload()
            return
        self.effect_summary.setText(f"{_effect_name(int(mode))}: {_effect_description(int(mode))}")
        self._load_effect_params(int(mode))
        self._update_audio_preview_visibility(int(mode))
        self._on_audio_preview_tick()
        self._update_status(f"Effect mode set to {_effect_name(int(mode))} live.", "success")

    def on_fps_limit_change(self, value: int) -> None:
        fps = _clamp(value)
        self.fps_value.setText("Unlimited" if fps == 0 else f"{fps} FPS")
        if self._loading:
            return
        self.device.set_led_fps_limit(fps)

    def pick_effect_color(self) -> None:
        color = QColorDialog.getColor(QColor(*self.effect_color[:3]), self, "Choose Effect Color")
        if color.isValid():
            self._apply_effect_color([color.red(), color.green(), color.blue()])

    def reload(self) -> None:
        self._loading = True
        try:
            mode = self.device.get_led_effect()
            if mode in self.effect_mode_buttons:
                with QSignalBlocker(self.effect_mode_buttons[mode]):
                    self.effect_mode_buttons[mode].setChecked(True)
                name, desc = _effect_name(int(mode)), _effect_description(int(mode))
                self.effect_summary.setText(f"{name}: {desc}")
                self._load_effect_params(int(mode))
                self._update_audio_preview_visibility(int(mode))
                self._on_audio_preview_tick()
            fps = self.device.get_led_fps_limit()
            if fps is not None:
                with QSignalBlocker(self.fps_slider):
                    self.fps_slider.setValue(int(fps))
                self.fps_value.setText("Unlimited" if int(fps) == 0 else f"{int(fps)} FPS")
        finally:
            self._loading = False
        self._update_status("Effect settings loaded from device.", "success")

    load_led_effect_settings = reload
    refresh_from_device = reload

    def on_page_activated(self) -> None:
        self.audio_preview_timer.start()
        self.reload()

    def on_page_deactivated(self) -> None:
        self.audio_preview_timer.stop()
