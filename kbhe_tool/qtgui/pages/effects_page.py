from __future__ import annotations

from PySide6.QtCore import QSignalBlocker, Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QButtonGroup,
    QCheckBox,
    QColorDialog,
    QComboBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QRadioButton,
    QScrollArea,
    QSlider,
    QSpinBox,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from ...protocol import LED_EFFECT_PARAM_COUNT
from ..widgets import PageScaffold, SectionCard, StatusChip, SubCard, make_secondary_button

EFFECT_GROUPS = [
    ("Software Control", [(0, "Matrix (Software)"), (4, "Solid Color"), (14, "Third-Party Live")]),
    ("Ambient Motion", [(1, "Rainbow Wave"), (2, "Breathing"), (3, "Static Rainbow"), (5, "Plasma"), (6, "Fire"), (7, "Ocean Waves"), (8, "Matrix Rain"), (9, "Sparkle"), (10, "Breathing Rainbow"), (11, "Spiral"), (12, "Color Cycle")]),
    ("Reactive", [(13, "Reactive (Key Press)"), (15, "Sensor Distance")]),
]

EFFECT_METADATA = {
    0: ("Matrix (Software)", "Uses the editable matrix pattern from the Lighting tab."),
    1: ("Rainbow Wave", "Animated rainbow sweep across the physical layout."),
    2: ("Breathing", "Pulses the selected color."),
    3: ("Static Rainbow", "Rainbow colors without motion."),
    4: ("Solid Color", "A single steady color fill."),
    5: ("Plasma", "Fluid plasma motion."),
    6: ("Fire", "Animated fire simulation."),
    7: ("Ocean Waves", "Layered wave motion."),
    8: ("Matrix Rain", "Digital rain effect."),
    9: ("Sparkle", "Random sparkles."),
    10: ("Breathing Rainbow", "Breathing plus hue drift."),
    11: ("Spiral", "Spiral motion pattern."),
    12: ("Color Cycle", "Continuous cycling through hues."),
    13: ("Reactive (Key Press)", "Launches expanding color ripples from each key press."),
    14: ("Third-Party Live", "Shows the live frame controlled externally."),
    15: ("Sensor Distance", "Each key color follows its travel distance."),
}

EFFECT_PARAM_METADATA = {
    1: [{"index": 0, "label": "Horizontal Scale", "kind": "slider", "default": 160},
        {"index": 1, "label": "Vertical Scale", "kind": "slider", "default": 96},
        {"index": 2, "label": "Drift", "kind": "slider", "default": 160},
        {"index": 3, "label": "Saturation", "kind": "slider", "default": 255}],
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
         {"index": 2, "label": "Base Glow", "kind": "slider", "default": 0, "max": 64},
         {"index": 3, "label": "White Core", "kind": "toggle", "default": 1},
         {"index": 4, "label": "Gain", "kind": "slider", "default": 224}],
    15: [{"index": 0, "label": "Brightness Floor", "kind": "slider", "default": 32},
         {"index": 1, "label": "Hue Span", "kind": "slider", "default": 170, "min": 1},
         {"index": 2, "label": "Saturation", "kind": "slider", "default": 255},
         {"index": 3, "label": "Reverse Gradient", "kind": "toggle", "default": 0}],
}

_QUICK_COLORS = [("#ff3b30", "Red"), ("#34c759", "Green"), ("#0a84ff", "Blue"), ("#ffffff", "White"), ("#ffd60a", "Yellow"), ("#ff2d55", "Magenta"), ("#32ade6", "Cyan"), ("#ff9500", "Orange")]
DEFAULT_EFFECT_COLOR = [0, 255, 0]


def _clamp(value: int) -> int:
    return max(0, min(255, int(value)))


def _default_effect_params(mode: int) -> list[int]:
    defaults = [0] * LED_EFFECT_PARAM_COUNT
    for spec in EFFECT_PARAM_METADATA.get(int(mode), []):
        if "index" in spec:
            defaults[int(spec["index"])] = int(spec.get("default", 0))
    return defaults


class EffectsPage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self._loading = False
        self.effect_color = list(getattr(controller, "effect_color", DEFAULT_EFFECT_COLOR[:])) if controller else DEFAULT_EFFECT_COLOR[:]
        self.effect_params = [0] * LED_EFFECT_PARAM_COUNT
        self.param_rows = []
        self._build_ui()
        self.reload()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        scaffold = PageScaffold("Effects", "Choose a mode, tune it per effect, and set the color used by color-aware effects.")
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
        card = SectionCard("Effect Mode", "Choose a mode. The device applies the change immediately.")
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
        col.addWidget(self._build_speed_card())
        col.addWidget(self._build_tuning_card())
        col.addWidget(self._build_fps_card())
        col.addWidget(self._build_color_card())
        col.addStretch(1)
        return col

    def _build_speed_card(self) -> QWidget:
        card = SectionCard("Effect Speed", "Controls how quickly animated effects advance.")
        self.effect_speed_slider = QSlider(Qt.Horizontal)
        self.effect_speed_slider.setRange(1, 255)
        self.effect_speed_slider.valueChanged.connect(self.on_effect_speed_change)
        card.body_layout.addWidget(self.effect_speed_slider)
        self.effect_speed_value = QLabel("128")
        self.effect_speed_value.setAlignment(Qt.AlignCenter)
        self.effect_speed_value.setObjectName("CardTitle")
        card.body_layout.addWidget(self.effect_speed_value)
        return card

    def _build_tuning_card(self) -> QWidget:
        card = SectionCard("Effect Tuning", "Each effect can expose sliders, toggles, and selects.")
        self.param_container = QWidget()
        self.param_layout = QVBoxLayout(self.param_container)
        self.param_layout.setContentsMargins(0, 0, 0, 0)
        self.param_layout.setSpacing(10)
        card.body_layout.addWidget(self.param_container)
        return card

    def _build_fps_card(self) -> QWidget:
        card = SectionCard("FPS Limit", "Caps the frame rate when the firmware supports throttling.")
        self.fps_slider = QSlider(Qt.Horizontal)
        self.fps_slider.setRange(0, 120)
        self.fps_slider.valueChanged.connect(self.on_fps_limit_change)
        card.body_layout.addWidget(self.fps_slider)
        self.fps_value = QLabel("60 FPS")
        self.fps_value.setAlignment(Qt.AlignCenter)
        self.fps_value.setObjectName("CardTitle")
        card.body_layout.addWidget(self.fps_value)
        return card

    def _build_color_card(self) -> QWidget:
        card = SectionCard("Effect Color", "Used by Solid, Breathing, Reactive, and related effects.")
        self.effect_color_preview = QFrame()
        self.effect_color_preview.setFixedHeight(48)
        card.body_layout.addWidget(self.effect_color_preview)
        self.effect_color_label = QLabel()
        self.effect_color_label.setObjectName("Muted")
        card.body_layout.addWidget(self.effect_color_label)
        card.body_layout.addWidget(make_secondary_button("Choose Color...", self.pick_effect_color))
        swatch_host = QWidget()
        swatch_grid = QGridLayout(swatch_host)
        swatch_grid.setContentsMargins(0, 0, 0, 0)
        for i, (hex_color, label) in enumerate(_QUICK_COLORS):
            swatch = QToolButton()
            swatch.setText(label)
            swatch.setFixedSize(76, 34)
            light = hex_color in ("#ffffff", "#ffd60a", "#64d2ff")
            swatch.setStyleSheet(f"QToolButton {{ background: {hex_color}; color: {'#000000' if light else '#ffffff'}; border-radius: 8px; font-weight: 600; border: 1px solid rgba(0,0,0,0.15); }}")
            swatch.clicked.connect(lambda _=False, h=hex_color: self.set_effect_color_hex(h))
            swatch_grid.addWidget(swatch, i // 4, i % 4)
        card.body_layout.addWidget(swatch_host)
        self._set_effect_color(self.effect_color)
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
        if self.controller is not None:
            try:
                self.controller.effect_color = self.effect_color
            except Exception:
                pass
        hex_color = "#{:02x}{:02x}{:02x}".format(*self.effect_color)
        self.effect_color_preview.setStyleSheet(f"border-radius: 12px; border: 1px solid palette(mid); background: {hex_color};")
        self.effect_color_label.setText(f"RGB({self.effect_color[0]}, {self.effect_color[1]}, {self.effect_color[2]})")
        for preview in self.findChildren(QFrame):
            if preview.property("effectInlineColor") is True:
                preview.setStyleSheet(
                    f"border-radius: 10px; border: 1px solid palette(mid); background: {hex_color};"
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
        metadata = EFFECT_PARAM_METADATA.get(mode, [])
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
            self._update_status(f"{spec['label']} set for {EFFECT_METADATA.get(mode, (f'Mode {mode}', ''))[0]}.", "success")
        else:
            self._update_status("Device rejected effect tuning values.", "error")

    def _load_effect_params(self, mode: int) -> None:
        defaults = _default_effect_params(mode)
        try:
            params = self.device.get_led_effect_params(mode)
        except Exception as exc:
            self._update_status(f"Failed to load effect params: {exc}", "warning")
            params = None
        if params:
            for i in range(min(LED_EFFECT_PARAM_COUNT, len(params))):
                defaults[i] = int(params[i])
        self.effect_params = defaults
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
        self.effect_summary.setText(f"{EFFECT_METADATA.get(int(mode), (f'Mode {mode}', ''))[0]}: {EFFECT_METADATA.get(int(mode), ('', ''))[1]}")
        self._load_effect_params(int(mode))
        self._update_status(f"Effect mode set to {EFFECT_METADATA.get(int(mode), (f'Mode {mode}', ''))[0]} live.", "success")

    def on_effect_speed_change(self, value: int) -> None:
        speed = _clamp(value)
        self.effect_speed_value.setText(str(speed))
        if self._loading:
            return
        self.device.set_led_effect_speed(speed)

    def on_fps_limit_change(self, value: int) -> None:
        fps = _clamp(value)
        self.fps_value.setText("Unlimited" if fps == 0 else f"{fps} FPS")
        if self._loading:
            return
        self.device.set_led_fps_limit(fps)

    def pick_effect_color(self) -> None:
        color = QColorDialog.getColor(QColor(*self.effect_color[:3]), self, "Choose Effect Color")
        if color.isValid():
            self._set_effect_color([color.red(), color.green(), color.blue()])
            self.device.set_led_effect_color(color.red(), color.green(), color.blue())

    def set_effect_color_hex(self, hex_color: str) -> None:
        color = QColor(hex_color)
        if color.isValid():
            self._set_effect_color([color.red(), color.green(), color.blue()])
            self.device.set_led_effect_color(color.red(), color.green(), color.blue())

    def reload(self) -> None:
        self._loading = True
        try:
            mode = self.device.get_led_effect()
            if mode in self.effect_mode_buttons:
                with QSignalBlocker(self.effect_mode_buttons[mode]):
                    self.effect_mode_buttons[mode].setChecked(True)
                name, desc = EFFECT_METADATA.get(int(mode), (f"Mode {mode}", ""))
                self.effect_summary.setText(f"{name}: {desc}")
                self._load_effect_params(int(mode))
            speed = self.device.get_led_effect_speed()
            if speed is not None:
                with QSignalBlocker(self.effect_speed_slider):
                    self.effect_speed_slider.setValue(int(speed))
                self.effect_speed_value.setText(str(int(speed)))
            fps = self.device.get_led_fps_limit()
            if fps is not None:
                with QSignalBlocker(self.fps_slider):
                    self.fps_slider.setValue(int(fps))
                self.fps_value.setText("Unlimited" if int(fps) == 0 else f"{int(fps)} FPS")
            effect_color = self.device.get_led_effect_color()
            if effect_color is not None and len(effect_color) >= 3:
                self._set_effect_color(effect_color)
        finally:
            self._loading = False
        self._update_status("Effect settings loaded from device.", "success")

    load_led_effect_settings = reload
    refresh_from_device = reload

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
