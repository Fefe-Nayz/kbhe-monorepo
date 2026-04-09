from __future__ import annotations

from PySide6.QtCore import QSignalBlocker
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
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
    ROTARY_RGB_BEHAVIORS,
    ROTARY_RGB_BEHAVIOR_NAMES,
)
from ..widgets import FormRow, PageScaffold, SectionCard, StatusChip


class RotaryEncoderPage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self._loading = False
        self._build_ui()
        self.reload()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Rotary Encoder",
            "Configure the encoder mapping, response, and RGB customizer behavior. "
            "Changes apply live; use Save to Flash to persist them.",
        )
        root.addWidget(scaffold, 1)

        scaffold.add_card(self._build_rotation_card())
        scaffold.add_card(self._build_button_card())
        scaffold.add_card(self._build_rgb_card())

        self.status_chip = StatusChip("Rotary encoder settings ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _build_rotation_card(self) -> SectionCard:
        card = SectionCard(
            "Rotation",
            "Choose what turning the encoder does and how strong each step should be.",
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

        self.invert_toggle = QCheckBox("Invert direction")
        self.invert_toggle.toggled.connect(self._on_value_changed)
        card.body_layout.addWidget(self.invert_toggle)

        note = QLabel(
            "Volume repeats media steps. LED-related modes change the current live LED state."
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

        self.rgb_step_spin = QSpinBox()
        self.rgb_step_spin.setRange(1, 32)
        self.rgb_step_spin.setSingleStep(1)
        self.rgb_step_spin.valueChanged.connect(self._on_value_changed)
        card.body_layout.addWidget(FormRow("RGB Step Size", self.rgb_step_spin))

        self.rgb_hint = QLabel()
        self.rgb_hint.setObjectName("Muted")
        self.rgb_hint.setWordWrap(True)
        card.body_layout.addWidget(self.rgb_hint)
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

    def _current_settings(self) -> dict:
        return {
            "rotation_action": int(self.rotation_action_combo.currentData()),
            "button_action": int(self.button_action_combo.currentData()),
            "sensitivity": int(self.sensitivity_spin.value()),
            "invert_direction": bool(self.invert_toggle.isChecked()),
            "rgb_behavior": int(self.rgb_behavior_combo.currentData()),
            "rgb_effect_mode": int(self.rgb_effect_combo.currentData()),
            "rgb_step": int(self.rgb_step_spin.value()),
        }

    def _sync_rgb_hint(self) -> None:
        rotation_name = ROTARY_ACTION_NAMES.get(
            int(self.rotation_action_combo.currentData()), "Unknown"
        )
        behavior_name = ROTARY_RGB_BEHAVIOR_NAMES.get(
            int(self.rgb_behavior_combo.currentData()), "Unknown"
        )
        effect_name = self.rgb_effect_combo.currentText()
        if rotation_name == "RGB Customizer":
            self.rgb_hint.setText(
                f"The encoder will drive {behavior_name.lower()} using the {effect_name} effect."
            )
        else:
            self.rgb_hint.setText(
                f"RGB Customizer is prepared for {behavior_name.lower()} on {effect_name} and becomes active when mapped."
            )

    def _apply_settings(self) -> None:
        try:
            if self.device.set_rotary_encoder_settings(self._current_settings()):
                self._sync_rgb_hint()
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
            "sensitivity": 1,
            "invert_direction": False,
            "rgb_behavior": int(ROTARY_RGB_BEHAVIORS["Hue"]),
            "rgb_effect_mode": int(LEDEffect.SOLID),
            "rgb_step": 8,
        }
        try:
            settings = self.device.get_rotary_encoder_settings() or defaults
        except Exception as exc:
            settings = defaults
            self._set_status(f"Load failed, showing defaults: {exc}", "warning")

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
            with QSignalBlocker(self.rgb_step_spin):
                self.rgb_step_spin.setValue(int(settings["rgb_step"]))
            self._sync_rgb_hint()
        finally:
            self._loading = False

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
