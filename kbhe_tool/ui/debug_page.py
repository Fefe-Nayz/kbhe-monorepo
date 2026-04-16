from __future__ import annotations

import time
from typing import Any, Optional

from PySide6.QtCore import QTimer, Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QProgressBar,
    QVBoxLayout,
    QWidget,
)


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


class DebugPage(QWidget):
    statusChanged = Signal(str)

    def __init__(self, device: Any | None = None, parent: QWidget | None = None):
        super().__init__(parent)
        self.device = device
        self._page_active = False
        self._last_sensor_error: Optional[str] = None
        self._gui_tick_started = time.monotonic()
        self._gui_tick_count = 0

        self.setObjectName("DebugPage")
        self._build_ui()
        self._apply_style()
        self._sync_enabled_state()
        self._init_timer()
        self.reload()

    def create_debug_widgets(self, parent: QWidget | None = None):
        del parent
        return self

    def set_device(self, device: Any | None):
        self.device = device
        self._sync_enabled_state()
        self.reload()

    def reload(self):
        self.load_filter_settings()
        self.refresh_config_display()
        self._refresh_live_state()

    def on_page_activated(self):
        self._page_active = True
        self.reload()
        if self.live_update_check.isChecked() and self.device is not None:
            self.sensor_timer.start(self.refresh_rate_spin.value())
            self._poll_sensor_once()
        self._refresh_live_state()

    def on_page_deactivated(self):
        self._page_active = False
        self.sensor_timer.stop()
        self._refresh_live_state()

    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(16, 16, 16, 16)
        root.setSpacing(12)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        root.addWidget(scroll)

        content = QWidget()
        scroll.setWidget(content)
        layout = QVBoxLayout(content)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)

        header = self._card("Diagnostics", "Live sensor monitoring and filter tuning.")
        hl = header.layout()
        self.connection_label = QLabel("No device connected")
        self.status_label = QLabel("Ready.")
        self.live_state_label = QLabel("Live polling is disabled.")
        self.connection_label.setProperty("muted", True)
        self.live_state_label.setProperty("muted", True)
        for label in (self.connection_label, self.status_label, self.live_state_label):
            label.setWordWrap(True)
            hl.addWidget(label)
        actions = QHBoxLayout()
        self.reload_button = QPushButton("Reload")
        self.reload_button.clicked.connect(self.reload)
        self.refresh_config_button = QPushButton("Refresh Config")
        self.refresh_config_button.clicked.connect(self.refresh_config_display)
        actions.addWidget(self.reload_button)
        actions.addWidget(self.refresh_config_button)
        actions.addStretch(1)
        hl.addLayout(actions)
        layout.addWidget(header)

        monitor = self._card("Live Sensor Monitor", "The timer starts only when the page is active.")
        ml = monitor.layout()
        top = QHBoxLayout()
        self.live_update_check = QCheckBox("Enable live updates")
        self.live_update_check.toggled.connect(self.toggle_live_update)
        self.refresh_rate_spin = QSpinBox()
        self.refresh_rate_spin.setRange(50, 1000)
        self.refresh_rate_spin.setValue(100)
        self.refresh_rate_spin.valueChanged.connect(self._on_refresh_rate_changed)
        top.addWidget(self.live_update_check)
        top.addWidget(QLabel("Refresh interval (ms):"))
        top.addWidget(self.refresh_rate_spin)
        top.addStretch(1)
        ml.addLayout(top)

        metrics = QHBoxLayout()
        self.hid_rate_value = self._metric_card("HID Poll Rate")
        self.gui_rate_value = self._metric_card("GUI Update Rate")
        metrics.addWidget(self.hid_rate_value["frame"])
        metrics.addWidget(self.gui_rate_value["frame"])
        ml.addLayout(metrics)
        self.hid_rate_value["value"].setText("-- Hz")
        self.gui_rate_value["value"].setText("-- Hz")

        self.monitor_state_label = QLabel("Stopped.")
        self.monitor_state_label.setProperty("muted", True)
        ml.addWidget(self.monitor_state_label)

        self.task_times_label = QLabel(
            "Tasks: analog -- us | trigger -- us | socd -- us | kb -- us | nkro -- us | gp -- us | total -- us"
        )
        self.task_times_label.setProperty("muted", True)
        self.task_times_label.setWordWrap(True)
        ml.addWidget(self.task_times_label)

        self.analog_monitor_label = QLabel(
            "Analog: raw -- us | filter -- us | cal -- us | lut -- us | store -- us | min -- us | max -- us | avg -- us | nz -- | max key --"
        )
        self.analog_monitor_label.setProperty("muted", True)
        self.analog_monitor_label.setWordWrap(True)
        ml.addWidget(self.analog_monitor_label)
        layout.addWidget(monitor)

        adc = self._card("ADC Sensor Values", "Raw ADC values and the derived distance estimates.")
        al = adc.layout()
        note = QLabel("LUT: ADC 2100-2672 -> Distance 0-4 mm | Typical raw range: 2000-2700")
        note.setProperty("muted", True)
        note.setWordWrap(True)
        al.addWidget(note)
        adc_grid = QGridLayout()
        adc_grid.addWidget(QLabel("Key"), 0, 0)
        adc_grid.addWidget(QLabel("ADC"), 0, 1)
        adc_grid.addWidget(QLabel("Value"), 0, 2)
        adc_grid.addWidget(QLabel("Distance"), 0, 3)
        self.adc_labels = []
        self.adc_bars = []
        self.distance_labels = []
        for i in range(6):
            row = i + 1
            adc_grid.addWidget(QLabel(f"Key {i + 1}"), row, 0)
            bar = QProgressBar()
            bar.setRange(0, 700)
            bar.setTextVisible(False)
            self.adc_bars.append(bar)
            adc_grid.addWidget(bar, row, 1)
            value = QLabel("----")
            self.adc_labels.append(value)
            adc_grid.addWidget(value, row, 2)
            dist = QLabel("-.-- mm")
            self.distance_labels.append(dist)
            adc_grid.addWidget(dist, row, 3)
        al.addLayout(adc_grid)
        layout.addWidget(adc)

        key = self._card("Key States", "Normalized key travel and key press state.")
        kl = key.layout()
        key_grid = QGridLayout()
        self.key_state_labels = []
        self.key_distance_bars = []
        for i in range(6):
            box = self._subcard()
            box_l = QVBoxLayout(box)
            box_l.setContentsMargins(10, 10, 10, 10)
            box_l.setSpacing(4)
            title = QLabel(f"Key {i + 1}")
            title.setStyleSheet("font-weight: 600;")
            state = QLabel("⬜ OFF")
            state.setProperty("muted", True)
            bar = QProgressBar()
            bar.setRange(0, 255)
            bar.setTextVisible(False)
            box_l.addWidget(title)
            box_l.addWidget(state)
            box_l.addWidget(bar)
            key_grid.addWidget(box, i // 3, i % 3)
            self.key_state_labels.append(state)
            self.key_distance_bars.append(bar)
        kl.addLayout(key_grid)
        layout.addWidget(key)

        lock = self._card("Lock Indicators", "Caps, Num, and Scroll lock state plus quick test buttons.")
        ll = lock.layout()
        lock_grid = QGridLayout()
        self.caps_lock_indicator = self._chip("Caps Lock")
        self.num_lock_indicator = self._chip("Num Lock")
        self.scroll_lock_indicator = self._chip("Scroll Lock")
        self.pe0_led_indicator = self._chip("PE0 LED")
        lock_grid.addWidget(self.caps_lock_indicator["frame"], 0, 0)
        lock_grid.addWidget(self.num_lock_indicator["frame"], 0, 1)
        lock_grid.addWidget(self.scroll_lock_indicator["frame"], 1, 0)
        lock_grid.addWidget(self.pe0_led_indicator["frame"], 1, 1)
        ll.addLayout(lock_grid)
        buttons = QHBoxLayout()
        for text, code in [("Toggle Caps Lock", 0x39), ("Toggle Num Lock", 0x53), ("Toggle Scroll Lock", 0x47)]:
            btn = QPushButton(text)
            btn.clicked.connect(lambda _checked=False, k=code: self.send_keypress(k))
            buttons.addWidget(btn)
        buttons.addStretch(1)
        ll.addLayout(buttons)
        layout.addWidget(lock)

        config = self._card("Current Configuration", "Firmware, HID options, LED state, and live-not-saved reminders.")
        cl = config.layout()
        self.config_text = QPlainTextEdit()
        self.config_text.setReadOnly(True)
        self.config_text.setMinimumHeight(180)
        cl.addWidget(self.config_text)
        layout.addWidget(config)

        filter_box = self._card("ADC EMA Filter Settings", "Tune the filter, then apply live to the device.")
        fl = filter_box.layout()
        self.filter_enabled_check = QCheckBox("Enable ADC EMA Filter")
        self.filter_enabled_check.toggled.connect(self.on_filter_enabled_change)
        fl.addWidget(self.filter_enabled_check)
        self.filter_noise_band_spin = self._spin(1, 100, 30)
        self.filter_alpha_min_spin = self._spin(2, 128, 32)
        self.filter_alpha_max_spin = self._spin(1, 32, 4)
        form = QGridLayout()
        for row, (label, spin, hint) in enumerate([
            ("Noise Band (ADC counts)", self.filter_noise_band_spin, "(default: 30)"),
            ("Alpha Min (1/N, slow)", self.filter_alpha_min_spin, "(default: 32 -> 1/32)"),
            ("Alpha Max (1/N, fast)", self.filter_alpha_max_spin, "(default: 4 -> 1/4)"),
        ]):
            form.addWidget(QLabel(label), row, 0)
            form.addWidget(spin, row, 1)
            hint_label = QLabel(hint)
            hint_label.setProperty("muted", True)
            form.addWidget(hint_label, row, 2)
        fl.addLayout(form)
        filter_actions = QHBoxLayout()
        self.apply_filter_button = QPushButton("Apply Filter")
        self.apply_filter_button.clicked.connect(self.apply_filter_settings)
        self.reload_filter_button = QPushButton("Reload From Device")
        self.reload_filter_button.clicked.connect(self.load_filter_settings)
        self.reset_filter_button = QPushButton("Reset Defaults")
        self.reset_filter_button.clicked.connect(self.reset_filter_defaults)
        for btn in (self.apply_filter_button, self.reload_filter_button, self.reset_filter_button):
            filter_actions.addWidget(btn)
        filter_actions.addStretch(1)
        fl.addLayout(filter_actions)
        layout.addWidget(filter_box)

        layout.addStretch(1)

    def _apply_style(self):
        self.setStyleSheet(
            """
            QWidget#DebugPage { background: #f4f7fb; }
            QFrame[card="true"] { background: white; border: 1px solid #d8e0ea; border-radius: 16px; }
            QFrame[subcard="true"] { background: #fbfcfe; border: 1px solid #e3e9f2; border-radius: 12px; }
            QLabel[muted="true"] { color: #667085; }
            QProgressBar { border: 1px solid #cfd7e2; border-radius: 6px; background: #eef2f7; height: 10px; }
            QProgressBar::chunk { border-radius: 6px; background: #2563eb; }
            QPlainTextEdit { background: #0f172a; color: #e5eefb; border: 1px solid #d8e0ea; border-radius: 12px; font-family: Consolas, monospace; font-size: 10pt; }
            QPushButton { padding: 6px 12px; }
            """
        )

    def _card(self, title: str, subtitle: str | None = None) -> QFrame:
        card = QFrame()
        card.setProperty("card", True)
        layout = QVBoxLayout(card)
        layout.setContentsMargins(16, 16, 16, 16)
        layout.setSpacing(10)
        title_label = QLabel(title)
        title_label.setStyleSheet("font-size: 15px; font-weight: 700; color: #101828;")
        layout.addWidget(title_label)
        if subtitle:
            sub = QLabel(subtitle)
            sub.setProperty("muted", True)
            sub.setWordWrap(True)
            layout.addWidget(sub)
        return card

    def _subcard(self) -> QFrame:
        card = QFrame()
        card.setProperty("subcard", True)
        return card

    def _metric_card(self, title: str):
        frame = self._subcard()
        frame.setMinimumWidth(220)
        layout = QVBoxLayout(frame)
        layout.setContentsMargins(12, 10, 12, 10)
        title_label = QLabel(title)
        title_label.setStyleSheet("font-weight: 600; color: #344054;")
        value_label = QLabel("--")
        value_label.setStyleSheet("font-size: 18px; font-weight: 700; color: #101828;")
        layout.addWidget(title_label)
        layout.addWidget(value_label)
        return {"frame": frame, "value": value_label}

    def _chip(self, title: str):
        frame = self._subcard()
        layout = QHBoxLayout(frame)
        layout.setContentsMargins(10, 10, 10, 10)
        label = QLabel(title)
        value = QLabel("⬜ OFF")
        value.setProperty("muted", True)
        layout.addWidget(label)
        layout.addStretch(1)
        layout.addWidget(value)
        return {"frame": frame, "value": value}

    def _spin(self, minimum: int, maximum: int, value: int) -> QSpinBox:
        spin = QSpinBox()
        spin.setRange(minimum, maximum)
        spin.setValue(value)
        spin.setFixedWidth(90)
        return spin

    def _set_status(self, message: str, kind: str = "info"):
        colors = {"info": "#175cd3", "ok": "#067647", "warn": "#b54708", "error": "#b42318"}
        self.status_label.setText(message)
        self.status_label.setStyleSheet(f"color: {colors.get(kind, '#175cd3')}; font-weight: 600;")
        self.statusChanged.emit(message)

    def _refresh_connection_state(self):
        if self.device is None:
            self.connection_label.setText("No device connected")
            self.connection_label.setStyleSheet("color: #b42318; font-weight: 600;")
        else:
            self.connection_label.setText("Device connected")
            self.connection_label.setStyleSheet("color: #067647; font-weight: 600;")

    def _sync_enabled_state(self):
        has_device = self.device is not None
        for widget in [
            self.live_update_check,
            self.refresh_rate_spin,
            self.reload_button,
            self.refresh_config_button,
            self.filter_enabled_check,
            self.filter_noise_band_spin,
            self.filter_alpha_min_spin,
            self.filter_alpha_max_spin,
            self.apply_filter_button,
            self.reload_filter_button,
            self.reset_filter_button,
            self.config_text,
        ]:
            widget.setEnabled(has_device)
        self._refresh_connection_state()

    def _refresh_live_state(self):
        if self.live_update_check.isChecked() and self._page_active and self.device is not None:
            self.live_state_label.setText(f"Live polling is running every {self.refresh_rate_spin.value()} ms.")
            self.monitor_state_label.setText("Running.")
        elif self.live_update_check.isChecked():
            self.live_state_label.setText("Live polling is armed and will start when this page becomes active.")
            self.monitor_state_label.setText("Armed.")
        else:
            self.live_state_label.setText("Live polling is disabled.")
            self.monitor_state_label.setText("Stopped.")

    def _init_timer(self):
        self.sensor_timer = QTimer(self)
        self.sensor_timer.timeout.connect(self._poll_sensor_once)

    def _on_refresh_rate_changed(self, _value: int):
        if self._page_active and self.live_update_check.isChecked() and self.device is not None:
            self.sensor_timer.start(self.refresh_rate_spin.value())
            self._poll_sensor_once()
        self._refresh_live_state()

    def toggle_live_update(self, _checked: bool):
        if self.live_update_check.isChecked() and self._page_active and self.device is not None:
            self.sensor_timer.start(self.refresh_rate_spin.value())
            self._poll_sensor_once()
        else:
            self.sensor_timer.stop()
        self._refresh_live_state()

    def _device_call(self, name: str, default: Any = None, *args, **kwargs):
        if self.device is None:
            return default
        method = getattr(self.device, name, None)
        if method is None:
            return default
        try:
            return method(*args, **kwargs)
        except Exception as exc:
            self._set_status(f"{name} failed: {exc}", "error")
            return default

    def _poll_sensor_once(self):
        if self.device is None:
            self._set_status("Connect a device to read diagnostics.", "warn")
            return
        try:
            adc_data = self.device.get_adc_values() or {}
            key_states = self.device.get_key_states() or {}
            lock_states = self.device.get_lock_states() or {}
        except Exception as exc:
            error = f"Sensor monitor error: {exc}"
            if error != self._last_sensor_error:
                self._last_sensor_error = error
                self._set_status(error, "error")
            return

        self._last_sensor_error = None
        self._update_adc_display(adc_data)
        self._update_key_display(key_states)
        self._update_lock_display(lock_states)
        self._update_gui_rate()

    def _update_adc_display(self, adc_data: dict):
        adc_raw = list(adc_data.get("adc_raw") or adc_data.get("adc") or [])
        adc_filtered = list(adc_data.get("adc_filtered") or adc_raw)
        for i in range(6):
            raw_value = adc_raw[i] if i < len(adc_raw) else None
            filtered_value = adc_filtered[i] if i < len(adc_filtered) else None
            if raw_value is None:
                self.adc_bars[i].setValue(0)
                self.adc_labels[i].setText("----")
            else:
                bar_value = int(filtered_value if filtered_value is not None else raw_value)
                self.adc_bars[i].setValue(_clamp(bar_value - 2000, 0, 700))
                if filtered_value is None:
                    self.adc_labels[i].setText(f"R:{int(raw_value):4d}")
                else:
                    self.adc_labels[i].setText(
                        f"R:{int(raw_value):4d} F:{int(filtered_value):4d}"
                    )
        scan_rate = adc_data.get("scan_rate_hz")
        scan_time = adc_data.get("scan_time_us")
        if scan_rate is not None and scan_time is not None:
            self.hid_rate_value["value"].setText(f"{scan_rate} Hz / {scan_time} us")
        elif scan_rate is not None:
            self.hid_rate_value["value"].setText(f"{scan_rate} Hz")

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

    def _update_key_display(self, key_states: dict):
        states = list(key_states.get("states") or [])
        distances = list(key_states.get("distances") or [])
        distances_mm = list(key_states.get("distances_mm") or [])
        for i in range(6):
            active = bool(states[i]) if i < len(states) else False
            self.key_state_labels[i].setText("🟢 ON" if active else "⬜ OFF")
            self.key_state_labels[i].setStyleSheet("color: #067647; font-weight: 600;" if active else "color: #667085;")
            self.key_distance_bars[i].setValue(int(distances[i]) if i < len(distances) else 0)
            self.distance_labels[i].setText(f"{distances_mm[i]:.2f} mm" if i < len(distances_mm) else "-.-- mm")

    def _update_lock_display(self, lock_states: dict):
        caps = bool(lock_states.get("caps_lock", False))
        num = bool(lock_states.get("num_lock", False))
        scroll = bool(lock_states.get("scroll_lock", False))
        self._set_chip(self.caps_lock_indicator, caps)
        self._set_chip(self.num_lock_indicator, num)
        self._set_chip(self.scroll_lock_indicator, scroll)
        if caps and num:
            self._set_chip(self.pe0_led_indicator, True, "🟢 SOLID", "#067647")
        elif caps and not num:
            self._set_chip(self.pe0_led_indicator, True, "🟡 FAST BLINK", "#b54708")
        elif num and not caps:
            self._set_chip(self.pe0_led_indicator, True, "🟡 SLOW BLINK", "#b54708")
        else:
            self._set_chip(self.pe0_led_indicator, False)

    def _set_chip(self, chip: dict, enabled: bool, text: str | None = None, color: str | None = None):
        chip["value"].setText(text if text is not None else ("🟢 ON" if enabled else "⬜ OFF"))
        chip["value"].setStyleSheet(f"color: {color or ('#067647' if enabled else '#667085')}; font-weight: 600;")

    def _update_gui_rate(self):
        self._gui_tick_count += 1
        elapsed = time.monotonic() - self._gui_tick_started
        if elapsed >= 1.0:
            self.gui_rate_value["value"].setText(f"{self._gui_tick_count / elapsed:.1f} Hz")
            self._gui_tick_count = 0
            self._gui_tick_started = time.monotonic()

    def send_keypress(self, keycode: int):
        if self.device is None:
            self._set_status("Connect a device before sending a keypress.", "warn")
            return
        self._set_status(f"Press the physical key with HID code 0x{keycode:02X} to toggle the lock state.", "info")

    def load_filter_settings(self):
        if self.device is None:
            self._set_status("Connect a device to load filter settings.", "warn")
            return
        enabled = self._device_call("get_filter_enabled", None)
        params = self._device_call("get_filter_params", {}) or {}
        if enabled is not None:
            self.filter_enabled_check.setChecked(bool(enabled))
        if "noise_band" in params:
            self.filter_noise_band_spin.setValue(int(params["noise_band"]))
        if "alpha_min_denom" in params:
            self.filter_alpha_min_spin.setValue(int(params["alpha_min_denom"]))
        if "alpha_max_denom" in params:
            self.filter_alpha_max_spin.setValue(int(params["alpha_max_denom"]))
        self._set_status("Filter settings loaded.", "ok")

    def on_filter_enabled_change(self, enabled: bool):
        if self.device is None:
            self._set_status("Connect a device before changing the filter.", "warn")
            return
        if self._device_call("set_filter_enabled", False, bool(enabled)):
            self._set_status(f"Filter {'enabled' if enabled else 'disabled'} live.", "ok")
        else:
            self._set_status("Failed to update filter state.", "error")

    def apply_filter_settings(self):
        if self.device is None:
            self._set_status("Connect a device before applying filter settings.", "warn")
            return
        noise_band = _clamp(self.filter_noise_band_spin.value(), 1, 100)
        alpha_min = _clamp(self.filter_alpha_min_spin.value(), 2, 128)
        alpha_max = _clamp(self.filter_alpha_max_spin.value(), 1, 32)
        self.filter_noise_band_spin.setValue(noise_band)
        self.filter_alpha_min_spin.setValue(alpha_min)
        self.filter_alpha_max_spin.setValue(alpha_max)
        if self._device_call("set_filter_params", False, noise_band, alpha_min, alpha_max):
            self._set_status(f"Filter params applied: band={noise_band}, alpha_min=1/{alpha_min}, alpha_max=1/{alpha_max}", "ok")
        else:
            self._set_status("Error applying filter parameters.", "error")

    def reset_filter_defaults(self):
        self.filter_enabled_check.setChecked(True)
        self.filter_noise_band_spin.setValue(30)
        self.filter_alpha_min_spin.setValue(32)
        self.filter_alpha_max_spin.setValue(4)
        if self.device is None:
            self._set_status("Connect a device before resetting filter defaults.", "warn")
            return
        enabled_ok = self._device_call("set_filter_enabled", False, True)
        params_ok = self._device_call("set_filter_params", False, 30, 32, 4)
        if enabled_ok and params_ok:
            self._set_status("Filter reset to defaults.", "ok")
        else:
            self._set_status("Filter defaults were sent, but the device reported a failure.", "warn")

    def refresh_config_display(self):
        if self.device is None:
            self.config_text.setPlainText("No device connected.\n\nConnect a device to inspect firmware and live options.")
            self._set_status("Connect a device to refresh configuration.", "warn")
            return
        version = self._device_call("get_firmware_version", "Unknown")
        options = self._device_call("get_options", {}) or {}
        led_enabled = self._device_call("led_get_enabled", None)
        brightness = self._device_call("led_get_brightness", None)
        filter_enabled = self._device_call("get_filter_enabled", None)
        filter_params = self._device_call("get_filter_params", {}) or {}
        lines = [
            "KBHE Configuration",
            "",
            f"Firmware Version: {version if version else 'Unknown'}",
            "",
            "HID Interfaces:",
            f"  - Keyboard: {'Enabled' if options.get('keyboard_enabled') else 'Disabled'}",
            f"  - Gamepad:  {'Enabled' if options.get('gamepad_enabled') else 'Disabled'}",
            f"  - Raw HID:  {'Enabled' if options.get('raw_hid_echo', True) else 'Disabled'}",
            "",
            "LED Matrix:",
            f"  - Enabled:    {'Yes' if led_enabled else 'No'}",
            f"  - Brightness: {brightness if brightness is not None else 'Unknown'}",
            "",
            "ADC Filter:",
            f"  - Enabled:    {'Yes' if filter_enabled else 'No'}",
            f"  - Noise Band: {filter_params.get('noise_band', 'Unknown')}",
            f"  - Alpha Min:  {filter_params.get('alpha_min_denom', 'Unknown')}",
            f"  - Alpha Max:  {filter_params.get('alpha_max_denom', 'Unknown')}",
            "",
            "Note: Live changes are applied immediately but are not saved to flash until you explicitly save them.",
        ]
        self.config_text.setPlainText("\n".join(lines))
        self._set_status("Configuration refreshed.", "ok")


DebugPageMixin = DebugPage
