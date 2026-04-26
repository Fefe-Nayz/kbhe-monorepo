from __future__ import annotations

from PySide6.QtCore import Qt, QTimer
from PySide6.QtCharts import QChart, QChartView, QLineSeries, QScatterSeries, QValueAxis
from PySide6.QtGui import QBrush, QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from kbhe_tool.key_layout import key_display_name
from kbhe_tool.protocol import KEY_COUNT
from ..theme import current_colors
from ..widgets import (
    KeyboardLayoutWidget,
    PageScaffold,
    SectionCard,
    StatusChip,
    make_danger_button,
    make_primary_button,
    make_secondary_button,
)


_CURVE_PRESETS: list[tuple[str, int, int, int, int]] = [
    ("Linear", 85, 85, 170, 170),
    ("S-Curve", 50, 85, 170, 205),
    ("Aggressive", 20, 100, 100, 255),
    ("Slow Start", 100, 20, 200, 200),
    ("Fast Start", 20, 200, 100, 255),
    ("Ease In", 150, 60, 210, 210),
    ("Ease Out", 50, 50, 110, 200),
]

_GUIDED_PHASE_LABELS = {
    0: ("Idle", "neutral"),
    1: ("Release / Baseline", "warn"),
    2: ("Waiting For Press", "info"),
    3: ("Tracking Max", "info"),
    4: ("Complete", "ok"),
    5: ("Aborted", "warn"),
    6: ("Error", "bad"),
}


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, int(value)))


class CalibrationPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._current_key = _clamp(int(getattr(session, "selected_key", 0)), 0, KEY_COUNT - 1)
        self._key_zero_values = [0] * KEY_COUNT
        self._key_max_values = [4095] * KEY_COUNT
        self._loading = False
        self._guided_active = False
        self._last_guided_phase: int | None = None
        self._manual_apply_timer = QTimer(self)
        self._manual_apply_timer.setSingleShot(True)
        self._manual_apply_timer.setInterval(180)
        self._manual_apply_timer.timeout.connect(
            lambda: self.apply_manual_calibration(announce=False)
        )

        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
            self.session.selectedKeyChanged.connect(self.on_selected_key_changed)
        except Exception:
            pass

        self._build_ui()
        self.apply_theme()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        self.reload()

    def _has_curve_ui(self) -> bool:
        return hasattr(self, "curve_key_badge")

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Calibration",
            "Calibre le zero au repos et la course max par touche. "
            "Les edits manuels s'appliquent en live et s'autosauvent apres un court idle. "
            "La forme de reponse analogique gamepad est maintenant geree depuis la page Gamepad.",
        )
        root.addWidget(scaffold, 1)

        top_row = QHBoxLayout()
        top_row.setSpacing(14)
        top_row.setAlignment(Qt.AlignTop)
        top_row.addWidget(self._build_layout_card(), 2)
        top_row.addWidget(self._build_manual_card(), 2)
        top_row.addWidget(self._build_guided_card(), 1)
        scaffold.content_layout.addLayout(top_row)

        self.status_chip = StatusChip("Calibration page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _build_layout_card(self) -> SectionCard:
        card = SectionCard(
            "Key Layout",
            "Selectionne la touche a calibrer. La couleur reflete l'ecart entre "
            "le zero mesure et la reference LUT.",
        )

        self.layout_view = KeyboardLayoutWidget(self.session, unit=32)
        card.body_layout.addWidget(self.layout_view, 0, Qt.AlignTop | Qt.AlignLeft)

        chips = QHBoxLayout()
        chips.setSpacing(8)
        self.layout_focus_chip = StatusChip(key_display_name(self._current_key), "info")
        self.layout_zero_chip = StatusChip("Zero 0", "neutral")
        self.layout_max_chip = StatusChip("Max 4095", "neutral")
        chips.addWidget(self.layout_focus_chip)
        chips.addWidget(self.layout_zero_chip)
        chips.addWidget(self.layout_max_chip)
        chips.addStretch(1)
        card.body_layout.addLayout(chips)

        hint = QLabel(
            "Bleu = touche selectionnee. Les tooltips affichent zero, max et span calibre."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        card.body_layout.addWidget(hint)
        return card

    def _build_manual_card(self) -> SectionCard:
        card = SectionCard(
            "Manual Zero / Max",
            "Edite les valeurs de repos et de pression max de la touche focalisee. "
            "Ces donnees pilotent les graphes en % et les effets distance. "
            "Les edits manuels s'appliquent en live; les actions Auto Zero et Guided restent explicites.",
        )

        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(8)
        card.body_layout.addLayout(grid)

        lut_lbl = QLabel("LUT Reference")
        lut_lbl.setObjectName("Muted")
        self.lut_zero_spin = QSpinBox()
        self.lut_zero_spin.setRange(-32768, 32767)
        self.lut_zero_spin.valueChanged.connect(self._on_lut_zero_changed)
        grid.addWidget(lut_lbl, 0, 0)
        grid.addWidget(self.lut_zero_spin, 0, 1)

        self.focused_key_zero_label = QLabel("Focused key zero")
        self.focused_key_zero_label.setObjectName("Muted")
        self.key_zero_spin = QSpinBox()
        self.key_zero_spin.setRange(-32768, 32767)
        self.key_zero_spin.valueChanged.connect(self._on_key_zero_changed)
        grid.addWidget(self.focused_key_zero_label, 1, 0)
        grid.addWidget(self.key_zero_spin, 1, 1)

        self.focused_key_max_label = QLabel("Focused key max")
        self.focused_key_max_label.setObjectName("Muted")
        self.key_max_spin = QSpinBox()
        self.key_max_spin.setRange(-32768, 32767)
        self.key_max_spin.valueChanged.connect(self._on_key_max_changed)
        grid.addWidget(self.focused_key_max_label, 2, 0)
        grid.addWidget(self.key_max_spin, 2, 1)

        hint = QLabel(
            "Le span utile d'une touche est `max - zero`. Un mauvais max fausse "
            "les vues Normalized (%) et Distance Sensor."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        grid.addWidget(hint, 3, 0, 1, 2)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Reload Values", self.load_calibration))
        actions.addWidget(make_secondary_button("Auto Zero Focused", self.auto_calibrate_selected_key))
        actions.addWidget(make_secondary_button("Auto Zero All", self.auto_calibrate_all))
        actions.addStretch(1)
        card.body_layout.addLayout(actions)
        return card

    def _build_guided_card(self) -> SectionCard:
        card = SectionCard(
            "Guided LED Calibration",
            "Le clavier pulse rouge pendant la moyenne des valeurs au repos, puis "
            "guide chaque touche en rouge pour capturer son maximum avant relachement.",
        )

        status_row = QHBoxLayout()
        status_row.setSpacing(8)
        self.guided_phase_chip = StatusChip("Idle", "neutral")
        self.guided_progress_chip = StatusChip("0%", "neutral")
        self.guided_target_chip = StatusChip(key_display_name(self._current_key), "info")
        status_row.addWidget(self.guided_phase_chip)
        status_row.addWidget(self.guided_progress_chip)
        status_row.addWidget(self.guided_target_chip)
        status_row.addStretch(1)
        card.body_layout.addLayout(status_row)

        self.guided_description = QLabel(
            "Demarre la sequence, ne touche pas le clavier pendant la phase rouge, "
            "puis suis la LED rouge touche par touche."
        )
        self.guided_description.setObjectName("Muted")
        self.guided_description.setWordWrap(True)
        card.body_layout.addWidget(self.guided_description)

        self.guided_samples_label = QLabel("Samples: 0")
        self.guided_samples_label.setObjectName("Muted")
        card.body_layout.addWidget(self.guided_samples_label)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        self.guided_start_button = make_primary_button(
            "Start Guided Calibration", self.start_guided_calibration
        )
        self.guided_abort_button = make_danger_button(
            "Abort", self.abort_guided_calibration
        )
        actions.addWidget(self.guided_start_button)
        actions.addWidget(self.guided_abort_button)
        actions.addStretch(1)
        card.body_layout.addLayout(actions)
        return card

    def _build_curve_card(self) -> SectionCard:
        card = SectionCard(
            "Analog Curve",
            "La courbe de la touche focalisee s'applique apres calibration. "
            "Le point live suit la donnee normalized du firmware.",
        )

        curve_header = QHBoxLayout()
        curve_header.setSpacing(8)
        editing_lbl = QLabel("Editing")
        editing_lbl.setObjectName("Muted")
        curve_header.addWidget(editing_lbl)
        self.curve_key_badge = StatusChip(key_display_name(self._current_key), "info")
        curve_header.addWidget(self.curve_key_badge)
        curve_header.addStretch(1)
        self.curve_enabled_check = QCheckBox("Enable custom curve for this key")
        curve_header.addWidget(self.curve_enabled_check)
        card.body_layout.addLayout(curve_header)

        curve_body = QHBoxLayout()
        curve_body.setSpacing(14)
        card.body_layout.addLayout(curve_body)

        chart_host = QFrame()
        chart_host.setObjectName("SubCard")
        chart_host_layout = QVBoxLayout(chart_host)
        chart_host_layout.setContentsMargins(10, 10, 10, 10)

        self.curve_chart = QChart()
        self.curve_chart.legend().hide()
        self.curve_chart.setBackgroundVisible(False)
        self.curve_series = QLineSeries()
        c = current_colors()
        self.curve_series.setPen(QPen(QColor(c["accent"]), 2))
        self.curve_chart.addSeries(self.curve_series)

        self.curve_axis_x = QValueAxis()
        self.curve_axis_x.setRange(0, 255)
        self.curve_axis_x.setTickCount(6)
        self.curve_axis_x.setLabelFormat("%d")
        self.curve_axis_x.setTitleText("Normalized Input")

        self.curve_axis_y = QValueAxis()
        self.curve_axis_y.setRange(0, 255)
        self.curve_axis_y.setTickCount(6)
        self.curve_axis_y.setLabelFormat("%d")
        self.curve_axis_y.setTitleText("Curve Output")

        self.curve_chart.addAxis(self.curve_axis_x, Qt.AlignBottom)
        self.curve_chart.addAxis(self.curve_axis_y, Qt.AlignLeft)
        self.curve_series.attachAxis(self.curve_axis_x)
        self.curve_series.attachAxis(self.curve_axis_y)

        self.curve_chart_view = QChartView(self.curve_chart)
        self.curve_chart_view.setRenderHint(QPainter.Antialiasing)
        self.curve_chart_view.setMinimumHeight(300)
        self.curve_chart_view.setBackgroundBrush(QBrush(Qt.GlobalColor.transparent))
        self.curve_chart_view.viewport().setAutoFillBackground(False)
        chart_host_layout.addWidget(self.curve_chart_view)
        curve_body.addWidget(chart_host, 2)

        self.position_series = QScatterSeries()
        self.position_series.setMarkerSize(14)
        self.position_series.setMarkerShape(QScatterSeries.MarkerShape.MarkerShapeCircle)
        self.position_series.setColor(QColor(c["warning"]))
        self.position_series.setBorderColor(QColor(c["warning"]))
        self.curve_chart.addSeries(self.position_series)
        self.position_series.attachAxis(self.curve_axis_x)
        self.position_series.attachAxis(self.curve_axis_y)
        self.position_series.setVisible(False)

        controls = QWidget()
        ctrl_layout = QVBoxLayout(controls)
        ctrl_layout.setContentsMargins(0, 0, 0, 0)
        ctrl_layout.setSpacing(10)

        preset_row = QHBoxLayout()
        preset_row.setSpacing(8)
        preset_lbl = QLabel("Preset")
        preset_lbl.setObjectName("Muted")
        preset_row.addWidget(preset_lbl)
        self.preset_combo = QComboBox()
        self.preset_combo.addItem("Custom", None)
        for name, p1x, p1y, p2x, p2y in _CURVE_PRESETS:
            self.preset_combo.addItem(name, (p1x, p1y, p2x, p2y))
        self.preset_combo.currentIndexChanged.connect(self._on_preset_selected)
        preset_row.addWidget(self.preset_combo, 1)
        ctrl_layout.addLayout(preset_row)

        live_row = QHBoxLayout()
        live_row.setSpacing(8)
        live_rate_lbl = QLabel("Global live")
        live_rate_lbl.setObjectName("Muted")
        live_row.addWidget(live_rate_lbl)
        self.position_rate_label = StatusChip("OFF", "neutral")
        live_row.addWidget(self.position_rate_label)
        live_row.addStretch(1)
        ctrl_layout.addLayout(live_row)

        spin_grid_widget = QWidget()
        spin_grid = QGridLayout(spin_grid_widget)
        spin_grid.setContentsMargins(0, 0, 0, 0)
        spin_grid.setHorizontalSpacing(10)
        spin_grid.setVerticalSpacing(10)

        self.p1x_spin = QSpinBox()
        self.p1y_spin = QSpinBox()
        self.p2x_spin = QSpinBox()
        self.p2y_spin = QSpinBox()
        for spin in (self.p1x_spin, self.p1y_spin, self.p2x_spin, self.p2y_spin):
            spin.setRange(0, 255)
            spin.valueChanged.connect(self._on_spin_changed)

        for row, (name, spin) in enumerate(
            [
                ("P1 X", self.p1x_spin),
                ("P1 Y", self.p1y_spin),
                ("P2 X", self.p2x_spin),
                ("P2 Y", self.p2y_spin),
            ]
        ):
            lbl = QLabel(name)
            lbl.setObjectName("Muted")
            spin_grid.addWidget(lbl, row, 0)
            spin_grid.addWidget(spin, row, 1)

        tip = QLabel("Garde les X ordonnes de gauche a droite pour une courbe stable.")
        tip.setObjectName("Muted")
        tip.setWordWrap(True)
        spin_grid.addWidget(tip, 4, 0, 1, 2)
        ctrl_layout.addWidget(spin_grid_widget)
        ctrl_layout.addStretch(1)
        curve_body.addWidget(controls, 1)

        curve_actions = QHBoxLayout()
        curve_actions.setSpacing(8)
        curve_actions.addWidget(make_secondary_button("Reload Curve", self.load_key_curve))
        curve_actions.addWidget(make_primary_button("Apply Curve", self.apply_analog_curve))
        curve_actions.addStretch(1)
        card.body_layout.addLayout(curve_actions)
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

    def _set_key_labels(self) -> None:
        text = key_display_name(self._current_key)
        self.layout_focus_chip.setText(text)
        if self._has_curve_ui():
            self.curve_key_badge.setText(text)
        self.focused_key_zero_label.setText(f"{text} zero")
        self.focused_key_max_label.setText(f"{text} max")
        self.guided_target_chip.setText(text)

    def _on_key_zero_changed(self, value: int) -> None:
        if self._loading:
            return
        self._key_zero_values[self._current_key] = int(value)
        self._refresh_layout_overview()
        self._schedule_manual_apply()

    def _on_key_max_changed(self, value: int) -> None:
        if self._loading:
            return
        self._key_max_values[self._current_key] = int(value)
        self._refresh_layout_overview()
        self._schedule_manual_apply()

    def _on_lut_zero_changed(self, _value: int) -> None:
        if self._loading:
            return
        self._refresh_layout_overview()
        self._schedule_manual_apply()

    def _schedule_manual_apply(self) -> None:
        if self._loading:
            return
        self._manual_apply_timer.start()

    @staticmethod
    def _bezier_point(
        t: float,
        p0: tuple[int, int],
        p1: tuple[int, int],
        p2: tuple[int, int],
        p3: tuple[int, int],
    ) -> tuple[float, float]:
        mt = 1.0 - t
        x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
        y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
        return x, y

    def _on_spin_changed(self) -> None:
        if not self._has_curve_ui():
            return
        self._update_curve_preview()
        vals = (
            self.p1x_spin.value(),
            self.p1y_spin.value(),
            self.p2x_spin.value(),
            self.p2y_spin.value(),
        )
        for index, (_, p1x, p1y, p2x, p2y) in enumerate(_CURVE_PRESETS, start=1):
            if vals == (p1x, p1y, p2x, p2y):
                self.preset_combo.blockSignals(True)
                self.preset_combo.setCurrentIndex(index)
                self.preset_combo.blockSignals(False)
                return
        self.preset_combo.blockSignals(True)
        self.preset_combo.setCurrentIndex(0)
        self.preset_combo.blockSignals(False)

    def _on_preset_selected(self, index: int) -> None:
        if not self._has_curve_ui():
            return
        data = self.preset_combo.itemData(index)
        if data is None:
            return
        for spin, val in zip((self.p1x_spin, self.p1y_spin, self.p2x_spin, self.p2y_spin), data):
            spin.blockSignals(True)
            spin.setValue(val)
            spin.blockSignals(False)
        self._update_curve_preview()

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if not hasattr(self, "position_rate_label") or not hasattr(self, "position_series"):
            return
        if enabled:
            self.position_rate_label.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
            self.position_series.setVisible(True)
            self._update_position_dot()
        else:
            self.position_rate_label.set_text_and_level("OFF", "neutral")
            self.position_series.setVisible(False)
            self.position_series.clear()

    def _update_position_dot(self) -> None:
        if not hasattr(self, "position_series"):
            return
        try:
            key_states = self.device.get_key_states() or {}
            travel_values = key_states.get("distances") or [0] * KEY_COUNT
            travel = float(travel_values[self._current_key]) if self._current_key < len(travel_values) else 0.0
        except Exception:
            return

        p0 = (0, 0)
        p1 = (self.p1x_spin.value(), self.p1y_spin.value())
        p2 = (self.p2x_spin.value(), self.p2y_spin.value())
        p3 = (255, 255)

        lo, hi = 0.0, 1.0
        for _ in range(20):
            mid = (lo + hi) / 2.0
            bx, _ = self._bezier_point(mid, p0, p1, p2, p3)
            if bx < travel:
                lo = mid
            else:
                hi = mid
        _, by = self._bezier_point((lo + hi) / 2.0, p0, p1, p2, p3)

        self.position_series.clear()
        self.position_series.append(travel, by)

    def _update_curve_preview(self) -> None:
        if not hasattr(self, "curve_series"):
            return
        self.curve_series.clear()
        p0 = (0, 0)
        p1 = (_clamp(self.p1x_spin.value(), 0, 255), _clamp(self.p1y_spin.value(), 0, 255))
        p2 = (_clamp(self.p2x_spin.value(), 0, 255), _clamp(self.p2y_spin.value(), 0, 255))
        p3 = (255, 255)
        for step in range(101):
            x, y = self._bezier_point(step / 100.0, p0, p1, p2, p3)
            self.curve_series.append(x, y)

    def _refresh_layout_overview(self) -> None:
        lut_zero = int(self.lut_zero_spin.value())
        max_delta = max(1, max(abs(int(value) - lut_zero) for value in self._key_zero_values))
        colors = current_colors()

        for index in range(KEY_COUNT):
            zero = int(self._key_zero_values[index])
            max_value = int(self._key_max_values[index])
            span = max_value - zero
            delta = zero - lut_zero
            magnitude = min(1.0, abs(delta) / max_delta)
            if delta >= 0:
                fill = QColor("#dbeafe")
                target = QColor("#60a5fa")
            else:
                fill = QColor("#dcfce7")
                target = QColor("#34d399")
            r = int(fill.red() + (target.red() - fill.red()) * magnitude)
            g = int(fill.green() + (target.green() - fill.green()) * magnitude)
            b = int(fill.blue() + (target.blue() - fill.blue()) * magnitude)
            self.layout_view.set_key_state(
                index,
                fill=QColor(r, g, b).name(),
                border=colors["accent"] if index == self._current_key else colors["border"],
                tooltip=(
                    f"{key_display_name(index)}\n"
                    f"Zero: {zero}\n"
                    f"Max: {max_value}\n"
                    f"Span: {span}\n"
                    f"Δ vs LUT: {delta:+d}"
                ),
            )

        current_zero = int(self._key_zero_values[self._current_key])
        current_max = int(self._key_max_values[self._current_key])
        self.layout_zero_chip.set_text_and_level(f"Zero {current_zero}", "neutral")
        self.layout_max_chip.set_text_and_level(f"Max {current_max}", "neutral")

    def _guided_phase_message(self, status: dict[str, int | bool] | None) -> tuple[str, str]:
        if not status:
            return ("Calibration guidee inactive.", "neutral")

        phase = int(status.get("phase", 0))
        key_index = _clamp(int(status.get("current_key", 0)), 0, KEY_COUNT - 1)
        if phase == 1:
            return (
                "Ne touche pas le clavier. Les valeurs au repos sont moyennées pendant quelques secondes.",
                "warn",
            )
        if phase == 2:
            return (
                f"Appuie a fond sur {key_display_name(key_index)}, puis relache-la completement.",
                "info",
            )
        if phase == 3:
            return (
                f"Maintiens {key_display_name(key_index)} enfoncee pour capturer sa valeur max, puis relache.",
                "info",
            )
        if phase == 4:
            return ("Calibration terminee, les nouvelles valeurs sont sauvegardees.", "success")
        if phase == 6:
            return ("La calibration guidee a echoue. Verifie la communication et recommence.", "error")
        return ("Calibration guidee inactive.", "neutral")

    def _refresh_guided_ui(self, status: dict[str, int | bool] | None) -> None:
        phase = int(status.get("phase", 0)) if status else 0
        active = bool(status.get("active", False)) if status else False
        current_key = _clamp(int(status.get("current_key", self._current_key)), 0, KEY_COUNT - 1)
        progress = int(status.get("progress_percent", 0)) if status else 0
        samples = int(status.get("sample_count", 0)) if status else 0
        elapsed_ms = int(status.get("phase_elapsed_ms", 0)) if status else 0

        label, level = _GUIDED_PHASE_LABELS.get(phase, ("Idle", "neutral"))
        self.guided_phase_chip.set_text_and_level(label, level)
        self.guided_progress_chip.set_text_and_level(f"{progress}%", "info" if active else "neutral")
        self.guided_target_chip.setText(key_display_name(current_key))
        self.guided_samples_label.setText(f"Samples: {samples} · Phase time: {elapsed_ms} ms")

        message, msg_kind = self._guided_phase_message(status)
        self.guided_description.setText(message)
        self.guided_start_button.setEnabled(not active)
        self.guided_abort_button.setEnabled(active)
        self._guided_active = active

        if phase != self._last_guided_phase and phase in (4, 6):
            self.load_calibration()
            self._update_status(message, "success" if phase == 4 else "error")
        elif phase != self._last_guided_phase and active:
            self._update_status(message, "warning" if msg_kind == "warn" else "info")

        self._last_guided_phase = phase

    def reload(self) -> None:
        self.load_calibration()
        self._poll_guided_status()

    def load_calibration(self) -> None:
        try:
            calibration = self.device.get_calibration()
            if not calibration:
                raise RuntimeError("device returned no calibration")
        except Exception as exc:
            self._update_status(f"Failed to load calibration: {exc}", "error")
            return

        self._manual_apply_timer.stop()
        key_zero_values = list(calibration.get("key_zero_values") or [0] * KEY_COUNT)
        key_max_values = list(calibration.get("key_max_values") or [4095] * KEY_COUNT)
        if len(key_zero_values) < KEY_COUNT:
            key_zero_values.extend([0] * (KEY_COUNT - len(key_zero_values)))
        if len(key_max_values) < KEY_COUNT:
            key_max_values.extend([4095] * (KEY_COUNT - len(key_max_values)))

        self._loading = True
        self.lut_zero_spin.setValue(int(calibration.get("lut_zero_value", 0)))
        self._key_zero_values = key_zero_values[:KEY_COUNT]
        self._key_max_values = key_max_values[:KEY_COUNT]
        self.key_zero_spin.setValue(int(self._key_zero_values[self._current_key]))
        self.key_max_spin.setValue(int(self._key_max_values[self._current_key]))
        self._loading = False
        self._set_key_labels()
        self._refresh_layout_overview()
        self._update_status("Calibration values loaded.", "success")

    def apply_manual_calibration(self, announce: bool = True) -> None:
        lut_zero = int(self.lut_zero_spin.value())
        self._key_zero_values[self._current_key] = int(self.key_zero_spin.value())
        self._key_max_values[self._current_key] = int(self.key_max_spin.value())
        try:
            if not self.device.set_calibration(lut_zero, self._key_zero_values, self._key_max_values):
                raise RuntimeError("device rejected calibration values")
        except Exception as exc:
            self._update_status(f"Error applying calibration: {exc}", "error")
            return
        self._refresh_layout_overview()
        if announce:
            self._update_status(
                f"Manual zero/max calibration updated live for {key_display_name(self._current_key)}.",
                "success",
            )

    def auto_calibrate_selected_key(self) -> None:
        try:
            result = self.device.auto_calibrate(self._current_key)
            if not result:
                raise RuntimeError("device rejected auto-calibration")
        except Exception as exc:
            self._update_status(f"Auto-calibration failed: {exc}", "error")
            return
        self.load_calibration()
        self._update_status(f"Auto zero captured for {key_display_name(self._current_key)}.", "success")

    def auto_calibrate_all(self) -> None:
        reply = QMessageBox.question(
            self,
            "Auto-Calibrate All Zeros",
            "Capture the current resting zero point for all keys now?",
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        try:
            result = self.device.auto_calibrate(0xFF)
            if not result:
                raise RuntimeError("device rejected auto-calibration")
        except Exception as exc:
            self._update_status(f"Auto-calibration failed: {exc}", "error")
            return
        self.load_calibration()
        self._update_status("Auto zero captured for all keys.", "success")

    def start_guided_calibration(self) -> None:
        reply = QMessageBox.question(
            self,
            "Guided Calibration",
            "Start the LED-guided calibration sequence now? The keyboard will take control of the LEDs.",
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        try:
            status = self.device.guided_calibration_start()
            if not status:
                raise RuntimeError("device rejected guided calibration start")
        except Exception as exc:
            self._update_status(f"Unable to start guided calibration: {exc}", "error")
            return
        self._refresh_guided_ui(status)

    def abort_guided_calibration(self) -> None:
        try:
            status = self.device.guided_calibration_abort()
        except Exception as exc:
            self._update_status(f"Unable to abort guided calibration: {exc}", "error")
            return
        self._refresh_guided_ui(status)
        self._update_status("Guided calibration aborted.", "warning")

    def _poll_guided_status(self) -> None:
        try:
            status = self.device.guided_calibration_status()
        except Exception:
            status = None
        self._refresh_guided_ui(status)

    def load_key_curve(self, key_index: int | None = None) -> None:
        if key_index is None:
            key_index = self._current_key
        self._current_key = _clamp(key_index, 0, KEY_COUNT - 1)
        self._set_key_labels()
        self._loading = True
        self.key_zero_spin.setValue(int(self._key_zero_values[self._current_key]))
        self.key_max_spin.setValue(int(self._key_max_values[self._current_key]))
        self._loading = False

        if not self._has_curve_ui():
            self._refresh_layout_overview()
            self._update_status(
                "Analog curve editing moved to the Gamepad page.",
                "info",
            )
            return

        try:
            curve = self.device.get_key_curve(self._current_key)
            if not curve:
                raise RuntimeError("device returned no curve")
        except Exception as exc:
            self._update_status(f"Error loading curve: {exc}", "error")
            return

        self._loading = True
        self.curve_enabled_check.setChecked(bool(curve.get("curve_enabled", False)))
        for spin, val in zip(
            (self.p1x_spin, self.p1y_spin, self.p2x_spin, self.p2y_spin),
            (
                int(curve.get("p1_x", 85)),
                int(curve.get("p1_y", 85)),
                int(curve.get("p2_x", 170)),
                int(curve.get("p2_y", 170)),
            ),
        ):
            spin.blockSignals(True)
            spin.setValue(val)
            spin.blockSignals(False)
        self._loading = False
        self._update_curve_preview()
        self._on_spin_changed()
        self._refresh_layout_overview()
        self._update_status(f"Loaded curve for {key_display_name(self._current_key)}.", "success")

    def apply_analog_curve(self) -> None:
        if not self._has_curve_ui():
            self._update_status(
                "Analog curve editing moved to the Gamepad page.",
                "warning",
            )
            return
        try:
            ok = self.device.set_key_curve(
                self._current_key,
                self.curve_enabled_check.isChecked(),
                _clamp(self.p1x_spin.value(), 0, 255),
                _clamp(self.p1y_spin.value(), 0, 255),
                _clamp(self.p2x_spin.value(), 0, 255),
                _clamp(self.p2y_spin.value(), 0, 255),
            )
            if not ok:
                raise RuntimeError("device rejected the curve")
        except Exception as exc:
            self._update_status(f"Error applying curve: {exc}", "error")
            return
        self._update_status(f"Applied curve to {key_display_name(self._current_key)}.", "success")

    def on_selected_key_changed(self, key_index: int) -> None:
        self._current_key = _clamp(key_index, 0, KEY_COUNT - 1)
        self._set_key_labels()
        self._loading = True
        self.key_zero_spin.setValue(int(self._key_zero_values[self._current_key]))
        self.key_max_spin.setValue(int(self._key_max_values[self._current_key]))
        self._loading = False
        self._refresh_layout_overview()

    def on_live_tick(self) -> None:
        if self.session.live_enabled and hasattr(self, "position_series"):
            self._update_position_dot()
        if self._guided_active or self.isVisible():
            self._poll_guided_status()

    def apply_theme(self) -> None:
        if not hasattr(self, "curve_series"):
            return
        c = current_colors()
        self.curve_series.setPen(QPen(QColor(c["accent"]), 2))
        if hasattr(self, "position_series"):
            self.position_series.setColor(QColor(c["warning"]))
            self.position_series.setBorderColor(QColor(c["warning"]))
        text_brush = QBrush(QColor(c["text_muted"]))
        grid_pen = QPen(QColor(c["border"]), 1)
        line_pen = QPen(QColor(c["border"]), 1)
        for axis in (self.curve_axis_x, self.curve_axis_y):
            axis.setLabelsBrush(text_brush)
            axis.setTitleBrush(text_brush)
            axis.setGridLinePen(grid_pen)
            axis.setLinePen(line_pen)

    def on_page_activated(self) -> None:
        self.reload()
        if self.session.live_enabled and hasattr(self, "position_series"):
            self._update_position_dot()
        self._poll_guided_status()

    def on_page_deactivated(self) -> None:
        self._manual_apply_timer.stop()
