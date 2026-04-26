from __future__ import annotations

import time

from PySide6.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QVBoxLayout,
    QWidget,
)

from kbhe_tool.key_layout import key_display_name
from kbhe_tool.protocol import GAMEPAD_API_MODE_NAMES, LED_EFFECT_NAMES
from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
    make_secondary_button,
)


class OverviewPage(QWidget):
    def __init__(self, session, controller=None, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self.controller = controller
        self._page_active = False
        self._mcu_metrics_cache: dict = {}
        self._next_mcu_refresh_monotonic = 0.0
        self._build_ui()
        self.on_selected_key_changed(getattr(self.session, "selected_key", 0))
        try:
            self.session.selectedKeyChanged.connect(self.on_selected_key_changed)
        except Exception:
            pass
        try:
            self.session.snapshotChanged.connect(self.reload)
        except Exception:
            pass
        try:
            self.session.connectionChanged.connect(self.reload)
        except Exception:
            pass
        try:
            self.session.liveSettingsChanged.connect(lambda *_args: self.reload())
        except Exception:
            pass
        self.reload()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Dashboard",
            "Short device snapshot and direct jumps to the pages you actually use.",
        )
        root.addWidget(scaffold, 1)

        top_row = QHBoxLayout()
        top_row.setSpacing(14)
        top_row.addWidget(self._build_device_card(), 1)
        top_row.addWidget(self._build_modes_card(), 1)
        top_row.addWidget(self._build_mcu_card(), 1)
        scaffold.content_layout.addLayout(top_row)

        bottom_row = QHBoxLayout()
        bottom_row.setSpacing(14)
        bottom_row.addWidget(self._build_focus_card(), 1)
        bottom_row.addWidget(self._build_actions_card(), 1)
        scaffold.content_layout.addLayout(bottom_row)

        scaffold.add_stretch()

    def _build_device_card(self) -> SectionCard:
        card = SectionCard(
            "Device State",
            "Connection, firmware, and live polling without the filler text.",
        )

        header = QHBoxLayout()
        header.setSpacing(8)
        self.connection_chip = StatusChip("Disconnected", "bad")
        self.live_chip = StatusChip("Live OFF", "neutral")
        header.addWidget(self.connection_chip)
        header.addWidget(self.live_chip)
        header.addStretch(1)
        card.body_layout.addLayout(header)

        self.firmware_value = QLabel("Firmware: --")
        self.firmware_value.setObjectName("CardTitle")
        self.firmware_value.setStyleSheet("font-size: 18pt; font-weight: 700;")
        card.body_layout.addWidget(self.firmware_value)

        self.connection_detail = QLabel("--")
        self.connection_detail.setObjectName("Muted")
        self.connection_detail.setWordWrap(True)
        card.body_layout.addWidget(self.connection_detail)

        return card

    def _build_modes_card(self) -> SectionCard:
        card = SectionCard(
            "Current Modes",
            "The active input and lighting modes that matter right now.",
        )

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)
        card.body_layout.addLayout(grid)

        self.mode_values: dict[str, QLabel] = {}
        rows = [
            ("keyboard", "Keyboard"),
            ("gamepad", "Gamepad"),
            ("nkro", "NKRO"),
            ("led", "LED"),
            ("brightness", "Brightness"),
            ("gamepad_api", "Gamepad API"),
            ("led_effect", "LED Effect"),
        ]
        for row, (key, title) in enumerate(rows):
            label = QLabel(title)
            label.setObjectName("Muted")
            value = QLabel("--")
            value.setWordWrap(True)
            grid.addWidget(label, row, 0)
            grid.addWidget(value, row, 1)
            self.mode_values[key] = value

        return card

    def _build_focus_card(self) -> SectionCard:
        card = SectionCard(
            "Focused Key",
            "Keep the selected key in view and jump straight to the page that edits it.",
        )

        self.focus_key_value = QLabel(key_display_name(0))
        self.focus_key_value.setObjectName("CardTitle")
        self.focus_key_value.setStyleSheet("font-size: 28pt; font-weight: 700; padding: 6px 0;")
        card.body_layout.addWidget(self.focus_key_value)

        hint = QLabel(
            "The selected key is shared with Keyboard, Calibration, and Gamepad."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        card.body_layout.addWidget(hint)

        for title, page_id in [
            ("Open Keyboard", "keyboard"),
            ("Open Calibration", "calibration"),
            ("Open Gamepad", "gamepad"),
        ]:
            card.body_layout.addWidget(
                make_secondary_button(title, lambda _=False, pid=page_id: self._open_page(pid))
            )

        return card

    def _build_mcu_card(self) -> SectionCard:
        card = SectionCard(
            "MCU Metrics",
            "Internal STM32 telemetry: temperature, clock, scan cadence, and estimated workload.",
        )

        self.mcu_temp_value = QLabel("--")
        self.mcu_temp_value.setObjectName("CardTitle")
        self.mcu_temp_value.setStyleSheet("font-size: 22pt; font-weight: 700;")
        card.body_layout.addWidget(self.mcu_temp_value)

        self.mcu_temp_hint = QLabel("Internal MCU temperature")
        self.mcu_temp_hint.setObjectName("Muted")
        card.body_layout.addWidget(self.mcu_temp_hint)

        self.mcu_clock_value = QLabel("--")
        self.mcu_clock_value.setObjectName("CardTitle")
        self.mcu_clock_value.setStyleSheet("font-size: 18pt; font-weight: 700; margin-top: 8px;")
        card.body_layout.addWidget(self.mcu_clock_value)

        self.mcu_clock_hint = QLabel("Core clock")
        self.mcu_clock_hint.setObjectName("Muted")
        card.body_layout.addWidget(self.mcu_clock_hint)

        self.mcu_load_value = QLabel("--")
        self.mcu_load_value.setObjectName("CardTitle")
        self.mcu_load_value.setStyleSheet("font-size: 18pt; font-weight: 700; margin-top: 8px;")
        card.body_layout.addWidget(self.mcu_load_value)

        self.mcu_load_hint = QLabel("Scan workload")
        self.mcu_load_hint.setObjectName("Muted")
        self.mcu_load_hint.setWordWrap(True)
        card.body_layout.addWidget(self.mcu_load_hint)

        return card

    def _build_actions_card(self) -> SectionCard:
        card = SectionCard(
            "Quick Access",
            "Shortcuts to the pages that usually follow from the current state.",
        )

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(10)
        card.body_layout.addLayout(grid)

        actions = [
            ("Keyboard", "keyboard"),
            ("Gamepad", "gamepad"),
            ("Lighting", "lighting"),
            ("Effects", "effects"),
            ("Rotary", "rotary"),
            ("Firmware", "firmware"),
            ("Device", "device"),
            ("Debug", "debug"),
        ]
        for index, (title, page_id) in enumerate(actions):
            button = make_secondary_button(
                title, lambda _=False, pid=page_id: self._open_page(pid)
            )
            button.setMinimumHeight(46)
            grid.addWidget(button, index // 2, index % 2)

        return card

    def _open_page(self, page_id: str) -> None:
        if self.controller is not None and hasattr(self.controller, "show_page"):
            self.controller.show_page(page_id)

    def _safe_gamepad_settings(self) -> dict:
        try:
            return self.device.get_gamepad_settings() or {}
        except Exception:
            return {}

    def _safe_led_effect(self):
        try:
            return self.device.get_led_effect()
        except Exception:
            return None

    def _safe_mcu_metrics(self) -> dict:
        try:
            return getattr(self.device, "get_mcu_metrics", lambda: {})() or {}
        except Exception:
            return {}

    def _maybe_refresh_mcu_metrics(self, force: bool = False) -> None:
        if not self._page_active or not bool(getattr(self.session, "connected", False)):
            return
        now = time.monotonic()
        if not force and now < self._next_mcu_refresh_monotonic:
            return
        self._next_mcu_refresh_monotonic = now + 1.0
        self._mcu_metrics_cache = self._safe_mcu_metrics()

    def reload(self, *_args) -> None:
        snapshot = self.session.snapshot or {}
        options = snapshot.get("options") or {}
        connected = bool(getattr(self.session, "connected", True))
        firmware = snapshot.get("firmware_version")
        brightness = snapshot.get("brightness")
        led_enabled = snapshot.get("led_enabled")
        nkro_enabled = snapshot.get("nkro_enabled")
        live_enabled = bool(getattr(self.session, "live_enabled", False))
        live_interval_ms = int(getattr(self.session, "live_interval_ms", 100))

        gamepad_settings = self._safe_gamepad_settings() if connected else {}
        gamepad_api = gamepad_settings.get("api_mode")
        led_effect = self._safe_led_effect() if connected else None
        self._maybe_refresh_mcu_metrics()
        mcu_metrics = self._mcu_metrics_cache if connected else {}

        self.connection_chip.set_text_and_level(
            "Connected" if connected else "Disconnected",
            "ok" if connected else "bad",
        )
        self.live_chip.set_text_and_level(
            f"Live {live_interval_ms} ms" if live_enabled else "Live OFF",
            "info" if live_enabled else "neutral",
        )
        self.firmware_value.setText(f"Firmware: {firmware if firmware is not None else '--'}")
        self.connection_detail.setText(
            "Raw HID session is up and the app can query settings live."
            if connected
            else "Reconnect the keyboard before editing or polling live data."
        )

        self.mode_values["keyboard"].setText("On" if options.get("keyboard_enabled") else "Off")
        self.mode_values["gamepad"].setText("On" if options.get("gamepad_enabled") else "Off")
        self.mode_values["nkro"].setText("On" if nkro_enabled else "Off")
        self.mode_values["led"].setText("On" if led_enabled else "Off")
        self.mode_values["brightness"].setText(str(brightness) if brightness is not None else "--")
        self.mode_values["gamepad_api"].setText(
            GAMEPAD_API_MODE_NAMES.get(int(gamepad_api), "--")
            if gamepad_api is not None
            else "--"
        )
        self.mode_values["led_effect"].setText(
            LED_EFFECT_NAMES.get(led_effect, str(led_effect))
            if led_effect is not None
            else "--"
        )

        temperature_c = mcu_metrics.get("temperature_c")
        if isinstance(temperature_c, (int, float)):
            self.mcu_temp_value.setText(f"{int(temperature_c)} °C")
            vref_mv = mcu_metrics.get("vref_mv")
            if isinstance(vref_mv, int) and vref_mv > 0:
                self.mcu_temp_hint.setText(f"Internal MCU temperature · Vref {vref_mv} mV")
            else:
                self.mcu_temp_hint.setText("Internal MCU temperature")
        else:
            self.mcu_temp_value.setText("Unavailable")
            self.mcu_temp_hint.setText(
                "Internal temperature sensor not available yet or current sample failed."
            )

        core_clock_hz = mcu_metrics.get("core_clock_hz")
        if isinstance(core_clock_hz, int) and core_clock_hz > 0:
            self.mcu_clock_value.setText(f"{core_clock_hz / 1_000_000.0:.0f} MHz")
            self.mcu_clock_hint.setText("SystemCoreClock")
        else:
            self.mcu_clock_value.setText("--")
            self.mcu_clock_hint.setText("Core clock")

        load_percent = mcu_metrics.get("load_percent")
        scan_rate_hz = mcu_metrics.get("scan_rate_hz")
        work_us = mcu_metrics.get("work_us")
        if isinstance(load_percent, (int, float)):
            self.mcu_load_value.setText(f"{float(load_percent):.1f}%")
            hint_bits = []
            if isinstance(scan_rate_hz, int) and scan_rate_hz > 0:
                hint_bits.append(f"{scan_rate_hz} Hz")
            if isinstance(work_us, int) and work_us > 0:
                hint_bits.append(f"{work_us} us work")
            self.mcu_load_hint.setText(" · ".join(hint_bits) if hint_bits else "Scan workload")
        else:
            self.mcu_load_value.setText("--")
            self.mcu_load_hint.setText("Scan workload")

    def on_selected_key_changed(self, key_index: int) -> None:
        self.focus_key_value.setText(key_display_name(int(key_index)))

    def on_page_activated(self) -> None:
        self._page_active = True
        self._maybe_refresh_mcu_metrics(force=True)
        try:
            self.session.refresh_snapshot()
        except Exception:
            pass
        self.reload()

    def on_page_deactivated(self) -> None:
        self._page_active = False

    def on_live_tick(self) -> None:
        if not self._page_active:
            return
        self._maybe_refresh_mcu_metrics()
