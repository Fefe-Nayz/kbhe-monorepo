from __future__ import annotations

from PySide6.QtCore import Qt, QSignalBlocker, QTimer
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QScrollArea,
    QFrame,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from kbhe_tool.protocol import (
    ADVANCED_TICK_RATE_DEFAULT,
    ADVANCED_TICK_RATE_MAX,
    ADVANCED_TICK_RATE_MIN,
    HID_KEYCODES,
    HID_KEYCODE_NAMES,
    KEY_BEHAVIORS,
    KEY_COUNT,
    SOCD_RESOLUTIONS,
)
from kbhe_tool.key_layout import key_display_name
from ..widgets import (
    KeyboardLayoutWidget,
    SectionCard,
    StatusChip,
    make_primary_button,
    make_secondary_button,
)


def _clamp(v, lo, hi):
    return max(lo, min(hi, v))


def _safe_float(v, default=0.0):
    try:
        return float(v)
    except Exception:
        return default


class _SliderRow(QWidget):
    """Compact slider with label and live mm readout."""

    def __init__(self, label: str, minimum: float, maximum: float, default: float,
                 decimals: int, scale: int, parent=None, suffix: str = " mm"):
        super().__init__(parent)
        self._scale = scale
        self._decimals = decimals
        self._suffix = suffix

        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(10)

        lbl = QLabel(label)
        lbl.setObjectName("Muted")
        lbl.setFixedWidth(165)
        row.addWidget(lbl)

        self.slider = QSlider(Qt.Horizontal)
        self.slider.setRange(int(minimum * scale), int(maximum * scale))
        self.slider.setValue(int(default * scale))
        row.addWidget(self.slider, 1)

        self.value_label = QLabel()
        self.value_label.setFixedWidth(68)
        self.value_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        row.addWidget(self.value_label)

        self.slider.valueChanged.connect(self._refresh_label)
        self._refresh_label(self.slider.value())

    def _refresh_label(self, raw: int) -> None:
        self.value_label.setText(
            f"{raw / self._scale:.{self._decimals}f}{self._suffix}"
        )

    def get_value(self) -> float:
        return self.slider.value() / self._scale

    def set_value(self, value: float) -> None:
        with QSignalBlocker(self.slider):
            self.slider.setValue(int(round(value * self._scale)))
        self._refresh_label(self.slider.value())


class KeyboardPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self._loading = False
        self._apply_timer = QTimer(self)
        self._apply_timer.setSingleShot(True)
        self._apply_timer.setInterval(120)
        self._apply_timer.timeout.connect(self._apply_now)
        self._controls: list[QWidget] = []

        self._build_ui()
        self._connect_session()
        self.reload()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Scrollable content
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        root.addWidget(scroll, 1)

        container = QWidget()
        cl = QVBoxLayout(container)
        cl.setContentsMargins(20, 20, 20, 20)
        cl.setSpacing(14)
        scroll.setWidget(container)

        # Status row (key badge + status chip)
        status_row = QHBoxLayout()
        status_row.setSpacing(10)
        self.key_badge = StatusChip(key_display_name(0), "info")
        self.status_chip = StatusChip("Ready", "neutral")
        status_row.addWidget(self.key_badge)
        status_row.addStretch(1)
        status_row.addWidget(self.status_chip)
        cl.addLayout(status_row)

        top_row = QHBoxLayout()
        top_row.setSpacing(14)
        top_row.setAlignment(Qt.AlignTop)
        cl.addLayout(top_row)

        top_row.addWidget(self._build_layout_card(), 2)

        side_cards = QVBoxLayout()
        side_cards.setSpacing(14)
        top_row.addLayout(side_cards, 1)
        self._build_keycode_card(side_cards)
        self._build_actions_card(side_cards)
        side_cards.addStretch(1)

        bottom_row = QHBoxLayout()
        bottom_row.setSpacing(14)
        bottom_row.setAlignment(Qt.AlignTop)
        cl.addLayout(bottom_row)

        left = QVBoxLayout()
        left.setSpacing(14)
        right = QVBoxLayout()
        right.setSpacing(14)
        bottom_row.addLayout(left, 1)
        bottom_row.addLayout(right, 1)

        self._build_actuation_card(left)
        self._build_socd_card(left)
        self._build_rapid_card(right)
        self._build_behavior_card(right)

        left.addStretch(1)
        right.addStretch(1)
        cl.addStretch(1)

    def _build_layout_card(self) -> SectionCard:
        card = SectionCard(
            "Keyboard Layout",
            "Sélectionne une touche sur le vrai layout 82 touches pour éditer immédiatement son comportement.",
        )

        self.layout_view = KeyboardLayoutWidget(self.session, unit=34)
        card.body_layout.addWidget(self.layout_view, 0, Qt.AlignTop | Qt.AlignLeft)

        info_row = QHBoxLayout()
        info_row.setSpacing(8)
        self.summary_keycode_chip = StatusChip("Q", "info")
        self.summary_actuation_chip = StatusChip("1.20 mm", "neutral")
        self.summary_rt_chip = StatusChip("RT Off", "neutral")
        info_row.addWidget(self.summary_keycode_chip)
        info_row.addWidget(self.summary_actuation_chip)
        info_row.addWidget(self.summary_rt_chip)
        info_row.addStretch(1)
        card.body_layout.addLayout(info_row)

        hint = QLabel(
            "Le contour bleu suit la touche focalisée. Les changements sont appliqués live sur la touche sélectionnée."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        card.body_layout.addWidget(hint)
        return card

    def _build_keycode_card(self, parent: QVBoxLayout) -> None:
        card = SectionCard(
            "Output Action",
            "Choose the action this switch sends when non-gamepad output is active: keyboard, media, browser, brightness, or mouse.",
        )
        parent.addWidget(card)
        bl = card.body_layout

        row = QHBoxLayout()
        row.setSpacing(10)
        lbl = QLabel("Action")
        lbl.setObjectName("Muted")
        lbl.setFixedWidth(165)
        row.addWidget(lbl)
        self.keycode_combo = QComboBox()
        self.keycode_combo.addItems(list(HID_KEYCODES.keys()))
        self.keycode_combo.currentIndexChanged.connect(self._on_changed)
        self._controls.append(self.keycode_combo)
        row.addWidget(self.keycode_combo, 1)
        bl.addLayout(row)

        self.disable_kb_check = QCheckBox("Disable keyboard output when key is in gamepad mode")
        self.disable_kb_check.stateChanged.connect(self._on_changed)
        self._controls.append(self.disable_kb_check)
        bl.addWidget(self.disable_kb_check)

    def _create_action_combo(self) -> QComboBox:
        combo = QComboBox()
        combo.addItems(list(HID_KEYCODES.keys()))
        combo.currentIndexChanged.connect(self._on_changed)
        self._controls.append(combo)
        return combo

    def _build_actuation_card(self, parent: QVBoxLayout) -> None:
        card = SectionCard("Fixed Actuation", "Used when rapid trigger is disabled.")
        parent.addWidget(card)
        bl = card.body_layout

        self.fixed_actuation_row = _SliderRow("Actuation point", 0.1, 4.0, 2.0, decimals=1, scale=10)
        self.fixed_release_row = _SliderRow("Release point", 0.1, 4.0, 1.8, decimals=1, scale=10)
        bl.addWidget(self.fixed_actuation_row)
        bl.addWidget(self.fixed_release_row)
        self.fixed_actuation_row.slider.valueChanged.connect(self._on_changed)
        self.fixed_release_row.slider.valueChanged.connect(self._on_changed)
        self._controls += [self.fixed_actuation_row.slider, self.fixed_release_row.slider]

        hint = QLabel("Key activates at the actuation point and releases at the release point.")
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        bl.addWidget(hint)

    def _build_socd_card(self, parent: QVBoxLayout) -> None:
        card = SectionCard("SOCD", "Pair a key and choose how simultaneous opposite inputs are resolved.")
        parent.addWidget(card)
        bl = card.body_layout

        row = QHBoxLayout()
        row.setSpacing(10)
        lbl = QLabel("Paired key")
        lbl.setObjectName("Muted")
        lbl.setFixedWidth(165)
        row.addWidget(lbl)
        self.socd_combo = QComboBox()
        self.socd_combo.addItem("None", 255)
        for i in range(KEY_COUNT):
            self.socd_combo.addItem(key_display_name(i), i)
        self.socd_combo.currentIndexChanged.connect(self._on_socd_changed)
        self._controls.append(self.socd_combo)
        row.addWidget(self.socd_combo, 1)
        bl.addLayout(row)

        resolution_row = QHBoxLayout()
        resolution_row.setSpacing(10)
        resolution_lbl = QLabel("Resolution")
        resolution_lbl.setObjectName("Muted")
        resolution_lbl.setFixedWidth(165)
        resolution_row.addWidget(resolution_lbl)
        self.socd_resolution_combo = QComboBox()
        for label, value in SOCD_RESOLUTIONS.items():
            self.socd_resolution_combo.addItem(label, value)
        self.socd_resolution_combo.currentIndexChanged.connect(self._on_socd_changed)
        self._controls.append(self.socd_resolution_combo)
        resolution_row.addWidget(self.socd_resolution_combo, 1)
        bl.addLayout(resolution_row)

        self.socd_hint = QLabel()
        self.socd_hint.setObjectName("Muted")
        self.socd_hint.setWordWrap(True)
        bl.addWidget(self.socd_hint)
        self._update_socd_hint()

    def _build_rapid_card(self, parent: QVBoxLayout) -> None:
        card = SectionCard("Rapid Trigger", "Enables re-activation based on movement distance, not a fixed point.")
        parent.addWidget(card)
        bl = card.body_layout

        self.rapid_check = QCheckBox("Enable rapid trigger for this key")
        self.rapid_check.stateChanged.connect(self._on_rapid_toggled)
        self._controls.append(self.rapid_check)
        bl.addWidget(self.rapid_check)

        self.continuous_rt_check = QCheckBox("Continuous rapid trigger until full release")
        self.continuous_rt_check.stateChanged.connect(self._on_changed)
        self._controls.append(self.continuous_rt_check)
        bl.addWidget(self.continuous_rt_check)

        self.rapid_body = QWidget()
        rapid_bl = QVBoxLayout(self.rapid_body)
        rapid_bl.setContentsMargins(0, 4, 0, 0)
        rapid_bl.setSpacing(8)
        bl.addWidget(self.rapid_body)

        self.rt_activation_row = _SliderRow("Activation distance", 0.1, 2.0, 0.5, decimals=2, scale=100)
        self.rt_press_row = _SliderRow("Press sensitivity", 0.1, 1.0, 0.3, decimals=2, scale=100)
        rapid_bl.addWidget(self.rt_activation_row)
        rapid_bl.addWidget(self.rt_press_row)
        self.rt_activation_row.slider.valueChanged.connect(self._on_changed)
        self.rt_press_row.slider.valueChanged.connect(self._on_changed)
        self._controls += [self.rt_activation_row.slider, self.rt_press_row.slider]

        self.separate_check = QCheckBox("Use separate release sensitivity")
        self.separate_check.stateChanged.connect(self._on_separate_toggled)
        self._controls.append(self.separate_check)
        rapid_bl.addWidget(self.separate_check)

        self.release_body = QWidget()
        release_bl = QVBoxLayout(self.release_body)
        release_bl.setContentsMargins(0, 0, 0, 0)
        release_bl.setSpacing(0)
        self.rt_release_row = _SliderRow("Release sensitivity", 0.1, 1.0, 0.3, decimals=2, scale=100)
        release_bl.addWidget(self.rt_release_row)
        self.rt_release_row.slider.valueChanged.connect(self._on_changed)
        self._controls.append(self.rt_release_row.slider)
        rapid_bl.addWidget(self.release_body)

        hint = QLabel("Fires when the key moves by the sensitivity threshold after initial actuation.")
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        bl.addWidget(hint)

        self._update_rapid_visibility()

    def _build_behavior_card(self, parent: QVBoxLayout) -> None:
        card = SectionCard(
            "Advanced Behavior",
            "Switch between normal output, tap-hold, toggle, or 1-4 dynamic travel zones.",
        )
        parent.addWidget(card)
        bl = card.body_layout

        self.tick_rate_row = _SliderRow(
            "Advanced tick rate",
            ADVANCED_TICK_RATE_MIN,
            ADVANCED_TICK_RATE_MAX,
            ADVANCED_TICK_RATE_DEFAULT,
            decimals=0,
            scale=1,
            suffix=" ticks",
        )
        self.tick_rate_row.slider.valueChanged.connect(self._on_changed)
        self._controls.append(self.tick_rate_row.slider)
        bl.addWidget(self.tick_rate_row)

        mode_row = QHBoxLayout()
        mode_row.setSpacing(10)
        mode_lbl = QLabel("Behavior")
        mode_lbl.setObjectName("Muted")
        mode_lbl.setFixedWidth(165)
        mode_row.addWidget(mode_lbl)
        self.behavior_combo = QComboBox()
        for label, value in KEY_BEHAVIORS.items():
            self.behavior_combo.addItem(label, value)
        self.behavior_combo.currentIndexChanged.connect(self._on_behavior_changed)
        self._controls.append(self.behavior_combo)
        mode_row.addWidget(self.behavior_combo, 1)
        bl.addLayout(mode_row)

        self.tap_hold_body = QWidget()
        tap_bl = QVBoxLayout(self.tap_hold_body)
        tap_bl.setContentsMargins(0, 0, 0, 0)
        tap_bl.setSpacing(8)
        tap_action_row = QHBoxLayout()
        tap_action_row.setSpacing(10)
        tap_action_lbl = QLabel("Hold action")
        tap_action_lbl.setObjectName("Muted")
        tap_action_lbl.setFixedWidth(165)
        tap_action_row.addWidget(tap_action_lbl)
        self.tap_hold_secondary_combo = self._create_action_combo()
        tap_action_row.addWidget(self.tap_hold_secondary_combo, 1)
        tap_bl.addLayout(tap_action_row)
        self.tap_hold_threshold_row = _SliderRow(
            "Hold threshold", 50, 1000, 200, decimals=0, scale=1, suffix=" ms"
        )
        self.tap_hold_threshold_row.slider.valueChanged.connect(self._on_changed)
        self._controls.append(self.tap_hold_threshold_row.slider)
        tap_bl.addWidget(self.tap_hold_threshold_row)
        bl.addWidget(self.tap_hold_body)

        self.toggle_body = QWidget()
        toggle_bl = QVBoxLayout(self.toggle_body)
        toggle_bl.setContentsMargins(0, 0, 0, 0)
        toggle_bl.setSpacing(8)
        self.toggle_threshold_row = _SliderRow(
            "Hold threshold", 50, 1000, 200, decimals=0, scale=1, suffix=" ms"
        )
        self.toggle_threshold_row.slider.valueChanged.connect(self._on_changed)
        self._controls.append(self.toggle_threshold_row.slider)
        toggle_bl.addWidget(self.toggle_threshold_row)
        toggle_hint = QLabel(
            "Quick tap toggles the primary action. Holding falls back to a momentary press."
        )
        toggle_hint.setObjectName("Muted")
        toggle_hint.setWordWrap(True)
        toggle_bl.addWidget(toggle_hint)
        bl.addWidget(self.toggle_body)

        self.dynamic_body = QWidget()
        dynamic_bl = QVBoxLayout(self.dynamic_body)
        dynamic_bl.setContentsMargins(0, 0, 0, 0)
        dynamic_bl.setSpacing(8)

        count_row = QHBoxLayout()
        count_row.setSpacing(10)
        count_lbl = QLabel("Zone count")
        count_lbl.setObjectName("Muted")
        count_lbl.setFixedWidth(165)
        count_row.addWidget(count_lbl)
        self.dynamic_zone_count_combo = QComboBox()
        for count in range(1, 5):
            self.dynamic_zone_count_combo.addItem(str(count), count)
        self.dynamic_zone_count_combo.currentIndexChanged.connect(self._on_behavior_changed)
        self._controls.append(self.dynamic_zone_count_combo)
        count_row.addWidget(self.dynamic_zone_count_combo, 1)
        dynamic_bl.addLayout(count_row)

        self.dynamic_zone_rows = []
        for zone_index in range(4):
            zone_widget = QWidget()
            zone_layout = QVBoxLayout(zone_widget)
            zone_layout.setContentsMargins(0, 0, 0, 0)
            zone_layout.setSpacing(6)

            zone_header = QLabel(f"Zone {zone_index + 1}")
            zone_header.setObjectName("Muted")
            zone_layout.addWidget(zone_header)

            zone_slider = _SliderRow(
                "Travel end", 0.1, 4.0, float(zone_index + 1), decimals=1, scale=10
            )
            zone_slider.slider.valueChanged.connect(self._on_changed)
            self._controls.append(zone_slider.slider)
            zone_layout.addWidget(zone_slider)

            zone_action_row = QHBoxLayout()
            zone_action_row.setSpacing(10)
            zone_action_lbl = QLabel("Action")
            zone_action_lbl.setObjectName("Muted")
            zone_action_lbl.setFixedWidth(165)
            zone_action_row.addWidget(zone_action_lbl)
            zone_combo = self._create_action_combo()
            zone_action_row.addWidget(zone_combo, 1)
            zone_layout.addLayout(zone_action_row)

            dynamic_bl.addWidget(zone_widget)
            self.dynamic_zone_rows.append((zone_widget, zone_slider, zone_combo))

        bl.addWidget(self.dynamic_body)

        hint = QLabel(
            "Tap-hold uses the primary action on tap and the secondary action on hold. Dynamic mapping swaps the active action by travel zone. "
            "Tick rate controls the delay between consecutive advanced actions (1 tick ~ one scan)."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        bl.addWidget(hint)

        self._update_behavior_visibility()

    def _build_gamepad_card(self, parent: QVBoxLayout) -> None:
        # (Gamepad output disable is already in keycode card — this card is kept
        # to avoid removing the feature from the layout flow.)
        pass

    def _build_actions_card(self, parent: QVBoxLayout) -> None:
        card = SectionCard("Actions")
        parent.addWidget(card)
        bl = card.body_layout

        btn_row = QHBoxLayout()
        btn_row.setSpacing(8)

        self.load_btn  = make_secondary_button("Load",         self.load_selected_key_settings)
        self.apply_btn = make_primary_button("Apply",          self._apply_now)
        self.all_btn   = make_secondary_button("Apply to All", self.apply_to_all_keys)

        btn_row.addWidget(self.load_btn)
        btn_row.addWidget(self.apply_btn)
        btn_row.addWidget(self.all_btn)
        btn_row.addStretch(1)
        bl.addLayout(btn_row)

        hint = QLabel("Live changes are sent immediately. Use Save to Flash (top bar) to persist them.")
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        bl.addWidget(hint)

    # ------------------------------------------------------------------
    # Visibility helpers
    # ------------------------------------------------------------------

    def _update_rapid_visibility(self) -> None:
        self.rapid_body.setVisible(self.rapid_check.isChecked())
        self.release_body.setVisible(
            self.rapid_check.isChecked() and self.separate_check.isChecked()
        )

    def _on_rapid_toggled(self, *_) -> None:
        self._update_rapid_visibility()
        self._on_changed()

    def _on_separate_toggled(self, *_) -> None:
        self._update_rapid_visibility()
        self._on_changed()

    def _on_socd_changed(self, *_) -> None:
        self._update_socd_hint()
        self._on_changed()

    def _on_behavior_changed(self, *_) -> None:
        self._update_behavior_visibility()
        self._on_changed()

    def _update_behavior_visibility(self) -> None:
        behavior = (
            int(self.behavior_combo.currentData())
            if hasattr(self, "behavior_combo") and self.behavior_combo.currentIndex() >= 0
            else int(KEY_BEHAVIORS["Normal"])
        )
        zone_count = (
            int(self.dynamic_zone_count_combo.currentData())
            if hasattr(self, "dynamic_zone_count_combo") and self.dynamic_zone_count_combo.currentIndex() >= 0
            else 1
        )
        self.tap_hold_body.setVisible(behavior == KEY_BEHAVIORS["Tap-Hold"])
        self.toggle_body.setVisible(behavior == KEY_BEHAVIORS["Toggle"])
        self.dynamic_body.setVisible(behavior == KEY_BEHAVIORS["Dynamic Mapping"])
        for index, (widget, _slider, _combo) in enumerate(getattr(self, "dynamic_zone_rows", []), start=1):
            widget.setVisible(index <= zone_count)

    # ------------------------------------------------------------------
    # Session wiring
    # ------------------------------------------------------------------

    def _connect_session(self) -> None:
        sig = getattr(self.session, "selectedKeyChanged", None)
        if sig is not None:
            try:
                sig.connect(self.on_selected_key_changed)
            except Exception:
                pass

    def _selected_key(self) -> int:
        return _clamp(int(getattr(self.session, "selected_key", 0)), 0, KEY_COUNT - 1)

    def _device(self):
        return getattr(self.session, "device", None)

    def _set_controls_enabled(self, enabled: bool) -> None:
        for w in self._controls:
            w.setEnabled(enabled)

    # ------------------------------------------------------------------
    # Status helpers
    # ------------------------------------------------------------------

    def _set_status(self, text: str, level: str = "neutral") -> None:
        self.status_chip.set_text_and_level(text, level)

    def _set_key_badge(self, idx: int) -> None:
        self.key_badge.set_text_and_level(key_display_name(idx), "info")

    # ------------------------------------------------------------------
    # Data helpers
    # ------------------------------------------------------------------

    def _build_payload(self) -> dict:
        socd = int(self.socd_combo.currentData()) if self.socd_combo.currentIndex() >= 0 else 255
        socd_resolution = (
            int(self.socd_resolution_combo.currentData())
            if self.socd_resolution_combo.currentIndex() >= 0
            else 0
        )
        behavior_mode = (
            int(self.behavior_combo.currentData())
            if self.behavior_combo.currentIndex() >= 0
            else int(KEY_BEHAVIORS["Normal"])
        )
        dynamic_zone_count = (
            int(self.dynamic_zone_count_combo.currentData())
            if self.dynamic_zone_count_combo.currentIndex() >= 0
            else 1
        )

        rt_press = self.rt_press_row.get_value()
        rt_release = (
            self.rt_release_row.get_value()
            if self.separate_check.isChecked()
            else rt_press
        )
        dynamic_zones = []
        for _widget, slider, combo in self.dynamic_zone_rows:
            dynamic_zones.append(
                {
                    "end_mm_tenths": int(round(slider.get_value() * 10.0)),
                    "hid_keycode": HID_KEYCODES.get(combo.currentText(), HID_KEYCODES["NO"]),
                }
            )
        return {
            "hid_keycode":            HID_KEYCODES.get(self.keycode_combo.currentText(), HID_KEYCODES["Q"]),
            "actuation_point_mm":     self.fixed_actuation_row.get_value(),
            "release_point_mm":       self.fixed_release_row.get_value(),
            "rapid_trigger_enabled":  self.rapid_check.isChecked(),
            "rapid_trigger_activation": self.rt_activation_row.get_value(),
            "rapid_trigger_press":    rt_press,
            "rapid_trigger_release":  rt_release,
            "continuous_rapid_trigger": self.continuous_rt_check.isChecked(),
            "socd_pair":              socd,
            "socd_resolution":        socd_resolution,
            "disable_kb_on_gamepad":  self.disable_kb_check.isChecked(),
            "behavior_mode":          behavior_mode,
            "hold_threshold_ms":      (
                self.tap_hold_threshold_row.get_value()
                if behavior_mode == KEY_BEHAVIORS["Tap-Hold"]
                else self.toggle_threshold_row.get_value()
            ),
            "secondary_hid_keycode":  HID_KEYCODES.get(
                self.tap_hold_secondary_combo.currentText(), HID_KEYCODES["NO"]
            ),
            "dynamic_zone_count":     dynamic_zone_count,
            "dynamic_zones":          dynamic_zones,
        }

    def _sync_from_settings(self, s: dict) -> None:
        self._loading = True
        try:
            name = HID_KEYCODE_NAMES.get(s.get("hid_keycode", HID_KEYCODES["Q"]), "Q")
            idx  = self.keycode_combo.findText(name)
            self.keycode_combo.setCurrentIndex(max(0, idx))

            self.fixed_actuation_row.set_value(_safe_float(s.get("actuation_point_mm", 2.0)))
            self.fixed_release_row.set_value(_safe_float(s.get("release_point_mm", 1.8)))

            self.rapid_check.setChecked(bool(s.get("rapid_trigger_enabled", False)))
            self.rt_activation_row.set_value(_safe_float(s.get("rapid_trigger_activation", 0.5)))
            self.rt_press_row.set_value(_safe_float(s.get("rapid_trigger_press", 0.3)))
            self.rt_release_row.set_value(_safe_float(s.get("rapid_trigger_release", 0.3)))
            self.continuous_rt_check.setChecked(bool(s.get("continuous_rapid_trigger", False)))

            rt_press   = _safe_float(s.get("rapid_trigger_press", 0.3))
            rt_release = _safe_float(s.get("rapid_trigger_release", 0.3))
            self.separate_check.setChecked(abs(rt_press - rt_release) > 0.0001)

            socd = s.get("socd_pair")
            self.socd_combo.setCurrentIndex(
                max(0, self.socd_combo.findData(255 if socd is None else int(socd)))
            )
            socd_resolution = int(s.get("socd_resolution", 0))
            self.socd_resolution_combo.setCurrentIndex(
                max(0, self.socd_resolution_combo.findData(socd_resolution))
            )

            self.disable_kb_check.setChecked(bool(s.get("disable_kb_on_gamepad", False)))
            behavior_mode = int(s.get("behavior_mode", KEY_BEHAVIORS["Normal"]))
            self.behavior_combo.setCurrentIndex(max(0, self.behavior_combo.findData(behavior_mode)))
            self.tap_hold_threshold_row.set_value(_safe_float(s.get("hold_threshold_ms", 200)))
            self.toggle_threshold_row.set_value(_safe_float(s.get("hold_threshold_ms", 200)))
            secondary_name = HID_KEYCODE_NAMES.get(
                int(s.get("secondary_hid_keycode", HID_KEYCODES["NO"])),
                "NO",
            )
            self.tap_hold_secondary_combo.setCurrentIndex(
                max(0, self.tap_hold_secondary_combo.findText(secondary_name))
            )
            dynamic_zone_count = int(s.get("dynamic_zone_count", 1))
            self.dynamic_zone_count_combo.setCurrentIndex(
                max(0, self.dynamic_zone_count_combo.findData(dynamic_zone_count))
            )
            dynamic_zones = s.get("dynamic_zones") or []
            for index, (_widget, slider, combo) in enumerate(self.dynamic_zone_rows):
                zone = dynamic_zones[index] if index < len(dynamic_zones) else {}
                slider.set_value(_safe_float(zone.get("end_mm", (index + 1) * 1.0)))
                zone_name = HID_KEYCODE_NAMES.get(int(zone.get("hid_keycode", HID_KEYCODES["NO"])), "NO")
                combo.setCurrentIndex(max(0, combo.findText(zone_name)))
            self._update_rapid_visibility()
            self._update_socd_hint()
            self._update_behavior_visibility()
            self._update_layout_summary(s)
        finally:
            self._loading = False

    def _update_socd_hint(self) -> None:
        pair = int(self.socd_combo.currentData()) if self.socd_combo.currentIndex() >= 0 else 255
        resolution = (
            int(self.socd_resolution_combo.currentData())
            if self.socd_resolution_combo.currentIndex() >= 0
            else 0
        )
        if pair == 255:
            self.socd_hint.setText("SOCD is disabled until a paired key is selected.")
        elif resolution == SOCD_RESOLUTIONS["Most Pressed Wins"]:
            self.socd_hint.setText("When both paired keys are held, the deeper press wins.")
        else:
            self.socd_hint.setText("When both paired keys are held, the last pressed key wins.")

    def _update_layout_summary(self, settings: dict | None = None) -> None:
        if settings is None:
            settings = self._build_payload()
        key_name = HID_KEYCODE_NAMES.get(int(settings.get("hid_keycode", HID_KEYCODES["Q"])), "Q")
        actuation = _safe_float(settings.get("actuation_point_mm", 0.0))
        rt_enabled = bool(settings.get("rapid_trigger_enabled", False))
        rt_press = _safe_float(settings.get("rapid_trigger_press", 0.0))
        behavior_mode = int(settings.get("behavior_mode", KEY_BEHAVIORS["Normal"]))
        self.summary_keycode_chip.set_text_and_level(key_name, "info")
        self.summary_actuation_chip.set_text_and_level(f"{actuation:.2f} mm", "neutral")
        if rt_enabled:
            suffix = " C" if bool(settings.get("continuous_rapid_trigger", False)) else ""
            self.summary_rt_chip.set_text_and_level(f"RT {rt_press:.2f} mm{suffix}", "ok")
        elif behavior_mode == KEY_BEHAVIORS["Tap-Hold"]:
            self.summary_rt_chip.set_text_and_level("Tap-Hold", "info")
        elif behavior_mode == KEY_BEHAVIORS["Toggle"]:
            self.summary_rt_chip.set_text_and_level("Toggle", "info")
        elif behavior_mode == KEY_BEHAVIORS["Dynamic Mapping"]:
            self.summary_rt_chip.set_text_and_level("Dynamic", "info")
        else:
            self.summary_rt_chip.set_text_and_level("RT Off", "neutral")

    # ------------------------------------------------------------------
    # Device operations
    # ------------------------------------------------------------------

    def reload(self) -> None:
        idx = self._selected_key()
        self._set_key_badge(idx)
        if not self._device():
            self._set_controls_enabled(False)
            self._set_status("No device connected.", "warn")
            return
        self._set_controls_enabled(True)
        self.load_selected_key_settings(idx)

    def load_selected_key_settings(self, key_idx: int | None = None) -> None:
        idx = _clamp(int(key_idx if key_idx is not None else self._selected_key()), 0, KEY_COUNT - 1)
        self._set_key_badge(idx)
        device = self._device()
        if not device:
            self._set_status("No device connected.", "warn")
            return
        try:
            settings = device.get_key_settings(idx)
            if not settings:
                self._set_status(f"No settings returned for {key_display_name(idx)}.", "warn")
                return
            tick_rate = device.get_advanced_tick_rate()
            self._sync_from_settings(settings)
            if tick_rate is not None:
                self.tick_rate_row.set_value(float(tick_rate))
            self._set_status(f"Loaded {key_display_name(idx)}.", "ok")
        except Exception as exc:
            self._set_status(f"Load error: {exc}", "bad")

    def _on_changed(self, *_) -> None:
        if self._loading or not self._device():
            return
        self._update_layout_summary()
        self._apply_timer.start()

    def _apply_now(self) -> None:
        device = self._device()
        if not device:
            self._set_status("No device connected.", "warn")
            return
        idx = self._selected_key()
        try:
            ok = device.set_key_settings_extended(idx, self._build_payload())
            tick_ok = device.set_advanced_tick_rate(int(round(self.tick_rate_row.get_value())))
            if ok and tick_ok:
                self._set_status(f"Applied {key_display_name(idx)} live.", "ok")
            elif ok:
                self._set_status(
                    f"Applied {key_display_name(idx)} live, but tick rate update failed.",
                    "warn",
                )
            else:
                self._set_status(f"Apply failed for {key_display_name(idx)}.", "bad")
        except Exception as exc:
            self._set_status(f"Apply error: {exc}", "bad")

    def apply_key_settings(self) -> None:
        self._apply_now()

    def apply_to_all_keys(self) -> None:
        device = self._device()
        if not device:
            self._set_status("No device connected.", "warn")
            return
        try:
            payload  = self._build_payload()
            failures = [
                i + 1 for i in range(KEY_COUNT)
                if not device.set_key_settings_extended(i, payload)
            ]
            tick_ok = device.set_advanced_tick_rate(int(round(self.tick_rate_row.get_value())))
            if failures:
                self._set_status(f"Applied with failures on Key(s) {failures}.", "warn")
            elif not tick_ok:
                self._set_status("Applied key settings, but tick rate update failed.", "warn")
            else:
                self._set_status(f"Applied to all {KEY_COUNT} keys live.", "ok")
        except Exception as exc:
            self._set_status(f"Apply-all error: {exc}", "bad")

    def on_selected_key_changed(self, idx: int) -> None:
        self._set_key_badge(_clamp(int(idx), 0, KEY_COUNT - 1))
        self.load_selected_key_settings(idx)

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        self._apply_timer.stop()
