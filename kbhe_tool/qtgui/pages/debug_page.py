from __future__ import annotations

import time

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QProgressBar,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ...key_layout import key_display_name, key_short_label
from ...protocol import KEY_COUNT
from ..theme import current_colors
from ..widgets import (
    KeyboardLayoutWidget,
    PageScaffold,
    SectionCard,
    StatusChip,
    make_primary_button,
    make_secondary_button,
)


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, int(value)))


def _mix_colors(color_a: str, color_b: str, ratio: float) -> str:
    ratio = max(0.0, min(1.0, float(ratio)))
    a = current_colors()
    from PySide6.QtGui import QColor

    left = QColor(color_a or a["surface"])
    right = QColor(color_b or a["accent"])
    r = int(left.red() + (right.red() - left.red()) * ratio)
    g = int(left.green() + (right.green() - left.green()) * ratio)
    b = int(left.blue() + (right.blue() - left.blue()) * ratio)
    return QColor(r, g, b).name()


class DebugPage(QWidget):
    DIAG_MODES = [
        (0, "Normal"),
        (1, "DMA Stress"),
        (2, "CPU Stress"),
        (3, "DMA + CPU"),
    ]

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        self._last_error = None
        self._tick_started = time.monotonic()
        self._tick_count = 0
        self._raw_values = [0] * KEY_COUNT
        self._states = [0] * KEY_COUNT
        self._distances_norm = [0] * KEY_COUNT
        self._distances_mm = [0.0] * KEY_COUNT
        self._scan_payload = {}

        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
            self.session.selectedKeyChanged.connect(self._on_selected_key_changed)
        except Exception:
            pass

        self._build_ui()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        self._on_selected_key_changed(self.session.selected_key)
        self.reload()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Debug / Sensors",
            "Vue maintenance centrée sur les 82 capteurs: état live, ADC bruts, timings firmware et modes de diagnostic LED.",
        )
        root.addWidget(scaffold, 1)

        columns = QHBoxLayout()
        columns.setSpacing(14)
        scaffold.content_layout.addLayout(columns)

        left = QVBoxLayout()
        left.setSpacing(14)
        right = QVBoxLayout()
        right.setSpacing(14)
        columns.addLayout(left, 2)
        columns.addLayout(right, 1)

        left.addWidget(self._build_monitor_card())
        left.addWidget(self._build_focus_card())
        left.addWidget(self._build_overview_card())
        left.addWidget(self._build_lock_card())
        left.addStretch(1)

        right.addWidget(self._build_config_card())
        right.addWidget(self._build_filter_card())
        right.addWidget(self._build_diag_card())
        right.addStretch(1)

        self.status_chip = StatusChip("Diagnostics page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _build_monitor_card(self) -> SectionCard:
        card = SectionCard(
            "Live Monitor",
            "La page ne poll que lorsqu’elle est visible et que le live global est activé.",
        )

        controls = QHBoxLayout()
        controls.setSpacing(8)
        live_lbl = QLabel("Global live")
        live_lbl.setObjectName("Muted")
        controls.addWidget(live_lbl)
        self.live_info = StatusChip("OFF", "neutral")
        controls.addWidget(self.live_info)
        controls.addStretch(1)
        card.body_layout.addLayout(controls)

        rate_row = QHBoxLayout()
        rate_row.setSpacing(8)
        scan_lbl = QLabel("Scan")
        scan_lbl.setObjectName("Muted")
        rate_row.addWidget(scan_lbl)
        self.scan_rate_chip = StatusChip("-- Hz", "info")
        rate_row.addWidget(self.scan_rate_chip)
        rate_row.addSpacing(12)
        gui_lbl = QLabel("GUI")
        gui_lbl.setObjectName("Muted")
        rate_row.addWidget(gui_lbl)
        self.gui_rate_chip = StatusChip("-- Hz", "info")
        rate_row.addWidget(self.gui_rate_chip)
        rate_row.addStretch(1)
        card.body_layout.addLayout(rate_row)

        self.summary_label = QLabel("Pressed -- / 82  ·  Max raw --  ·  Max travel --")
        self.summary_label.setObjectName("Muted")
        self.summary_label.setWordWrap(True)
        card.body_layout.addWidget(self.summary_label)

        self.task_times_label = QLabel(
            "Tasks: analog -- us | trigger -- us | socd -- us | kb -- us | nkro -- us | gp -- us | total -- us"
        )
        self.task_times_label.setObjectName("Muted")
        self.task_times_label.setWordWrap(True)
        card.body_layout.addWidget(self.task_times_label)

        self.analog_monitor_label = QLabel(
            "Analog: raw -- us | filter -- us | cal -- us | lut -- us | store -- us | min -- us | max -- us | avg -- us | nz -- | max key --"
        )
        self.analog_monitor_label.setObjectName("Muted")
        self.analog_monitor_label.setWordWrap(True)
        card.body_layout.addWidget(self.analog_monitor_label)
        return card

    def _build_focus_card(self) -> SectionCard:
        card = SectionCard(
            "Focused Key Signal",
            "La touche sélectionnée dans le layout global sert de focus pour les mesures détaillées.",
        )

        self.focus_name = QLabel(key_display_name(self.session.selected_key))
        self.focus_name.setObjectName("CardTitle")
        card.body_layout.addWidget(self.focus_name)

        chips = QHBoxLayout()
        chips.setSpacing(8)
        self.focus_state_chip = StatusChip("IDLE", "neutral")
        chips.addWidget(self.focus_state_chip)
        self.focus_raw_chip = StatusChip("RAW --", "info")
        chips.addWidget(self.focus_raw_chip)
        self.focus_distance_chip = StatusChip("--.-- mm", "neutral")
        chips.addWidget(self.focus_distance_chip)
        chips.addStretch(1)
        card.body_layout.addLayout(chips)

        self.focus_norm_bar = QProgressBar()
        self.focus_norm_bar.setRange(0, 255)
        self.focus_norm_bar.setFormat("Normalized %p%")
        card.body_layout.addWidget(self.focus_norm_bar)

        self.focus_raw_bar = QProgressBar()
        self.focus_raw_bar.setRange(0, 4095)
        self.focus_raw_bar.setFormat("Raw ADC %v")
        card.body_layout.addWidget(self.focus_raw_bar)

        self.focus_hint = QLabel("Sélectionne une touche dans le layout pour inspecter sa mesure brute et sa distance.")
        self.focus_hint.setObjectName("Muted")
        self.focus_hint.setWordWrap(True)
        card.body_layout.addWidget(self.focus_hint)
        return card

    def _build_overview_card(self) -> SectionCard:
        card = SectionCard(
            "Keyboard State Overview",
            "Heatmap des 82 touches. Le fond reflète la course mesurée, le contour vert indique une touche pressée.",
        )
        self.layout_view = KeyboardLayoutWidget(self.session, unit=38)
        card.body_layout.addWidget(self.layout_view)
        return card

    def _build_lock_card(self) -> SectionCard:
        card = SectionCard("Lock Indicators")
        self.lock_rows: dict[str, StatusChip] = {}
        for name in ("Caps Lock", "Num Lock", "Scroll Lock", "PE0 LED"):
            row = QHBoxLayout()
            lbl = QLabel(name)
            lbl.setObjectName("Muted")
            chip = StatusChip("OFF", "neutral")
            row.addWidget(lbl)
            row.addStretch(1)
            row.addWidget(chip)
            card.body_layout.addLayout(row)
            self.lock_rows[name] = chip
        return card

    def _build_config_card(self) -> SectionCard:
        card = SectionCard("Configuration Snapshot")
        self.config_text = QPlainTextEdit()
        self.config_text.setReadOnly(True)
        self.config_text.setMinimumHeight(240)
        card.body_layout.addWidget(self.config_text)
        card.body_layout.addWidget(make_secondary_button("Refresh Snapshot", self.refresh_config_display))
        return card

    def _build_filter_card(self) -> SectionCard:
        card = SectionCard("ADC EMA Filter")
        self.filter_enabled_check = QCheckBox("Enable filter")
        self.filter_enabled_check.toggled.connect(self.on_filter_enabled_change)
        card.body_layout.addWidget(self.filter_enabled_check)

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)

        self.noise_spin = QSpinBox()
        self.noise_spin.setRange(1, 100)
        self.alpha_min_spin = QSpinBox()
        self.alpha_min_spin.setRange(2, 128)
        self.alpha_max_spin = QSpinBox()
        self.alpha_max_spin.setRange(1, 32)

        for row_i, (label, spin) in enumerate(
            [
                ("Noise band", self.noise_spin),
                ("Alpha min denom", self.alpha_min_spin),
                ("Alpha max denom", self.alpha_max_spin),
            ]
        ):
            lbl = QLabel(label)
            lbl.setObjectName("Muted")
            grid.addWidget(lbl, row_i, 0)
            grid.addWidget(spin, row_i, 1)

        card.body_layout.addLayout(grid)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_primary_button("Apply Filter", self.apply_filter_settings))
        actions.addWidget(make_secondary_button("Reload", self.load_filter_settings))
        actions.addWidget(make_secondary_button("Reset Defaults", self.reset_filter_defaults))
        actions.addStretch(1)
        card.body_layout.addLayout(actions)
        return card

    def _build_diag_card(self) -> SectionCard:
        card = SectionCard(
            "LED Diagnostic Mode",
            "Modes de charge CPU / DMA pour isoler les interactions entre RGB, ADC et USB.",
        )
        self.diag_combo = QComboBox()
        for value, label in self.DIAG_MODES:
            self.diag_combo.addItem(label, value)
        card.body_layout.addWidget(self.diag_combo)
        card.body_layout.addWidget(make_primary_button("Apply Diagnostic Mode", self.on_diagnostic_mode_change))
        return card

    def _update_status(self, message: str, kind: str = "info") -> None:
        level_map = {"success": "ok", "warning": "warn", "error": "bad", "info": "info"}
        self.status_chip.set_text_and_level(message, level_map.get(kind, "neutral"))
        try:
            self.session.set_status(
                message,
                {"success": "ok", "warning": "warn", "error": "danger"}.get(kind, "info"),
            )
        except Exception:
            pass

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.live_info.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
        else:
            self.live_info.set_text_and_level("OFF", "neutral")

    def _on_selected_key_changed(self, key_index: int) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(key_index)))
        self.focus_name.setText(key_display_name(key_index))
        self._refresh_focus_card()

    def _refresh_focus_card(self) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))
        pressed = bool(self._states[key_index])
        raw_value = int(self._raw_values[key_index])
        norm = int(self._distances_norm[key_index])
        distance_mm = float(self._distances_mm[key_index])
        self.focus_state_chip.set_text_and_level("PRESSED" if pressed else "IDLE", "ok" if pressed else "neutral")
        self.focus_raw_chip.set_text_and_level(f"RAW {raw_value}", "info")
        self.focus_distance_chip.set_text_and_level(f"{distance_mm:.2f} mm", "info")
        self.focus_norm_bar.setValue(_clamp(norm, 0, 255))
        self.focus_raw_bar.setValue(_clamp(raw_value, 0, 4095))
        self.focus_hint.setText(
            f"{key_display_name(key_index)}  ·  state={'pressed' if pressed else 'idle'}  ·  normalized={norm}/255"
        )

    def _refresh_layout_view(self) -> None:
        colors = current_colors()
        pressed_count = 0
        max_raw = max(self._raw_values) if self._raw_values else 0
        max_raw_key = self._raw_values.index(max_raw) if self._raw_values else 0
        max_travel = max(self._distances_mm) if self._distances_mm else 0.0
        max_travel_key = self._distances_mm.index(max_travel) if self._distances_mm else 0

        for key_index in range(KEY_COUNT):
            raw_value = int(self._raw_values[key_index])
            norm = max(0.0, min(1.0, float(self._distances_norm[key_index]) / 255.0))
            pressed = bool(self._states[key_index])
            if pressed:
                pressed_count += 1
            fill = _mix_colors(colors["surface_muted"], colors["accent_soft"], norm)
            if pressed:
                fill = _mix_colors(fill, colors["success"], 0.38)
            border = colors["success"] if pressed else colors["border"]
            self.layout_view.set_key_state(
                key_index,
                title=key_short_label(key_index),
                subtitle="",
                fill=fill,
                border=border,
                tooltip=(
                    f"{key_display_name(key_index)}\n"
                    f"Raw ADC: {raw_value}\n"
                    f"Distance: {self._distances_mm[key_index]:.2f} mm\n"
                    f"State: {'PRESSED' if pressed else 'IDLE'}"
                ),
            )

        self.summary_label.setText(
            f"Pressed {pressed_count} / {KEY_COUNT}  ·  Max raw {key_display_name(max_raw_key)} = {max_raw}  ·  "
            f"Max travel {key_display_name(max_travel_key)} = {max_travel:.2f} mm"
        )

    def _poll_sensor_once(self) -> None:
        try:
            adc_data = self.device.get_adc_values() or {}
            raw_values = self.device.get_all_raw_adc_values() or []
            key_states = self.device.get_key_states() or {}
            lock_states = self.device.get_lock_states() or {}
        except Exception as exc:
            message = f"Live monitor error: {exc}"
            if message != self._last_error:
                self._last_error = message
                self._update_status(message, "error")
            return

        self._last_error = None
        self._scan_payload = adc_data

        for key_index in range(KEY_COUNT):
            self._raw_values[key_index] = int(raw_values[key_index]) if key_index < len(raw_values) else 0

        states = list(key_states.get("states") or [])
        normalized = list(key_states.get("distances") or [])
        distances_mm = list(key_states.get("distances_mm") or [])
        for key_index in range(KEY_COUNT):
            self._states[key_index] = int(states[key_index]) if key_index < len(states) else 0
            self._distances_norm[key_index] = int(normalized[key_index]) if key_index < len(normalized) else 0
            self._distances_mm[key_index] = float(distances_mm[key_index]) if key_index < len(distances_mm) else 0.0

        self._refresh_layout_view()
        self._refresh_focus_card()

        caps = bool(lock_states.get("caps_lock", False))
        num = bool(lock_states.get("num_lock", False))
        scroll = bool(lock_states.get("scroll_lock", False))
        self.lock_rows["Caps Lock"].set_text_and_level("ON" if caps else "OFF", "ok" if caps else "neutral")
        self.lock_rows["Num Lock"].set_text_and_level("ON" if num else "OFF", "ok" if num else "neutral")
        self.lock_rows["Scroll Lock"].set_text_and_level("ON" if scroll else "OFF", "ok" if scroll else "neutral")
        if caps and num:
            self.lock_rows["PE0 LED"].set_text_and_level("SOLID", "ok")
        elif caps and not num:
            self.lock_rows["PE0 LED"].set_text_and_level("FAST BLINK", "warn")
        elif num and not caps:
            self.lock_rows["PE0 LED"].set_text_and_level("SLOW BLINK", "warn")
        else:
            self.lock_rows["PE0 LED"].set_text_and_level("OFF", "neutral")

        scan_rate = adc_data.get("scan_rate_hz")
        scan_time = adc_data.get("scan_time_us")
        if scan_rate is not None and scan_time is not None:
            self.scan_rate_chip.setText(f"{scan_rate} Hz / {scan_time} us")
        else:
            self.scan_rate_chip.setText("-- Hz")

        task_times = adc_data.get("task_times_us") or {}
        if task_times:
            self.task_times_label.setText(
                "Tasks: "
                f"analog {task_times.get('analog', 0)} us | "
                f"trigger {task_times.get('trigger', 0)} us | "
                f"socd {task_times.get('socd', 0)} us | "
                f"kb {task_times.get('keyboard', 0)} us | "
                f"nkro {task_times.get('keyboard_nkro', 0)} us | "
                f"gp {task_times.get('gamepad', 0)} us | "
                f"total {task_times.get('total', 0)} us"
            )

        analog_monitor = adc_data.get("analog_monitor_us") or {}
        if analog_monitor:
            self.analog_monitor_label.setText(
                "Analog: "
                f"raw {analog_monitor.get('raw', 0)} us | "
                f"filter {analog_monitor.get('filter', 0)} us | "
                f"cal {analog_monitor.get('calibration', 0)} us | "
                f"lut {analog_monitor.get('lut', 0)} us | "
                f"store {analog_monitor.get('store', 0)} us | "
                f"min {analog_monitor.get('key_min', 0)} us | "
                f"max {analog_monitor.get('key_max', 0)} us | "
                f"avg {analog_monitor.get('key_avg', 0)} us | "
                f"nz {analog_monitor.get('nonzero_keys', 0)} | "
                f"max key {analog_monitor.get('key_max_index', 0)}"
            )

        self._tick_count += 1
        elapsed = time.monotonic() - self._tick_started
        if elapsed >= 1.0:
            self.gui_rate_chip.setText(f"{self._tick_count / elapsed:.1f} Hz")
            self._tick_started = time.monotonic()
            self._tick_count = 0

    def reload(self) -> None:
        self.load_filter_settings()
        self.refresh_config_display()
        try:
            mode = self.device.get_led_diagnostic()
        except Exception:
            mode = None
        if mode is not None:
            self.diag_combo.setCurrentIndex(max(0, self.diag_combo.findData(int(mode))))
        if self._page_active and self.session.live_enabled:
            self._poll_sensor_once()

    def refresh_config_display(self) -> None:
        try:
            version = self.device.get_firmware_version()
            options = self.device.get_options() or {}
            nkro = self.device.get_nkro_enabled()
            gamepad = self.device.get_gamepad_settings() or {}
            filter_enabled = self.device.get_filter_enabled()
            filter_params = self.device.get_filter_params() or {}
            led_effect = self.device.get_led_effect()
            led_speed = self.device.get_led_effect_speed()
            lines = [
                f"Firmware: {version or 'Unknown'}",
                f"Keyboard Enabled: {bool(options.get('keyboard_enabled'))}",
                f"Gamepad Enabled: {bool(options.get('gamepad_enabled'))}",
                f"Raw HID Echo: {bool(options.get('raw_hid_echo'))}",
                f"NKRO Enabled: {bool(nkro)}",
                "",
                "Gamepad:",
                f"  Deadzone: {gamepad.get('deadzone', '--')}",
                f"  Curve Type: {gamepad.get('curve_type', '--')}",
                f"  Square Mode: {bool(gamepad.get('square_mode', False))}",
                f"  Snappy Mode: {bool(gamepad.get('snappy_mode', False))}",
                "",
                "Filter:",
                f"  Enabled: {bool(filter_enabled)}",
                f"  Noise Band: {filter_params.get('noise_band', '--')}",
                f"  Alpha Min: 1/{filter_params.get('alpha_min_denom', '--')}",
                f"  Alpha Max: 1/{filter_params.get('alpha_max_denom', '--')}",
                "",
                "Lighting:",
                f"  Effect Mode: {led_effect}",
                f"  Effect Speed: {led_speed}",
            ]
        except Exception as exc:
            self._update_status(f"Failed to read configuration snapshot: {exc}", "error")
            return
        self.config_text.setPlainText("\n".join(lines))

    def load_filter_settings(self) -> None:
        try:
            enabled = self.device.get_filter_enabled()
            params = self.device.get_filter_params() or {}
        except Exception as exc:
            self._update_status(f"Failed to load filter settings: {exc}", "error")
            return
        self.filter_enabled_check.blockSignals(True)
        self.filter_enabled_check.setChecked(bool(enabled))
        self.filter_enabled_check.blockSignals(False)
        self.noise_spin.setValue(int(params.get("noise_band", 30)))
        self.alpha_min_spin.setValue(int(params.get("alpha_min_denom", 32)))
        self.alpha_max_spin.setValue(int(params.get("alpha_max_denom", 4)))

    def on_filter_enabled_change(self, enabled: bool) -> None:
        try:
            if not self.device.set_filter_enabled(bool(enabled)):
                raise RuntimeError("device rejected filter state")
        except Exception as exc:
            self._update_status(f"Failed to update filter state: {exc}", "error")
            return
        self._update_status(f"Filter {'enabled' if enabled else 'disabled'} live.", "success")

    def apply_filter_settings(self) -> None:
        noise = _clamp(self.noise_spin.value(), 1, 100)
        alpha_min = _clamp(self.alpha_min_spin.value(), 2, 128)
        alpha_max = _clamp(self.alpha_max_spin.value(), 1, 32)
        try:
            if not self.device.set_filter_params(noise, alpha_min, alpha_max):
                raise RuntimeError("device rejected filter params")
        except Exception as exc:
            self._update_status(f"Failed to apply filter params: {exc}", "error")
            return
        self._update_status(
            f"Filter params applied: band={noise}, alpha_min=1/{alpha_min}, alpha_max=1/{alpha_max}.",
            "success",
        )

    def reset_filter_defaults(self) -> None:
        self.filter_enabled_check.setChecked(True)
        self.noise_spin.setValue(30)
        self.alpha_min_spin.setValue(32)
        self.alpha_max_spin.setValue(4)
        self.apply_filter_settings()

    def on_diagnostic_mode_change(self) -> None:
        mode = int(self.diag_combo.currentData())
        try:
            if not self.device.set_led_diagnostic(mode):
                raise RuntimeError("device rejected diagnostic mode")
        except Exception as exc:
            self._update_status(f"Failed to apply diagnostic mode: {exc}", "error")
            return
        self._update_status(f"Diagnostic mode set to {self.diag_combo.currentText()}.", "success")

    def on_page_activated(self) -> None:
        self._page_active = True
        if self.session.live_enabled:
            self._poll_sensor_once()

    def on_page_deactivated(self) -> None:
        self._page_active = False

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._poll_sensor_once()
