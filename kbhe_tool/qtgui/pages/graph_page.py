from __future__ import annotations

from PySide6.QtCharts import QChart, QChartView, QLineSeries, QValueAxis
from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QBrush, QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ..theme import current_colors
from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
    make_secondary_button,
)


def _channel_colors() -> list[str]:
    c = current_colors()
    return [c["graph_1"], c["graph_2"], c["graph_3"], c["graph_4"], c["graph_5"], c["graph_6"]]


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, int(value)))


class GraphPage(QWidget):
    DATA_TYPES = [
        ("adc", "ADC Raw"),
        ("adc_filtered", "ADC Filtered"),
        ("distance", "Distance (0.01 mm)"),
        ("normalized", "Normalized (%)"),
    ]

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
        except Exception:
            pass
        self.graph_data = {i: [] for i in range(6)}
        self._build_ui()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        self.apply_theme()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Live Graph",
            "Continuous plotting for ADC, distance, and normalized travel. "
            "Polling only runs while the page is active and live capture is enabled.",
        )
        root.addWidget(scaffold, 1)

        # Two-column: controls on left, chart on right
        columns = QHBoxLayout()
        columns.setSpacing(14)
        scaffold.content_layout.addLayout(columns)

        left = QVBoxLayout()
        left.setSpacing(14)
        left.addWidget(self._build_controls_card())
        left.addWidget(self._build_channels_card())
        left.addStretch(1)
        columns.addLayout(left, 0)

        right = QVBoxLayout()
        right.setSpacing(14)
        right.addWidget(self._build_chart_card(), 1)
        right.addWidget(self._build_stats_card())
        columns.addLayout(right, 1)

        self.status_chip = StatusChip("Graph page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

        self._set_default_y_range()

    def _build_controls_card(self) -> SectionCard:
        card = SectionCard("Capture Controls")

        live_row = QHBoxLayout()
        live_row.setSpacing(8)
        live_lbl = QLabel("Global live")
        live_lbl.setObjectName("Muted")
        live_row.addWidget(live_lbl)
        self.live_info = StatusChip("OFF", "neutral")
        live_row.addWidget(self.live_info)
        live_row.addStretch(1)
        card.body_layout.addLayout(live_row)

        dtype_row = QHBoxLayout()
        dtype_lbl = QLabel("Data type")
        dtype_lbl.setObjectName("Muted")
        dtype_row.addWidget(dtype_lbl)
        self.dtype_combo = QComboBox()
        for value, label in self.DATA_TYPES:
            self.dtype_combo.addItem(label, value)
        self.dtype_combo.currentIndexChanged.connect(self.on_graph_dtype_change)
        dtype_row.addWidget(self.dtype_combo, 1)
        card.body_layout.addLayout(dtype_row)

        points_row = QHBoxLayout()
        pts_lbl = QLabel("Points")
        pts_lbl.setObjectName("Muted")
        points_row.addWidget(pts_lbl)
        self.points_spin = QSpinBox()
        self.points_spin.setRange(50, 1500)
        self.points_spin.setValue(240)
        self.points_spin.valueChanged.connect(self._trim_buffers)
        points_row.addWidget(self.points_spin)
        card.body_layout.addLayout(points_row)

        y_row = QHBoxLayout()
        ymin_lbl = QLabel("Y min")
        ymin_lbl.setObjectName("Muted")
        y_row.addWidget(ymin_lbl)
        self.ymin_spin = QSpinBox()
        self.ymin_spin.setRange(-1000, 5000)
        self.ymin_spin.setValue(2000)
        y_row.addWidget(self.ymin_spin)
        ymax_lbl = QLabel("Y max")
        ymax_lbl.setObjectName("Muted")
        y_row.addWidget(ymax_lbl)
        self.ymax_spin = QSpinBox()
        self.ymax_spin.setRange(-1000, 5000)
        self.ymax_spin.setValue(2700)
        y_row.addWidget(self.ymax_spin)
        card.body_layout.addLayout(y_row)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Auto Y", self.auto_y_range))
        actions.addWidget(make_secondary_button("Reset View", self.reset_graph_view))
        actions.addWidget(make_secondary_button("Clear Data", self.clear_graph_data))
        card.body_layout.addLayout(actions)

        return card

    def _build_channels_card(self) -> SectionCard:
        card = SectionCard("Channels")

        self.channel_checks: list[QCheckBox] = []
        for i, color in enumerate(_channel_colors()):
            row = QHBoxLayout()
            check = QCheckBox(f"Key {i + 1}")
            check.setChecked(True)
            check.toggled.connect(self._refresh_series_visibility)
            dot = QLabel("●")
            dot.setStyleSheet(f"color: {color}; font-size: 18px;")
            row.addWidget(check)
            row.addStretch(1)
            row.addWidget(dot)
            card.body_layout.addLayout(row)
            self.channel_checks.append(check)

        return card

    def _build_chart_card(self) -> SectionCard:
        card = SectionCard("Chart")

        self.chart = QChart()
        self.chart.legend().hide()
        self.chart.setBackgroundVisible(False)

        self.chart_view = QChartView(self.chart)
        self.chart_view.setRenderHint(QPainter.Antialiasing)
        self.chart_view.setRubberBand(QChartView.RubberBand.RectangleRubberBand)
        self.chart_view.setMinimumHeight(300)
        self.chart_view.setBackgroundBrush(QBrush(Qt.GlobalColor.transparent))
        self.chart_view.viewport().setAutoFillBackground(False)

        self.series: list[QLineSeries] = []
        for color in _channel_colors():
            series = QLineSeries()
            series.setPen(QPen(QColor(color), 2))
            self.chart.addSeries(series)
            self.series.append(series)

        self.axis_x = QValueAxis()
        self.axis_x.setTitleText("Samples")
        self.axis_x.setLabelFormat("%d")
        self.axis_y = QValueAxis()

        self.chart.addAxis(self.axis_x, Qt.AlignBottom)
        self.chart.addAxis(self.axis_y, Qt.AlignLeft)
        for s in self.series:
            s.attachAxis(self.axis_x)
            s.attachAxis(self.axis_y)

        card.body_layout.addWidget(self.chart_view, 1)
        return card

    def _build_stats_card(self) -> SectionCard:
        card = SectionCard("Statistics")

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)
        card.body_layout.addLayout(grid)

        self.stat_labels: list[QLabel] = []
        for i, color in enumerate(_channel_colors()):
            lbl = QLabel(f"Key {i + 1}: --")
            lbl.setStyleSheet(f"color: {color}; font-weight: 600;")
            grid.addWidget(lbl, i // 3, i % 3)
            self.stat_labels.append(lbl)

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

    def _set_default_y_range(self) -> None:
        dtype = self.dtype_combo.currentData()
        if dtype in ("adc", "adc_filtered"):
            self.ymin_spin.setValue(2000)
            self.ymax_spin.setValue(2700)
        elif dtype == "distance":
            self.ymin_spin.setValue(0)
            self.ymax_spin.setValue(400)
        else:
            self.ymin_spin.setValue(0)
            self.ymax_spin.setValue(100)
        self._apply_axis_ranges()

    def _apply_axis_ranges(self) -> None:
        ymin = int(self.ymin_spin.value())
        ymax = int(self.ymax_spin.value())
        if ymax <= ymin:
            ymax = ymin + 1
            self.ymax_spin.setValue(ymax)
        max_len = max((len(v) for v in self.graph_data.values()), default=1)
        self.axis_x.setRange(0, max(1, max_len - 1))
        self.axis_y.setRange(ymin, ymax)

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.live_info.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
        else:
            self.live_info.set_text_and_level("OFF", "neutral")

    def _trim_buffers(self) -> None:
        limit = int(self.points_spin.value())
        for i in range(6):
            self.graph_data[i] = self.graph_data[i][-limit:]
        self._refresh_chart()

    def _refresh_series_visibility(self) -> None:
        for i, series in enumerate(self.series):
            series.setVisible(self.channel_checks[i].isChecked())

    def _collect_samples(self) -> list[int]:
        dtype = self.dtype_combo.currentData()
        if dtype in ("adc", "adc_filtered"):
            payload = self.device.get_adc_values() or {}
            if dtype == "adc_filtered":
                values = payload.get("adc_filtered") or payload.get("adc") or []
            else:
                values = payload.get("adc_raw") or payload.get("adc") or []
            return [int(v) for v in list(values)[:6]]
        key_states = self.device.get_key_states() or {}
        if dtype == "distance":
            return [int(v) for v in list(key_states.get("distances_01mm") or [])[:6]]
        return [
            max(0, min(255, int(v))) * 100 // 255
            for v in list(key_states.get("distances") or [])[:6]
        ]

    # ------------------------------------------------------------------
    # Live update
    # ------------------------------------------------------------------

    def _update_graph(self) -> None:
        try:
            samples = self._collect_samples()
        except Exception as exc:
            self._update_status(f"Graph update failed: {exc}", "error")
            return
        limit = int(self.points_spin.value())
        for i in range(6):
            value = samples[i] if i < len(samples) else 0
            self.graph_data[i].append(value)
            self.graph_data[i] = self.graph_data[i][-limit:]
        self._refresh_chart()

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._update_graph()

    def _refresh_chart(self) -> None:
        max_len = 1
        for i, series in enumerate(self.series):
            points = [QPointF(j, v) for j, v in enumerate(self.graph_data[i])]
            series.replace(points)
            max_len = max(max_len, len(points))
            values = self.graph_data[i]
            if values:
                self.stat_labels[i].setText(
                    f"Key {i + 1}: {values[-1]}  (min {min(values)} / max {max(values)})"
                )
            else:
                self.stat_labels[i].setText(f"Key {i + 1}: --")
        self.axis_x.setRange(0, max(1, max_len - 1))
        self._apply_axis_ranges()
        self._refresh_series_visibility()

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def reload(self) -> None:
        self._refresh_chart()

    def on_graph_dtype_change(self) -> None:
        self.clear_graph_data(announce=False)
        self._set_default_y_range()
        self._update_status(f"Graph set to {self.dtype_combo.currentText()}.", "success")

    def auto_y_range(self) -> None:
        values = [v for channel in self.graph_data.values() for v in channel]
        if not values:
            self._update_status("No samples available for auto-range yet.", "warning")
            return
        minimum = min(values)
        maximum = max(values)
        margin = max(1, int((maximum - minimum or 1) * 0.1))
        self.ymin_spin.setValue(minimum - margin)
        self.ymax_spin.setValue(maximum + margin)
        self._apply_axis_ranges()
        self._update_status("Y range auto-adjusted.", "success")

    def reset_graph_view(self) -> None:
        self._set_default_y_range()
        self._refresh_chart()
        self._update_status("Graph view reset.", "success")

    def clear_graph_data(self, announce: bool = True) -> None:
        for i in range(6):
            self.graph_data[i].clear()
        self._refresh_chart()
        if announce:
            self._update_status("Graph buffers cleared.", "success")

    def apply_theme(self) -> None:
        c = current_colors()
        colors = [c["graph_1"], c["graph_2"], c["graph_3"], c["graph_4"], c["graph_5"], c["graph_6"]]
        for series, color in zip(self.series, colors):
            series.setPen(QPen(QColor(color), 2))
        text_brush = QBrush(QColor(c["text_muted"]))
        grid_pen = QPen(QColor(c["border"]), 1)
        line_pen = QPen(QColor(c["border"]), 1)
        for axis in (self.axis_x, self.axis_y):
            axis.setLabelsBrush(text_brush)
            axis.setTitleBrush(text_brush)
            axis.setGridLinePen(grid_pen)
            axis.setLinePen(line_pen)

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_page_activated(self) -> None:
        self._page_active = True
        if self.session.live_enabled:
            self._update_graph()

    def on_page_deactivated(self) -> None:
        self._page_active = False
