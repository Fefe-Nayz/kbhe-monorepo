from __future__ import annotations

import time

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox,
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
from ...protocol import (
    FILTER_DEFAULT_ALPHA_MAX_DENOM,
    FILTER_DEFAULT_ALPHA_MIN_DENOM,
    FILTER_DEFAULT_ENABLED,
    FILTER_DEFAULT_NOISE_BAND,
    KEY_COUNT,
)
from ..theme import current_colors
from ..widgets import (
    KeyboardLayoutWidget,
    PageScaffold,
    SectionCard,
    StatusChip,
    SubCard,
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


class SignalProbeCard(SubCard):
    def __init__(self, key_index: int, parent=None):
        super().__init__(parent)
        self.key_index = int(key_index)

        self.title = QLabel(key_display_name(self.key_index))
        self.title.setObjectName("CardTitle")
        self.layout.addWidget(self.title)

        chips = QHBoxLayout()
        chips.setSpacing(6)
        self.state_chip = StatusChip("IDLE", "neutral")
        self.raw_chip = StatusChip("RAW --", "info")
        self.filtered_chip = StatusChip("EMA --", "info")
        self.calibrated_chip = StatusChip("CAL --", "info")
        self.distance_chip = StatusChip("--.-- mm", "neutral")
        for widget in (
            self.state_chip,
            self.raw_chip,
            self.filtered_chip,
            self.calibrated_chip,
            self.distance_chip,
        ):
            chips.addWidget(widget)
        chips.addStretch(1)
        self.layout.addLayout(chips)

        self.norm_bar = QProgressBar()
        self.norm_bar.setRange(0, 255)
        self.norm_bar.setFormat("Normalized %p%")
        self.layout.addWidget(self.norm_bar)

        self.raw_bar = QProgressBar()
        self.raw_bar.setRange(0, 4095)
        self.raw_bar.setFormat("Raw ADC %v")
        self.layout.addWidget(self.raw_bar)

        self.meta_label = QLabel("--")
        self.meta_label.setObjectName("Muted")
        self.meta_label.setWordWrap(True)
        self.layout.addWidget(self.meta_label)

    def set_key_index(self, key_index: int) -> None:
        self.key_index = int(key_index)
        self.title.setText(key_display_name(self.key_index))

    def apply_values(
        self,
        *,
        raw_value: int,
        filtered_value: int,
        calibrated_value: int,
        distance_mm: float,
        normalized: int,
        pressed: bool,
    ) -> None:
        self.state_chip.set_text_and_level("PRESSED" if pressed else "IDLE", "ok" if pressed else "neutral")
        self.raw_chip.set_text_and_level(f"RAW {int(raw_value)}", "info")
        self.filtered_chip.set_text_and_level(f"EMA {int(filtered_value)}", "info")
        self.calibrated_chip.set_text_and_level(f"CAL {int(calibrated_value)}", "info")
        self.distance_chip.set_text_and_level(f"{float(distance_mm):.2f} mm", "info")
        self.norm_bar.setValue(_clamp(int(normalized), 0, 255))
        self.raw_bar.setValue(_clamp(int(raw_value), 0, 4095))
        self.meta_label.setText(
            f"{key_short_label(self.key_index)}  ·  filtered delta {int(filtered_value) - int(raw_value):+d}  ·  "
            f"cal delta {int(calibrated_value) - int(filtered_value):+d}"
        )


class DebugPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        self._last_error = None
        self._tick_started = time.monotonic()
        self._tick_count = 0
        self._raw_values = [0] * KEY_COUNT
        self._filtered_values = [0] * KEY_COUNT
        self._calibrated_values = [0] * KEY_COUNT
        self._states = [0] * KEY_COUNT
        self._distances_norm = [0] * KEY_COUNT
        self._distances_mm = [0.0] * KEY_COUNT
        self._scan_payload = {}
        self._selected_keys: list[int] = [max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))]
        self._signal_probe_cards: dict[int, SignalProbeCard] = {}

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
            "Vue maintenance centrée sur les 82 capteurs: état live, ADC bruts/EMA, timings firmware et sélection multiple.",
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
        left.addWidget(self._build_selected_cards_card())
        left.addWidget(self._build_lock_card())
        left.addStretch(1)

        right.addWidget(self._build_config_card())
        right.addWidget(self._build_filter_card())
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
            "Tasks: analog -- us | trigger -- us | socd -- us | kb -- us | nkro -- us | gp -- us | led -- us | total -- us"
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
        self.focus_filtered_chip = StatusChip("EMA --", "info")
        chips.addWidget(self.focus_filtered_chip)
        self.focus_calibrated_chip = StatusChip("CAL --", "info")
        chips.addWidget(self.focus_calibrated_chip)
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
            "Heatmap des 82 touches. Clique des touches pour garder plusieurs capteurs sélectionnés tout en conservant une touche focus.",
        )
        header = QHBoxLayout()
        header.setSpacing(8)
        focus_lbl = QLabel("Focused")
        focus_lbl.setObjectName("Muted")
        header.addWidget(focus_lbl)
        self.selected_focus_chip = StatusChip(key_display_name(self.session.selected_key), "info")
        header.addWidget(self.selected_focus_chip)
        select_lbl = QLabel("Selected")
        select_lbl.setObjectName("Muted")
        header.addWidget(select_lbl)
        self.selected_count_chip = StatusChip("1 key", "neutral")
        header.addWidget(self.selected_count_chip)
        header.addStretch(1)
        header.addWidget(make_secondary_button("Add Focused", self.add_focused_key))
        header.addWidget(make_secondary_button("Select All", self.select_all_keys))
        header.addWidget(make_secondary_button("Clear Selection", self.clear_selected_keys))
        card.body_layout.addLayout(header)

        self.selection_label = QLabel()
        self.selection_label.setObjectName("Muted")
        self.selection_label.setWordWrap(True)
        card.body_layout.addWidget(self.selection_label)

        self.layout_view = KeyboardLayoutWidget(self.session, unit=38)
        self.layout_view.keyClicked.connect(self._toggle_selected_key)
        card.body_layout.addWidget(self.layout_view)
        self.selection_summary_label = QLabel("Selected avg raw -- | avg EMA -- | avg travel --")
        self.selection_summary_label.setObjectName("Muted")
        self.selection_summary_label.setWordWrap(True)
        card.body_layout.addWidget(self.selection_summary_label)
        return card

    def _build_lock_card(self) -> SectionCard:
        card = SectionCard("Lock Indicators")
        self.lock_rows: dict[str, StatusChip] = {}
        for name in ("Caps Lock", "Num Lock", "Scroll Lock", "Caps LED Override"):
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

    def _build_selected_cards_card(self) -> SectionCard:
        card = SectionCard(
            "Selected Signal Cards",
            "Une carte de signal est affichée pour chaque touche sélectionnée dans le layout.",
        )
        self.selected_cards_grid = QGridLayout()
        self.selected_cards_grid.setHorizontalSpacing(10)
        self.selected_cards_grid.setVerticalSpacing(10)
        card.body_layout.addLayout(self.selected_cards_grid)
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
        card = SectionCard(
            "ADC EMA Filter",
            "Désactivé = raw passthrough. Les alphas sont exprimés en 1/N: plus N est grand, plus le filtre est lent.",
        )
        self.filter_enabled_check = QCheckBox("Enable filter")
        self.filter_enabled_check.setChecked(bool(FILTER_DEFAULT_ENABLED))
        self.filter_enabled_check.toggled.connect(self.on_filter_enabled_change)
        card.body_layout.addWidget(self.filter_enabled_check)

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)

        self.noise_spin = QSpinBox()
        self.noise_spin.setRange(1, 255)
        self.noise_spin.setValue(FILTER_DEFAULT_NOISE_BAND)
        self.alpha_min_spin = QSpinBox()
        self.alpha_min_spin.setRange(1, 255)
        self.alpha_min_spin.setValue(FILTER_DEFAULT_ALPHA_MIN_DENOM)
        self.alpha_max_spin = QSpinBox()
        self.alpha_max_spin.setRange(1, 255)
        self.alpha_max_spin.setValue(FILTER_DEFAULT_ALPHA_MAX_DENOM)

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
        self.selected_focus_chip.set_text_and_level(key_display_name(key_index), "info")
        if key_index not in self._selected_keys:
            self._selected_keys.append(key_index)
        self._refresh_selected_keys_summary()
        self.focus_name.setText(key_display_name(key_index))
        self._refresh_focus_card()

    def _refresh_focus_card(self) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))
        pressed = bool(self._states[key_index])
        raw_value = int(self._raw_values[key_index])
        filtered_value = int(self._filtered_values[key_index])
        calibrated_value = int(self._calibrated_values[key_index])
        norm = int(self._distances_norm[key_index])
        distance_mm = float(self._distances_mm[key_index])
        self.focus_state_chip.set_text_and_level("PRESSED" if pressed else "IDLE", "ok" if pressed else "neutral")
        self.focus_raw_chip.set_text_and_level(f"RAW {raw_value}", "info")
        self.focus_filtered_chip.set_text_and_level(f"EMA {filtered_value}", "info")
        self.focus_calibrated_chip.set_text_and_level(f"CAL {calibrated_value}", "info")
        self.focus_distance_chip.set_text_and_level(f"{distance_mm:.2f} mm", "info")
        self.focus_norm_bar.setValue(_clamp(norm, 0, 255))
        self.focus_raw_bar.setValue(_clamp(raw_value, 0, 4095))
        self.focus_hint.setText(
            f"{key_display_name(key_index)}  ·  state={'pressed' if pressed else 'idle'}  ·  "
            f"normalized={norm}/255  ·  calibrated={calibrated_value}"
        )

    def _palette_for_index(self, order: int) -> str:
        colors = current_colors()
        palette = [
            colors["graph_1"],
            colors["graph_2"],
            colors["graph_3"],
            colors["graph_4"],
            colors["graph_5"],
            colors["graph_6"],
            colors["accent"],
            colors["warning"],
            colors["success"],
        ]
        return palette[order % len(palette)]

    def _refresh_selected_keys_summary(self) -> None:
        if not self._selected_keys:
            self._selected_keys = [max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))]

        count = len(self._selected_keys)
        self.selected_count_chip.set_text_and_level(f"{count} key{'s' if count != 1 else ''}", "info")
        labels = [key_display_name(index) for index in self._selected_keys[:6]]
        suffix = "" if count <= 6 else f" … +{count - 6}"
        self.selection_label.setText("Selected sensors: " + ", ".join(labels) + suffix)

        selected_raw = [self._raw_values[index] for index in self._selected_keys]
        selected_filtered = [self._filtered_values[index] for index in self._selected_keys]
        selected_calibrated = [self._calibrated_values[index] for index in self._selected_keys]
        selected_travel = [self._distances_mm[index] for index in self._selected_keys]
        avg_raw = sum(selected_raw) / len(selected_raw) if selected_raw else 0.0
        avg_filtered = sum(selected_filtered) / len(selected_filtered) if selected_filtered else 0.0
        avg_calibrated = sum(selected_calibrated) / len(selected_calibrated) if selected_calibrated else 0.0
        avg_travel = sum(selected_travel) / len(selected_travel) if selected_travel else 0.0
        pressed_count = sum(1 for index in self._selected_keys if self._states[index])
        self.selection_summary_label.setText(
            f"Selected avg raw {avg_raw:.0f} | avg EMA {avg_filtered:.0f} | avg calibrated {avg_calibrated:.0f} | "
            f"avg travel {avg_travel:.2f} mm | pressed {pressed_count}/{len(self._selected_keys)}"
        )
        self._refresh_selected_signal_cards()

    def add_focused_key(self) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))
        if key_index not in self._selected_keys:
            self._selected_keys.append(key_index)
        self._refresh_selected_keys_summary()
        self._refresh_layout_view()

    def clear_selected_keys(self) -> None:
        self._selected_keys = [max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))]
        self._refresh_selected_keys_summary()
        self._refresh_layout_view()

    def select_all_keys(self) -> None:
        self._selected_keys = list(range(KEY_COUNT))
        self._refresh_selected_keys_summary()
        self._refresh_layout_view()

    def _toggle_selected_key(self, key_index: int) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(key_index)))
        if key_index in self._selected_keys:
            if len(self._selected_keys) > 1:
                self._selected_keys.remove(key_index)
        else:
            self._selected_keys.append(key_index)
        self._refresh_selected_keys_summary()
        self._refresh_layout_view()

    def _refresh_layout_view(self) -> None:
        colors = current_colors()
        pressed_count = 0
        max_raw = max(self._raw_values) if self._raw_values else 0
        max_raw_key = self._raw_values.index(max_raw) if self._raw_values else 0
        max_travel = max(self._distances_mm) if self._distances_mm else 0.0
        max_travel_key = self._distances_mm.index(max_travel) if self._distances_mm else 0
        focused_key = max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))
        selected_lookup = {idx: order for order, idx in enumerate(self._selected_keys)}

        for key_index in range(KEY_COUNT):
            raw_value = int(self._raw_values[key_index])
            filtered_value = int(self._filtered_values[key_index])
            norm = max(0.0, min(1.0, float(self._distances_norm[key_index]) / 255.0))
            pressed = bool(self._states[key_index])
            if pressed:
                pressed_count += 1
            fill = _mix_colors(colors["surface_muted"], colors["accent_soft"], norm)
            if pressed:
                fill = _mix_colors(fill, colors["success"], 0.38)
            border = colors["success"] if pressed else colors["border"]
            if key_index in selected_lookup:
                border = self._palette_for_index(selected_lookup[key_index])
            if key_index == focused_key:
                border = colors["accent"]
            self.layout_view.set_key_state(
                key_index,
                title=key_short_label(key_index),
                subtitle="",
                fill=fill,
                border=border,
                tooltip=(
                    f"{key_display_name(key_index)}\n"
                    f"Raw ADC: {raw_value}\n"
                    f"EMA ADC: {filtered_value}\n"
                    f"Calibrated ADC: {self._calibrated_values[key_index]}\n"
                    f"Distance: {self._distances_mm[key_index]:.2f} mm\n"
                    f"State: {'PRESSED' if pressed else 'IDLE'}"
                ),
            )

        self.summary_label.setText(
            f"Pressed {pressed_count} / {KEY_COUNT}  ·  Max raw {key_display_name(max_raw_key)} = {max_raw}  ·  "
            f"Max travel {key_display_name(max_travel_key)} = {max_travel:.2f} mm"
        )

    def _refresh_selected_signal_cards(self) -> None:
        selected = [idx for idx in self._selected_keys if 0 <= idx < KEY_COUNT]
        wanted = set(selected)

        for key_index in list(self._signal_probe_cards):
            if key_index in wanted:
                continue
            card = self._signal_probe_cards.pop(key_index)
            self.selected_cards_grid.removeWidget(card)
            card.deleteLater()

        for order, key_index in enumerate(selected):
            card = self._signal_probe_cards.get(key_index)
            if card is None:
                card = SignalProbeCard(key_index)
                self._signal_probe_cards[key_index] = card
            else:
                card.set_key_index(key_index)

            card.apply_values(
                raw_value=self._raw_values[key_index],
                filtered_value=self._filtered_values[key_index],
                calibrated_value=self._calibrated_values[key_index],
                distance_mm=self._distances_mm[key_index],
                normalized=self._distances_norm[key_index],
                pressed=bool(self._states[key_index]),
            )
            row = order // 2
            col = order % 2
            self.selected_cards_grid.addWidget(card, row, col)

    def _poll_sensor_once(self) -> None:
        try:
            adc_data = self.device.get_adc_values() or {}
            raw_values = self.device.get_all_raw_adc_values() or []
            filtered_values = self.device.get_all_filtered_adc_values() or []
            calibrated_values = self.device.get_all_calibrated_adc_values() or []
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
            self._filtered_values[key_index] = (
                int(filtered_values[key_index]) if key_index < len(filtered_values) else self._raw_values[key_index]
            )
            self._calibrated_values[key_index] = (
                int(calibrated_values[key_index]) if key_index < len(calibrated_values) else self._filtered_values[key_index]
            )

        states = list(key_states.get("states") or [])
        normalized = list(key_states.get("distances") or [])
        distances_mm = list(key_states.get("distances_mm") or [])
        for key_index in range(KEY_COUNT):
            self._states[key_index] = int(states[key_index]) if key_index < len(states) else 0
            self._distances_norm[key_index] = int(normalized[key_index]) if key_index < len(normalized) else 0
            self._distances_mm[key_index] = float(distances_mm[key_index]) if key_index < len(distances_mm) else 0.0

        self._refresh_selected_keys_summary()
        self._refresh_layout_view()
        self._refresh_focus_card()

        caps = bool(lock_states.get("caps_lock", False))
        num = bool(lock_states.get("num_lock", False))
        scroll = bool(lock_states.get("scroll_lock", False))
        self.lock_rows["Caps Lock"].set_text_and_level("ON" if caps else "OFF", "ok" if caps else "neutral")
        self.lock_rows["Num Lock"].set_text_and_level("ON" if num else "OFF", "ok" if num else "neutral")
        self.lock_rows["Scroll Lock"].set_text_and_level("ON" if scroll else "OFF", "ok" if scroll else "neutral")
        self.lock_rows["Caps LED Override"].set_text_and_level(
            "WHITE" if caps else "OFF", "ok" if caps else "neutral"
        )

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
                f"led {task_times.get('led', 0)} us | "
                f"total {task_times.get('total', 0)} us"
            )

        analog_monitor = adc_data.get("analog_monitor_us") or {}
        if analog_monitor:
            computed_max_index = (
                max(range(KEY_COUNT), key=lambda idx: self._raw_values[idx]) if self._raw_values else 0
            )
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
                f"max key {computed_max_index}"
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
                f"  Radial Deadzone: {gamepad.get('radial_deadzone', '--')}",
                f"  Keyboard Routing: {gamepad.get('keyboard_routing', '--')}",
                f"  Square Mode: {bool(gamepad.get('square_mode', False))}",
                f"  Reactive Stick: {bool(gamepad.get('reactive_stick', False))}",
                "",
                "Filter:",
                f"  Enabled: {'Unknown' if filter_enabled is None else bool(filter_enabled)}",
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
        if enabled is not None:
            self.filter_enabled_check.blockSignals(True)
            self.filter_enabled_check.setChecked(bool(enabled))
            self.filter_enabled_check.blockSignals(False)
        if params:
            self.noise_spin.setValue(int(params.get("noise_band", 30)))
            self.alpha_min_spin.setValue(int(params.get("alpha_min_denom", 32)))
            self.alpha_max_spin.setValue(int(params.get("alpha_max_denom", 4)))
        if enabled is None or not params:
            self._update_status("Filter settings unavailable from firmware; keeping current UI values.", "warning")

    def on_filter_enabled_change(self, enabled: bool) -> None:
        try:
            if not self.device.set_filter_enabled(bool(enabled)):
                raise RuntimeError("device rejected filter state")
        except Exception as exc:
            self._update_status(f"Failed to update filter state: {exc}", "error")
            return
        self.load_filter_settings()
        self._update_status(f"Filter {'enabled' if enabled else 'disabled'} live.", "success")

    def apply_filter_settings(self) -> None:
        noise = _clamp(self.noise_spin.value(), 1, 255)
        alpha_min = _clamp(self.alpha_min_spin.value(), 1, 255)
        alpha_max = _clamp(self.alpha_max_spin.value(), 1, 255)
        if alpha_min < alpha_max:
            alpha_min, alpha_max = alpha_max, alpha_min
            self.alpha_min_spin.setValue(alpha_min)
            self.alpha_max_spin.setValue(alpha_max)
        try:
            if not self.device.set_filter_params(noise, alpha_min, alpha_max):
                raise RuntimeError("device rejected filter params")
        except Exception as exc:
            self._update_status(f"Failed to apply filter params: {exc}", "error")
            return
        self.load_filter_settings()
        self._update_status(
            f"Filter params applied: band={noise}, alpha_min=1/{alpha_min}, alpha_max=1/{alpha_max}.",
            "success",
        )

    def reset_filter_defaults(self) -> None:
        self.filter_enabled_check.setChecked(bool(FILTER_DEFAULT_ENABLED))
        self.noise_spin.setValue(FILTER_DEFAULT_NOISE_BAND)
        self.alpha_min_spin.setValue(FILTER_DEFAULT_ALPHA_MIN_DENOM)
        self.alpha_max_spin.setValue(FILTER_DEFAULT_ALPHA_MAX_DENOM)
        self.apply_filter_settings()

    def on_page_activated(self) -> None:
        self._page_active = True
        self.load_filter_settings()
        self.refresh_config_display()
        if self.session.live_enabled:
            self._poll_sensor_once()

    def on_page_deactivated(self) -> None:
        self._page_active = False

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._poll_sensor_once()
