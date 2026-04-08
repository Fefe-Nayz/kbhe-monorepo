from __future__ import annotations

import time

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

from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
    SubCard,
    make_primary_button,
    make_secondary_button,
)


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, int(value)))


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
        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
        except Exception:
            pass
        self._build_ui()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        self.reload()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Debug / Sensors",
            "Inspect ADC values, live key travel, filter settings, lock indicators, "
            "and LED diagnostic modes from one maintenance-focused screen.",
        )
        root.addWidget(scaffold, 1)

        # Two-column layout
        columns = QHBoxLayout()
        columns.setSpacing(14)
        scaffold.content_layout.addLayout(columns)

        left = QVBoxLayout()
        left.setSpacing(14)
        right = QVBoxLayout()
        right.setSpacing(14)
        columns.addLayout(left, 1)
        columns.addLayout(right, 1)

        left.addWidget(self._build_monitor_card())
        left.addWidget(self._build_adc_card())
        left.addWidget(self._build_key_states_card())
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
            "The page polls only while active and when live updates are enabled.",
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

    def _build_adc_card(self) -> SectionCard:
        card = SectionCard(
            "ADC Values",
            "Raw ADC readings, normalized against the expected Hall range.",
        )

        self.adc_rows: list[tuple[QProgressBar, QLabel, QLabel]] = []
        for i in range(6):
            row = QHBoxLayout()
            name = QLabel(f"Key {i + 1}")
            name.setObjectName("Muted")
            bar = QProgressBar()
            bar.setRange(0, 700)
            value = QLabel("----")
            distance = QLabel("-.-- mm")
            distance.setObjectName("Muted")
            row.addWidget(name)
            row.addWidget(bar, 1)
            row.addWidget(value)
            row.addWidget(distance)
            card.body_layout.addLayout(row)
            self.adc_rows.append((bar, value, distance))

        return card

    def _build_key_states_card(self) -> SectionCard:
        card = SectionCard("Key States")

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(10)
        card.body_layout.addLayout(grid)

        self.key_state_labels: list[StatusChip] = []
        self.key_state_bars: list[QProgressBar] = []
        for i in range(6):
            sub = SubCard()
            name = QLabel(f"Key {i + 1}")
            name.setObjectName("Muted")
            sub.layout.addWidget(name)
            chip = StatusChip("IDLE", "neutral")
            sub.layout.addWidget(chip)
            bar = QProgressBar()
            bar.setRange(0, 255)
            sub.layout.addWidget(bar)
            grid.addWidget(sub, i // 3, i % 3)
            self.key_state_labels.append(chip)
            self.key_state_bars.append(bar)

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

        helper = QLabel(
            "The PE0 LED state is inferred from lock combinations, "
            "matching the old diagnostic page."
        )
        helper.setObjectName("Muted")
        helper.setWordWrap(True)
        card.body_layout.addWidget(helper)

        return card

    def _build_config_card(self) -> SectionCard:
        card = SectionCard("Configuration Snapshot")
        self.config_text = QPlainTextEdit()
        self.config_text.setReadOnly(True)
        self.config_text.setMinimumHeight(240)
        card.body_layout.addWidget(self.config_text)
        card.body_layout.addWidget(
            make_secondary_button("Refresh Snapshot", self.refresh_config_display)
        )
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

        for row_i, (label, spin) in enumerate([
            ("Noise band", self.noise_spin),
            ("Alpha min denom", self.alpha_min_spin),
            ("Alpha max denom", self.alpha_max_spin),
        ]):
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
            "Use these modes to isolate DMA load vs CPU load when chasing analog noise.",
        )

        self.diag_combo = QComboBox()
        for value, label in self.DIAG_MODES:
            self.diag_combo.addItem(label, value)
        card.body_layout.addWidget(self.diag_combo)
        card.body_layout.addWidget(
            make_primary_button("Apply Diagnostic Mode", self.on_diagnostic_mode_change)
        )

        return card

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

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

    # ------------------------------------------------------------------
    # Live polling
    # ------------------------------------------------------------------

    def _poll_sensor_once(self) -> None:
        try:
            adc_data = self.device.get_adc_values() or {}
            key_states = self.device.get_key_states() or {}
            lock_states = self.device.get_lock_states() or {}
        except Exception as exc:
            message = f"Live monitor error: {exc}"
            if message != self._last_error:
                self._last_error = message
                self._update_status(message, "error")
            return

        self._last_error = None

        adc_raw = list(adc_data.get("adc_raw") or adc_data.get("adc") or [])
        adc_filtered = list(adc_data.get("adc_filtered") or adc_raw)
        distances_mm = list(key_states.get("distances_mm") or [])
        for i, (bar, value_label, distance_label) in enumerate(self.adc_rows):
            raw_value = int(adc_raw[i]) if i < len(adc_raw) else None
            filtered_value = int(adc_filtered[i]) if i < len(adc_filtered) else None

            if raw_value is None:
                bar.setValue(0)
                value_label.setText("----")
            else:
                shown = filtered_value if filtered_value is not None else raw_value
                bar.setValue(_clamp(shown - 2000, 0, 700))
                if filtered_value is None:
                    value_label.setText(f"R:{raw_value:4d}")
                else:
                    value_label.setText(f"R:{raw_value:4d} F:{filtered_value:4d}")
            distance_label.setText(
                f"{distances_mm[i]:.2f} mm" if i < len(distances_mm) else "-.-- mm"
            )

        states = list(key_states.get("states") or [])
        normalized = list(key_states.get("distances") or [])
        for i, chip in enumerate(self.key_state_labels):
            active = bool(states[i]) if i < len(states) else False
            chip.set_text_and_level("ACTIVE" if active else "IDLE", "ok" if active else "neutral")
            self.key_state_bars[i].setValue(
                int(normalized[i]) if i < len(normalized) else 0
            )

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
        else:
            self.task_times_label.setText(
                "Tasks: analog -- us | trigger -- us | socd -- us | kb -- us | nkro -- us | gp -- us | total -- us"
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
        else:
            self.analog_monitor_label.setText(
                "Analog: raw -- us | filter -- us | cal -- us | lut -- us | store -- us | min -- us | max -- us | avg -- us | nz -- | max key --"
            )

        self._tick_count += 1
        elapsed = time.monotonic() - self._tick_started
        if elapsed >= 1.0:
            self.gui_rate_chip.setText(f"{self._tick_count / elapsed:.1f} Hz")
            self._tick_started = time.monotonic()
            self._tick_count = 0

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._poll_sensor_once()

    # ------------------------------------------------------------------
    # Data loading / actions
    # ------------------------------------------------------------------

    def reload(self) -> None:
        self.load_filter_settings()
        self.refresh_config_display()
        try:
            mode = self.device.get_led_diagnostic()
        except Exception:
            mode = None
        if mode is not None:
            self.diag_combo.setCurrentIndex(max(0, self.diag_combo.findData(int(mode))))

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

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_page_activated(self) -> None:
        self._page_active = True
        if self.session.live_enabled:
            self._poll_sensor_once()

    def on_page_deactivated(self) -> None:
        self._page_active = False
