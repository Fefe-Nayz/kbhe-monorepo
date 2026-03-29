from __future__ import annotations

from PySide6.QtCore import Qt
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

from ..theme import current_colors
from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
    make_primary_button,
    make_secondary_button,
)


# (name, p1x, p1y, p2x, p2y)
_CURVE_PRESETS: list[tuple[str, int, int, int, int]] = [
    ("Linear",           85,  85, 170, 170),
    ("S-Curve",          50,  85, 170, 205),
    ("Aggressive",       20, 100, 100, 255),
    ("Slow Start",      100,  20, 200, 200),
    ("Fast Start",       20, 200, 100, 255),
    ("Ease In",         150,  60, 210, 210),
    ("Ease Out",         50,  50, 110, 200),
]


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, int(value)))


class CalibrationPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._current_key = int(getattr(session, "selected_key", 0))
        self._loading = False
        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
        except Exception:
            pass
        self._build_ui()
        self.apply_theme()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        try:
            self.session.selectedKeyChanged.connect(self.on_selected_key_changed)
        except Exception:
            pass
        self.reload()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Calibration",
            "Tune baseline zero points and analog curve shaping. "
            "Calibration values are applied live; persist them with Save to Flash.",
        )
        root.addWidget(scaffold, 1)

        # ── Top row: baseline + auto-calibration ──────────────────────
        top_row = QHBoxLayout()
        top_row.setSpacing(14)

        baseline_card = self._build_baseline_card()
        auto_card = self._build_auto_card()
        top_row.addWidget(baseline_card, 2)
        top_row.addWidget(auto_card, 1)

        scaffold.content_layout.addLayout(top_row)

        # ── Curve card ────────────────────────────────────────────────
        scaffold.add_card(self._build_curve_card())

        # ── Status chip ───────────────────────────────────────────────
        self.status_chip = StatusChip("Calibration page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

    def _build_baseline_card(self) -> SectionCard:
        card = SectionCard(
            "Baseline Zero Values",
            "The LUT reference and per-key zero values define the neutral ADC baseline "
            "used for travel conversion.",
        )

        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(8)
        card.body_layout.addLayout(grid)

        lut_lbl = QLabel("LUT Reference")
        lut_lbl.setObjectName("Muted")
        self.lut_zero_spin = QSpinBox()
        self.lut_zero_spin.setRange(-32768, 32767)
        grid.addWidget(lut_lbl, 0, 0)
        grid.addWidget(self.lut_zero_spin, 0, 1)

        self.key_zero_spins: list[QSpinBox] = []
        for i in range(6):
            lbl = QLabel(f"Key {i + 1}")
            lbl.setObjectName("Muted")
            spin = QSpinBox()
            spin.setRange(-32768, 32767)
            grid.addWidget(lbl, i + 1, 0)
            grid.addWidget(spin, i + 1, 1)
            self.key_zero_spins.append(spin)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Reload Values", self.load_calibration))
        actions.addWidget(make_primary_button("Apply Manual Values", self.apply_manual_calibration))
        actions.addStretch(1)
        card.body_layout.addLayout(actions)

        return card

    def _build_auto_card(self) -> SectionCard:
        card = SectionCard(
            "Auto Calibration",
            "Sample the current resting value for the focused key or all keys. "
            "Do this only when the switches are untouched.",
        )

        focused_row = QHBoxLayout()
        focused_row.setSpacing(8)
        focused_row.addWidget(QLabel("Focused key"))
        self.key_badge = StatusChip("Key 1", "info")
        focused_row.addWidget(self.key_badge)
        focused_row.addStretch(1)
        card.body_layout.addLayout(focused_row)

        card.body_layout.addStretch(1)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Auto-Calibrate Focused Key", self.auto_calibrate_selected_key))
        actions.addWidget(make_primary_button("Auto-Calibrate All Keys", self.auto_calibrate_all))
        card.body_layout.addLayout(actions)

        return card

    def _build_curve_card(self) -> SectionCard:
        card = SectionCard(
            "Analog Curve",
            "Each key stores two bezier control points. "
            "The selected key from the action bar determines which curve is shown.",
        )

        # Header row: editing badge + enable checkbox
        curve_header = QHBoxLayout()
        curve_header.setSpacing(8)
        editing_lbl = QLabel("Editing")
        editing_lbl.setObjectName("Muted")
        curve_header.addWidget(editing_lbl)
        self.curve_key_badge = StatusChip("Key 1", "info")
        curve_header.addWidget(self.curve_key_badge)
        curve_header.addStretch(1)
        self.curve_enabled_check = QCheckBox("Enable custom curve for this key")
        curve_header.addWidget(self.curve_enabled_check)
        card.body_layout.addLayout(curve_header)

        # Body: chart + controls side-by-side
        curve_body = QHBoxLayout()
        curve_body.setSpacing(14)
        card.body_layout.addLayout(curve_body)

        # Chart host
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
        self.curve_axis_x.setTitleText("Input")

        self.curve_axis_y = QValueAxis()
        self.curve_axis_y.setRange(0, 255)
        self.curve_axis_y.setTickCount(6)
        self.curve_axis_y.setLabelFormat("%d")
        self.curve_axis_y.setTitleText("Output")

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

        # Live position scatter series (a single dot on the curve)
        self.position_series = QScatterSeries()
        self.position_series.setMarkerSize(14)
        self.position_series.setMarkerShape(QScatterSeries.MarkerShape.MarkerShapeCircle)
        self.position_series.setColor(QColor(c["warning"]))
        self.position_series.setBorderColor(QColor(c["warning"]))
        self.curve_chart.addSeries(self.position_series)
        self.position_series.attachAxis(self.curve_axis_x)
        self.position_series.attachAxis(self.curve_axis_y)
        self.position_series.setVisible(False)

        # Control spinboxes + preset
        controls = QWidget()
        ctrl_layout = QVBoxLayout(controls)
        ctrl_layout.setContentsMargins(0, 0, 0, 0)
        ctrl_layout.setSpacing(10)

        # Preset row
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

        # Live position row
        live_row = QHBoxLayout()
        live_row.setSpacing(8)

        live_rate_lbl = QLabel("Global live")
        live_rate_lbl.setObjectName("Muted")
        live_row.addWidget(live_rate_lbl)

        self.position_rate_label = StatusChip("OFF", "neutral")
        live_row.addWidget(self.position_rate_label)

        live_row.addStretch(1)
        ctrl_layout.addLayout(live_row)

        # Spinboxes grid
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

        for row, (name, spin) in enumerate([
            ("P1 X", self.p1x_spin),
            ("P1 Y", self.p1y_spin),
            ("P2 X", self.p2x_spin),
            ("P2 Y", self.p2y_spin),
        ]):
            lbl = QLabel(name)
            lbl.setObjectName("Muted")
            spin_grid.addWidget(lbl, row, 0)
            spin_grid.addWidget(spin, row, 1)

        tip = QLabel("Tip: keep X coords ordered left-to-right for predictable curves.")
        tip.setObjectName("Muted")
        tip.setWordWrap(True)
        spin_grid.addWidget(tip, 4, 0, 1, 2)
        ctrl_layout.addWidget(spin_grid_widget)

        ctrl_layout.addStretch(1)
        curve_body.addWidget(controls, 1)

        # Curve actions
        curve_actions = QHBoxLayout()
        curve_actions.setSpacing(8)
        curve_actions.addWidget(make_secondary_button("Reload Curve", self.load_key_curve))
        curve_actions.addWidget(make_primary_button("Apply Curve", self.apply_analog_curve))
        curve_actions.addStretch(1)
        card.body_layout.addLayout(curve_actions)

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

    def _set_key_labels(self) -> None:
        text = f"Key {self._current_key + 1}"
        self.key_badge.setText(text)
        self.curve_key_badge.setText(text)

    # ------------------------------------------------------------------
    # Bezier curve
    # ------------------------------------------------------------------

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
        """Called when any spinbox changes; marks preset as Custom and redraws."""
        self._update_curve_preview()
        # If the current values don't match any preset, switch to Custom
        vals = (
            self.p1x_spin.value(), self.p1y_spin.value(),
            self.p2x_spin.value(), self.p2y_spin.value(),
        )
        for i, (_, p1x, p1y, p2x, p2y) in enumerate(_CURVE_PRESETS):
            if vals == (p1x, p1y, p2x, p2y):
                self.preset_combo.blockSignals(True)
                self.preset_combo.setCurrentIndex(i + 1)  # +1 because index 0 is "Custom"
                self.preset_combo.blockSignals(False)
                return
        self.preset_combo.blockSignals(True)
        self.preset_combo.setCurrentIndex(0)  # Custom
        self.preset_combo.blockSignals(False)

    def _on_preset_selected(self, index: int) -> None:
        data = self.preset_combo.itemData(index)
        if data is None:
            return
        p1x, p1y, p2x, p2y = data
        for spin, val in zip(
            (self.p1x_spin, self.p1y_spin, self.p2x_spin, self.p2y_spin),
            (p1x, p1y, p2x, p2y),
        ):
            spin.blockSignals(True)
            spin.setValue(val)
            spin.blockSignals(False)
        self._update_curve_preview()

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.position_rate_label.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
            self.position_series.setVisible(True)
            self._update_position_dot()
        else:
            self.position_rate_label.set_text_and_level("OFF", "neutral")
            self.position_series.setVisible(False)
            self.position_series.clear()

    def _update_position_dot(self) -> None:
        try:
            key_states = self.device.get_key_states()
            distances = (key_states.get("distances") or [0] * 6) if key_states else [0] * 6
            travel = float(distances[self._current_key]) if self._current_key < len(distances) else 0.0
        except Exception:
            return

        p0 = (0, 0)
        p1 = (self.p1x_spin.value(), self.p1y_spin.value())
        p2 = (self.p2x_spin.value(), self.p2y_spin.value())
        p3 = (255, 255)

        # Binary-search for the t that gives x ≈ travel
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

    def on_live_tick(self) -> None:
        if not self.session.live_enabled:
            return
        self._update_position_dot()

    def _update_curve_preview(self) -> None:
        self.curve_series.clear()
        p0 = (0, 0)
        p1 = (_clamp(self.p1x_spin.value(), 0, 255), _clamp(self.p1y_spin.value(), 0, 255))
        p2 = (_clamp(self.p2x_spin.value(), 0, 255), _clamp(self.p2y_spin.value(), 0, 255))
        p3 = (255, 255)
        for step in range(101):
            x, y = self._bezier_point(step / 100.0, p0, p1, p2, p3)
            self.curve_series.append(x, y)

    def apply_theme(self) -> None:
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

    # ------------------------------------------------------------------
    # Data loading / actions
    # ------------------------------------------------------------------

    def reload(self) -> None:
        self.load_calibration()
        self.load_key_curve(self._current_key)
        self._set_key_labels()

    def load_calibration(self) -> None:
        try:
            calibration = self.device.get_calibration()
            if not calibration:
                raise RuntimeError("device returned no calibration")
        except Exception as exc:
            self._update_status(f"Failed to load calibration: {exc}", "error")
            return

        self._loading = True
        self.lut_zero_spin.setValue(int(calibration.get("lut_zero_value", 0)))
        key_values = calibration.get("key_zero_values") or [0] * 6
        for i, spin in enumerate(self.key_zero_spins):
            spin.setValue(int(key_values[i]) if i < len(key_values) else 0)
        self._loading = False
        self._update_status("Calibration values loaded.", "success")

    def apply_manual_calibration(self) -> None:
        lut_zero = int(self.lut_zero_spin.value())
        key_zeros = [int(s.value()) for s in self.key_zero_spins]
        try:
            if not self.device.set_calibration(lut_zero, key_zeros):
                raise RuntimeError("device rejected calibration values")
        except Exception as exc:
            self._update_status(f"Error applying calibration: {exc}", "error")
            return
        self._update_status("Manual calibration applied live.", "success")

    def auto_calibrate_selected_key(self) -> None:
        try:
            result = self.device.auto_calibrate(self._current_key)
            if not result:
                raise RuntimeError("device rejected auto-calibration")
        except Exception as exc:
            self._update_status(f"Auto-calibration failed: {exc}", "error")
            return
        self.load_calibration()
        self._update_status(f"Auto-calibrated Key {self._current_key + 1}.", "success")

    def auto_calibrate_all(self) -> None:
        reply = QMessageBox.question(
            self,
            "Auto-Calibrate All",
            "Sample the resting zero point for all keys now?",
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
        self._update_status("Auto-calibrated all keys.", "success")

    def load_key_curve(self, key_index: int | None = None) -> None:
        if key_index is None:
            key_index = self._current_key
        self._current_key = _clamp(key_index, 0, 5)
        self._set_key_labels()
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
        self._on_spin_changed()  # sync preset combo
        self._update_status(f"Loaded curve for Key {self._current_key + 1}.", "success")

    def apply_analog_curve(self) -> None:
        payload = {
            "enabled": self.curve_enabled_check.isChecked(),
            "p1_x": _clamp(self.p1x_spin.value(), 0, 255),
            "p1_y": _clamp(self.p1y_spin.value(), 0, 255),
            "p2_x": _clamp(self.p2x_spin.value(), 0, 255),
            "p2_y": _clamp(self.p2y_spin.value(), 0, 255),
        }
        try:
            ok = self.device.set_key_curve(
                self._current_key,
                payload["enabled"],
                payload["p1_x"],
                payload["p1_y"],
                payload["p2_x"],
                payload["p2_y"],
            )
            if not ok:
                raise RuntimeError("device rejected the curve")
        except Exception as exc:
            self._update_status(f"Error applying curve: {exc}", "error")
            return
        self._update_status(f"Applied curve to Key {self._current_key + 1}.", "success")

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_selected_key_changed(self, key_index: int) -> None:
        self.load_key_curve(key_index)

    def on_page_activated(self) -> None:
        self.reload()
        if self.session.live_enabled:
            self._update_position_dot()

    def on_page_deactivated(self) -> None:
        pass
