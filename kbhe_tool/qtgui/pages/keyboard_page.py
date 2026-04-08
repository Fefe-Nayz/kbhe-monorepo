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

from kbhe_tool.protocol import HID_KEYCODES, HID_KEYCODE_NAMES, KEY_COUNT
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
                 decimals: int, scale: int, parent=None):
        super().__init__(parent)
        self._scale = scale
        self._decimals = decimals

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
        self.value_label.setText(f"{raw / self._scale:.{self._decimals}f} mm")

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
        card = SectionCard("HID Output", "Keycode this switch sends when keyboard mode is active.")
        parent.addWidget(card)
        bl = card.body_layout

        row = QHBoxLayout()
        row.setSpacing(10)
        lbl = QLabel("Keycode")
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
        card = SectionCard("SOCD", "Pair a key for last-input priority when opposing directions are pressed.")
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
        self.socd_combo.currentIndexChanged.connect(self._on_changed)
        self._controls.append(self.socd_combo)
        row.addWidget(self.socd_combo, 1)
        bl.addLayout(row)

        hint = QLabel("When both keys are pressed simultaneously, the last input wins.")
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        bl.addWidget(hint)

    def _build_rapid_card(self, parent: QVBoxLayout) -> None:
        card = SectionCard("Rapid Trigger", "Enables re-activation based on movement distance, not a fixed point.")
        parent.addWidget(card)
        bl = card.body_layout

        self.rapid_check = QCheckBox("Enable rapid trigger for this key")
        self.rapid_check.stateChanged.connect(self._on_rapid_toggled)
        self._controls.append(self.rapid_check)
        bl.addWidget(self.rapid_check)

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

        rt_press = self.rt_press_row.get_value()
        rt_release = (
            self.rt_release_row.get_value()
            if self.separate_check.isChecked()
            else rt_press
        )
        return {
            "hid_keycode":            HID_KEYCODES.get(self.keycode_combo.currentText(), HID_KEYCODES["Q"]),
            "actuation_point_mm":     self.fixed_actuation_row.get_value(),
            "release_point_mm":       self.fixed_release_row.get_value(),
            "rapid_trigger_enabled":  self.rapid_check.isChecked(),
            "rapid_trigger_activation": self.rt_activation_row.get_value(),
            "rapid_trigger_press":    rt_press,
            "rapid_trigger_release":  rt_release,
            "socd_pair":              socd,
            "disable_kb_on_gamepad":  self.disable_kb_check.isChecked(),
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

            rt_press   = _safe_float(s.get("rapid_trigger_press", 0.3))
            rt_release = _safe_float(s.get("rapid_trigger_release", 0.3))
            self.separate_check.setChecked(abs(rt_press - rt_release) > 0.0001)

            socd = s.get("socd_pair")
            self.socd_combo.setCurrentIndex(
                max(0, self.socd_combo.findData(255 if socd is None else int(socd)))
            )

            self.disable_kb_check.setChecked(bool(s.get("disable_kb_on_gamepad", False)))
            self._update_rapid_visibility()
            self._update_layout_summary(s)
        finally:
            self._loading = False

    def _update_layout_summary(self, settings: dict | None = None) -> None:
        if settings is None:
            settings = self._build_payload()
        key_name = HID_KEYCODE_NAMES.get(int(settings.get("hid_keycode", HID_KEYCODES["Q"])), "Q")
        actuation = _safe_float(settings.get("actuation_point_mm", 0.0))
        rt_enabled = bool(settings.get("rapid_trigger_enabled", False))
        rt_press = _safe_float(settings.get("rapid_trigger_press", 0.0))
        self.summary_keycode_chip.set_text_and_level(key_name, "info")
        self.summary_actuation_chip.set_text_and_level(f"{actuation:.2f} mm", "neutral")
        if rt_enabled:
            self.summary_rt_chip.set_text_and_level(f"RT {rt_press:.2f} mm", "ok")
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
            self._sync_from_settings(settings)
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
            if ok:
                self._set_status(f"Applied {key_display_name(idx)} live.", "ok")
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
            if failures:
                self._set_status(f"Applied with failures on Key(s) {failures}.", "warn")
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
