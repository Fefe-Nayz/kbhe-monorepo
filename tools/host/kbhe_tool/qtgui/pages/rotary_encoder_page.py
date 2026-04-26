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
    HID_KEYCODES,
    LED_EFFECT_NAMES,
    LEDEffect,
    ROTARY_ACTIONS,
    ROTARY_ACTION_NAMES,
    ROTARY_BINDING_LAYER_MODES,
    ROTARY_BINDING_MODE_NAMES,
    ROTARY_BINDING_MODES,
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

        self.cw_binding_widgets = self._build_binding_editor(
            card.body_layout, "CW Binding"
        )
        self.ccw_binding_widgets = self._build_binding_editor(
            card.body_layout, "CCW Binding"
        )

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

        self.click_binding_widgets = self._build_binding_editor(
            card.body_layout, "Click Binding"
        )
        return card

    def _build_binding_editor(self, body_layout, title: str):
        title_label = QLabel(title)
        title_label.setObjectName("Muted")
        body_layout.addWidget(title_label)

        mode_combo = QComboBox()
        for label, value in ROTARY_BINDING_MODES.items():
            mode_combo.addItem(label, value)
        mode_combo.currentIndexChanged.connect(self._on_value_changed)
        body_layout.addWidget(FormRow("Mode", mode_combo))

        keycode_combo = QComboBox()
        for label, value in HID_KEYCODES.items():
            keycode_combo.addItem(label, int(value))
        keycode_combo.currentIndexChanged.connect(self._on_value_changed)
        body_layout.addWidget(FormRow("Keycode", keycode_combo))

        modifier_spin = QSpinBox()
        modifier_spin.setRange(0, 255)
        modifier_spin.valueChanged.connect(self._on_value_changed)
        body_layout.addWidget(FormRow("Exact Mod Mask", modifier_spin))

        fallback_combo = QComboBox()
        for label, value in HID_KEYCODES.items():
            fallback_combo.addItem(label, int(value))
        fallback_combo.currentIndexChanged.connect(self._on_value_changed)
        body_layout.addWidget(FormRow("Fallback (No Match)", fallback_combo))

        layer_mode_combo = QComboBox()
        for label, value in ROTARY_BINDING_LAYER_MODES.items():
            layer_mode_combo.addItem(label, value)
        layer_mode_combo.currentIndexChanged.connect(self._on_value_changed)
        body_layout.addWidget(FormRow("Layer Target", layer_mode_combo))

        fixed_layer_spin = QSpinBox()
        fixed_layer_spin.setRange(0, 3)
        fixed_layer_spin.valueChanged.connect(self._on_value_changed)
        body_layout.addWidget(FormRow("Fixed Layer", fixed_layer_spin))

        return {
            "mode": mode_combo,
            "keycode": keycode_combo,
            "modifier_mask_exact": modifier_spin,
            "fallback_no_mod_keycode": fallback_combo,
            "layer_mode": layer_mode_combo,
            "layer_index": fixed_layer_spin,
        }

    def _binding_from_widgets(self, widgets) -> dict:
        return {
            "mode": int(widgets["mode"].currentData()),
            "keycode": int(widgets["keycode"].currentData()),
            "modifier_mask_exact": int(widgets["modifier_mask_exact"].value()),
            "fallback_no_mod_keycode": int(
                widgets["fallback_no_mod_keycode"].currentData()
            ),
            "layer_mode": int(widgets["layer_mode"].currentData()),
            "layer_index": int(widgets["layer_index"].value()),
        }

    def _set_widgets_from_binding(self, widgets, binding: dict) -> None:
        mode = int(binding.get("mode", 0))
        keycode = int(binding.get("keycode", 0))
        modifiers = int(binding.get("modifier_mask_exact", 0))
        fallback = int(binding.get("fallback_no_mod_keycode", 0))
        layer_mode = int(binding.get("layer_mode", 0))
        layer_index = int(binding.get("layer_index", 0))

        with QSignalBlocker(widgets["mode"]):
            widgets["mode"].setCurrentIndex(max(0, widgets["mode"].findData(mode)))
        with QSignalBlocker(widgets["keycode"]):
            widgets["keycode"].setCurrentIndex(
                max(0, widgets["keycode"].findData(keycode))
            )
        with QSignalBlocker(widgets["modifier_mask_exact"]):
            widgets["modifier_mask_exact"].setValue(max(0, min(255, modifiers)))
        with QSignalBlocker(widgets["fallback_no_mod_keycode"]):
            widgets["fallback_no_mod_keycode"].setCurrentIndex(
                max(0, widgets["fallback_no_mod_keycode"].findData(fallback))
            )
        with QSignalBlocker(widgets["layer_mode"]):
            widgets["layer_mode"].setCurrentIndex(
                max(0, widgets["layer_mode"].findData(layer_mode))
            )
        with QSignalBlocker(widgets["layer_index"]):
            widgets["layer_index"].setValue(max(0, min(3, layer_index)))

    def _update_binding_editor_enabled_state(self, widgets) -> None:
        mode = int(widgets["mode"].currentData())
        is_keycode = mode == int(ROTARY_BINDING_MODES["Keycode"])
        widgets["keycode"].setEnabled(is_keycode)
        widgets["modifier_mask_exact"].setEnabled(is_keycode)
        widgets["fallback_no_mod_keycode"].setEnabled(is_keycode)
        widgets["layer_mode"].setEnabled(is_keycode)
        widgets["layer_index"].setEnabled(
            is_keycode
            and int(widgets["layer_mode"].currentData())
            == int(ROTARY_BINDING_LAYER_MODES["Fixed Layer"])
        )

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
            "cw_binding": self._binding_from_widgets(self.cw_binding_widgets),
            "ccw_binding": self._binding_from_widgets(self.ccw_binding_widgets),
            "click_binding": self._binding_from_widgets(self.click_binding_widgets),
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
        cw_mode_name = ROTARY_BINDING_MODE_NAMES.get(
            int(self.cw_binding_widgets["mode"].currentData()), "Unknown"
        )
        ccw_mode_name = ROTARY_BINDING_MODE_NAMES.get(
            int(self.ccw_binding_widgets["mode"].currentData()), "Unknown"
        )
        click_mode_name = ROTARY_BINDING_MODE_NAMES.get(
            int(self.click_binding_widgets["mode"].currentData()), "Unknown"
        )

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

        self._update_binding_editor_enabled_state(self.cw_binding_widgets)
        self._update_binding_editor_enabled_state(self.ccw_binding_widgets)
        self._update_binding_editor_enabled_state(self.click_binding_widgets)
        self._set_status(
            f"Bindings: CW={cw_mode_name}, CCW={ccw_mode_name}, Click={click_mode_name}.",
            "info",
        )

    def _apply_settings(self) -> None:
        try:
            if self.device.set_rotary_encoder_settings(self._current_settings()):
                self._sync_hints()
                self._set_status(
                "Rotary encoder settings applied live and will autosave after a short idle.",
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
            "rgb_effect_mode": int(LEDEffect.SOLID_COLOR),
            "progress_style": int(ROTARY_PROGRESS_STYLES["Solid Color"]),
            "progress_effect_mode": int(LEDEffect.CYCLE_LEFT_RIGHT),
            "progress_color": [40, 210, 64],
            "cw_binding": {"mode": int(ROTARY_BINDING_MODES["Internal Action"]), "keycode": 0, "modifier_mask_exact": 0, "fallback_no_mod_keycode": 0, "layer_mode": int(ROTARY_BINDING_LAYER_MODES["Active Layer"]), "layer_index": 0},
            "ccw_binding": {"mode": int(ROTARY_BINDING_MODES["Internal Action"]), "keycode": 0, "modifier_mask_exact": 0, "fallback_no_mod_keycode": 0, "layer_mode": int(ROTARY_BINDING_LAYER_MODES["Active Layer"]), "layer_index": 0},
            "click_binding": {"mode": int(ROTARY_BINDING_MODES["Internal Action"]), "keycode": 0, "modifier_mask_exact": 0, "fallback_no_mod_keycode": 0, "layer_mode": int(ROTARY_BINDING_LAYER_MODES["Active Layer"]), "layer_index": 0},
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
            self._set_widgets_from_binding(
                self.cw_binding_widgets, settings.get("cw_binding", defaults["cw_binding"])
            )
            self._set_widgets_from_binding(
                self.ccw_binding_widgets, settings.get("ccw_binding", defaults["ccw_binding"])
            )
            self._set_widgets_from_binding(
                self.click_binding_widgets,
                settings.get("click_binding", defaults["click_binding"]),
            )
            self._set_progress_color(progress_color)
            self._sync_hints()
        finally:
            self._loading = False

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
