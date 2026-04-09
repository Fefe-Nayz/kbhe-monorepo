from __future__ import annotations

from PySide6.QtCore import QSignalBlocker
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QCheckBox,
    QColorDialog,
    QComboBox,
    QFrame,
    QLabel,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ...protocol import (
    LED_EFFECT_NAMES,
    LEDEffect,
    ROTARY_ACTIONS,
    ROTARY_ACTION_NAMES,
    ROTARY_BUTTON_ACTIONS,
    ROTARY_BUTTON_ACTION_NAMES,
    ROTARY_PROGRESS_STYLES,
    ROTARY_PROGRESS_STYLE_NAMES,
    ROTARY_RGB_BEHAVIORS,
    ROTARY_RGB_BEHAVIOR_NAMES,
)
from ..widgets import (
    FormRow,
    PageScaffold,
    SectionCard,
    StatusChip,
    make_secondary_button,
)


def _clamp(value: int) -> int:
    return max(0, min(255, int(value)))


def _rgb_to_hex(rgb: list[int]) -> str:
    r, g, b = (_clamp(channel) for channel in rgb[:3])
    return f"#{r:02x}{g:02x}{b:02x}"


def _text_color_for_hex(hex_color: str) -> str:
    color = QColor(hex_color)
    luminance = (
        0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF()
    )
    return "#0f172a" if luminance >= 0.62 else "#f8fafc"


class RotaryEncoderPage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self._loading = False
        self.progress_color = [40, 210, 64]
        self._build_ui()
        self.reload()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Rotary Encoder",
            "Configure the encoder mapping, detection sensitivity, per-step size, "
            "and the RGB style used by the rotary progress bar.",
        )
        root.addWidget(scaffold, 1)

        scaffold.add_card(self._build_rotation_card())
        scaffold.add_card(self._build_button_card())
        scaffold.add_card(self._build_rgb_card())
        scaffold.add_card(self._build_progress_card())

        self.status_chip = StatusChip("Rotary encoder settings ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _build_rotation_card(self) -> SectionCard:
        card = SectionCard(
            "Rotation",
            "Sensitivity changes how eagerly a detent is detected. Step size changes how much one detent modifies the target.",
        )

        self.rotation_action_combo = QComboBox()
        for label, value in ROTARY_ACTIONS.items():
            self.rotation_action_combo.addItem(label, value)
        self.rotation_action_combo.currentIndexChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Rotation Action", self.rotation_action_combo))

        self.sensitivity_spin = QSpinBox()
        self.sensitivity_spin.setRange(1, 16)
        self.sensitivity_spin.setSingleStep(1)
        self.sensitivity_spin.valueChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Sensitivity", self.sensitivity_spin))

        self.step_size_spin = QSpinBox()
        self.step_size_spin.setRange(1, 64)
        self.step_size_spin.setSingleStep(1)
        self.step_size_spin.valueChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Step Size", self.step_size_spin))

        self.invert_toggle = QCheckBox("Invert direction")
        self.invert_toggle.toggled.connect(self._on_value_changed)
        card.body_layout.addWidget(self.invert_toggle)

        note = QLabel(
            "Volume uses distinct media key pulses. High step sizes can be very aggressive. LED-related modes show a temporary progress bar on the top row."
        )
        note.setObjectName("Muted")
        note.setWordWrap(True)
        card.body_layout.addWidget(note)
        return card

    def _build_button_card(self) -> SectionCard:
        card = SectionCard(
            "Button",
            "Choose what the encoder press does when the push switch is clicked.",
        )

        self.button_action_combo = QComboBox()
        for label, value in ROTARY_BUTTON_ACTIONS.items():
            self.button_action_combo.addItem(label, value)
        self.button_action_combo.currentIndexChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Button Action", self.button_action_combo))
        return card

    def _build_rgb_card(self) -> SectionCard:
        card = SectionCard(
            "RGB Customizer",
            "Used when Rotation Action is set to RGB Customizer.",
        )

        self.rgb_behavior_combo = QComboBox()
        for label, value in ROTARY_RGB_BEHAVIORS.items():
            self.rgb_behavior_combo.addItem(label, value)
        self.rgb_behavior_combo.currentIndexChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Behavior", self.rgb_behavior_combo))

        self.rgb_effect_combo = QComboBox()
        for effect, label in sorted(LED_EFFECT_NAMES.items(), key=lambda item: int(item[0])):
            if int(effect) == int(LEDEffect.THIRD_PARTY):
                continue
            self.rgb_effect_combo.addItem(label, int(effect))
        self.rgb_effect_combo.currentIndexChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Target Effect", self.rgb_effect_combo))

        self.rgb_hint = QLabel()
        self.rgb_hint.setObjectName("Muted")
        self.rgb_hint.setWordWrap(True)
        card.body_layout.addWidget(self.rgb_hint)
        return card

    def _build_progress_card(self) -> SectionCard:
        card = SectionCard(
            "Progress Bar",
            "Controls the top-row overlay used by rotary-driven progress feedback.",
        )

        self.progress_style_combo = QComboBox()
        for label, value in ROTARY_PROGRESS_STYLES.items():
            self.progress_style_combo.addItem(label, value)
        self.progress_style_combo.currentIndexChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Bar Style", self.progress_style_combo))

        self.progress_effect_combo = QComboBox()
        for effect, label in sorted(LED_EFFECT_NAMES.items(), key=lambda item: int(item[0])):
            if int(effect) == int(LEDEffect.THIRD_PARTY):
                continue
            self.progress_effect_combo.addItem(label, int(effect))
        self.progress_effect_combo.currentIndexChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("Effect Palette", self.progress_effect_combo))

        self.progress_color_preview = QFrame()
        self.progress_color_preview.setFixedHeight(42)
        card.body_layout.addWidget(self.progress_color_preview)

        self.progress_color_label = QLabel()
        self.progress_color_label.setObjectName("Muted")
        card.body_layout.addWidget(self.progress_color_label)

        self.progress_color_btn = make_secondary_button(
            "Choose Solid Color...", self._pick_progress_color
        )
        card.body_layout.addWidget(self.progress_color_btn)

        self.progress_hint = QLabel()
        self.progress_hint.setObjectName("Muted")
        self.progress_hint.setWordWrap(True)
        card.body_layout.addWidget(self.progress_hint)
        self._set_progress_color(self.progress_color)
        return card

    def _set_status(self, message: str, level: str = "info") -> None:
        self.status_chip.set_text_and_level(
            message,
            {"success": "ok", "warning": "warn", "error": "bad", "info": "info"}.get(level, "neutral"),
        )
        if self.controller is not None and hasattr(self.controller, "set_status"):
            try:
                self.controller.set_status(message, level)
            except TypeError:
                self.controller.set_status(message)

    def _set_progress_color(self, rgb: list[int]) -> None:
        self.progress_color = [_clamp(channel) for channel in rgb[:3]]
        hex_color = _rgb_to_hex(self.progress_color)
        self.progress_color_preview.setStyleSheet(
            "border-radius: 12px; "
            "border: 1px solid rgba(15, 23, 42, 0.18); "
            f"background: {hex_color};"
        )
        self.progress_color_label.setText(
            f"Solid color {hex_color.upper()}  |  RGB({self.progress_color[0]}, {self.progress_color[1]}, {self.progress_color[2]})"
        )
        self.progress_color_btn.setStyleSheet(
            "QPushButton {"
            f"background: {hex_color};"
            f"color: {_text_color_for_hex(hex_color)};"
            "border-radius: 8px;"
            "font-weight: 600;"
            "padding: 8px 14px;"
            "border: 1px solid rgba(0,0,0,0.15);"
            "}"
        )

    def _pick_progress_color(self) -> None:
        color = QColorDialog.getColor(QColor(*self.progress_color), self, "Choose Progress Bar Color")
        if not color.isValid():
            return
        self._set_progress_color([color.red(), color.green(), color.blue()])
        if not self._loading:
            self._apply_settings()

    def _current_settings(self) -> dict:
        return {
            "rotation_action": int(self.rotation_action_combo.currentData()),
            "button_action": int(self.button_action_combo.currentData()),
            "sensitivity": int(self.sensitivity_spin.value()),
            "step_size": int(self.step_size_spin.value()),
            "invert_direction": bool(self.invert_toggle.isChecked()),
            "rgb_behavior": int(self.rgb_behavior_combo.currentData()),
            "rgb_effect_mode": int(self.rgb_effect_combo.currentData()),
            "progress_style": int(self.progress_style_combo.currentData()),
            "progress_effect_mode": int(self.progress_effect_combo.currentData()),
            "progress_color": self.progress_color[:],
        }

    def _sync_hints(self) -> None:
        rotation_name = ROTARY_ACTION_NAMES.get(
            int(self.rotation_action_combo.currentData()), "Unknown"
        )
        behavior_name = ROTARY_RGB_BEHAVIOR_NAMES.get(
            int(self.rgb_behavior_combo.currentData()), "Unknown"
        )
        effect_name = self.rgb_effect_combo.currentText()
        style_name = ROTARY_PROGRESS_STYLE_NAMES.get(
            int(self.progress_style_combo.currentData()), "Unknown"
        )
        palette_name = self.progress_effect_combo.currentText()

        if rotation_name == "RGB Customizer":
            self.rgb_hint.setText(
                f"The encoder will drive {behavior_name.lower()} on {effect_name} with a step size of {self.step_size_spin.value()}."
            )
        else:
            self.rgb_hint.setText(
                f"RGB Customizer is prepared for {behavior_name.lower()} on {effect_name} and becomes active when mapped."
            )

        self.progress_color_btn.setEnabled(style_name == "Solid Color")
        self.progress_effect_combo.setEnabled(style_name == "Effect Palette")

        if style_name == "Solid Color":
            self.progress_hint.setText(
                "The top-row progress bar uses a single solid color that grows and shrinks with the current rotary value."
            )
        elif style_name == "Rainbow Bar":
            self.progress_hint.setText(
                "The top-row progress bar uses a moving rainbow gradient, independent from the current keyboard effect."
            )
        else:
            self.progress_hint.setText(
                f"The top-row progress bar uses the {palette_name} palette so the overlay keeps an RGB look while still overriding the row."
            )

    def _apply_settings(self) -> None:
        try:
            if self.device.set_rotary_encoder_settings(self._current_settings()):
                self._sync_hints()
                self._set_status(
                    "Rotary encoder settings applied live. Save to Flash to persist them.",
                    "success",
                )
            else:
                self._set_status("Device rejected the rotary encoder settings.", "error")
        except Exception as exc:
            self._set_status(f"Failed to update rotary encoder settings: {exc}", "error")

    def _on_value_changed(self, *_args) -> None:
        if self._loading:
            return
        self._apply_settings()

    def reload(self) -> None:
        defaults = {
            "rotation_action": int(ROTARY_ACTIONS["Volume"]),
            "button_action": int(ROTARY_BUTTON_ACTIONS["Play / Pause"]),
            "sensitivity": 4,
            "step_size": 1,
            "invert_direction": False,
            "rgb_behavior": int(ROTARY_RGB_BEHAVIORS["Hue"]),
            "rgb_effect_mode": int(LEDEffect.SOLID),
            "progress_style": int(ROTARY_PROGRESS_STYLES["Solid Color"]),
            "progress_effect_mode": int(LEDEffect.RAINBOW),
            "progress_color": [40, 210, 64],
        }
        try:
            device_settings = self.device.get_rotary_encoder_settings() or {}
            settings = {**defaults, **device_settings}
        except Exception as exc:
            settings = defaults
            self._set_status(f"Load failed, showing defaults: {exc}", "warning")

        progress_color = list(settings.get("progress_color", defaults["progress_color"]))
        while len(progress_color) < 3:
            progress_color.append(0)

        self._loading = True
        try:
            with QSignalBlocker(self.rotation_action_combo):
                self.rotation_action_combo.setCurrentIndex(
                    max(0, self.rotation_action_combo.findData(int(settings["rotation_action"])))
                )
            with QSignalBlocker(self.button_action_combo):
                self.button_action_combo.setCurrentIndex(
                    max(0, self.button_action_combo.findData(int(settings["button_action"])))
                )
            with QSignalBlocker(self.sensitivity_spin):
                self.sensitivity_spin.setValue(int(settings["sensitivity"]))
            with QSignalBlocker(self.step_size_spin):
                self.step_size_spin.setValue(int(settings["step_size"]))
            with QSignalBlocker(self.invert_toggle):
                self.invert_toggle.setChecked(bool(settings["invert_direction"]))
            with QSignalBlocker(self.rgb_behavior_combo):
                self.rgb_behavior_combo.setCurrentIndex(
                    max(0, self.rgb_behavior_combo.findData(int(settings["rgb_behavior"])))
                )
            with QSignalBlocker(self.rgb_effect_combo):
                self.rgb_effect_combo.setCurrentIndex(
                    max(0, self.rgb_effect_combo.findData(int(settings["rgb_effect_mode"])))
                )
            with QSignalBlocker(self.progress_style_combo):
                self.progress_style_combo.setCurrentIndex(
                    max(0, self.progress_style_combo.findData(int(settings["progress_style"])))
                )
            with QSignalBlocker(self.progress_effect_combo):
                self.progress_effect_combo.setCurrentIndex(
                    max(0, self.progress_effect_combo.findData(int(settings["progress_effect_mode"])))
                )
            self._set_progress_color(progress_color)
            self._sync_hints()
        finally:
            self._loading = False

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
