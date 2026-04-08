from __future__ import annotations

from PySide6.QtCharts import QChart, QChartView, QLineSeries, QValueAxis
from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QComboBox,
    QDoubleSpinBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
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
    make_secondary_button,
)


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, int(value)))


class GraphPage(QWidget):
    DATA_TYPES = [
        ("adc", "ADC Raw"),
        ("distance", "Distance (mm)"),
        ("normalized", "Normalized (%)"),
    ]

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        self._history: list[float] = []
        self._snapshot_values: list[float] = [0.0] * KEY_COUNT
        self._snapshot_label = "ADC Raw"
        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
            self.session.selectedKeyChanged.connect(self._on_selected_key_changed)
        except Exception:
            pass
        self._build_ui()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        self._on_selected_key_changed(self.session.selected_key)
        self.apply_theme()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Live Graph",
            "Historique de la touche focalisée et snapshot 82 touches du frame courant. Le graphe ne ment plus sur un faux mode 6 canaux.",
        )
        root.addWidget(scaffold, 1)

        columns = QHBoxLayout()
        columns.setSpacing(14)
        columns.setAlignment(Qt.AlignTop)
        scaffold.content_layout.addLayout(columns)

        left = QVBoxLayout()
        left.setSpacing(14)
        right = QVBoxLayout()
        right.setSpacing(14)
        columns.addLayout(left, 0)
        columns.addLayout(right, 1)

        left.addWidget(self._build_controls_card())
        left.addWidget(self._build_stats_card())
        left.addStretch(1)

        right.addWidget(self._build_chart_card(), 1)
        right.addWidget(self._build_snapshot_card())

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

        focus_row = QHBoxLayout()
        focus_row.setSpacing(8)
        focus_lbl = QLabel("Focused key")
        focus_lbl.setObjectName("Muted")
        focus_row.addWidget(focus_lbl)
        self.focus_chip = StatusChip(key_display_name(0), "info")
        focus_row.addWidget(self.focus_chip)
        focus_row.addStretch(1)
        card.body_layout.addLayout(focus_row)

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
        points_lbl = QLabel("History points")
        points_lbl.setObjectName("Muted")
        points_row.addWidget(points_lbl)
        self.points_spin = QSpinBox()
        self.points_spin.setRange(50, 2000)
        self.points_spin.setValue(300)
        self.points_spin.valueChanged.connect(self._trim_history)
        points_row.addWidget(self.points_spin)
        card.body_layout.addLayout(points_row)

        y_row = QHBoxLayout()
        ymin_lbl = QLabel("Y min")
        ymin_lbl.setObjectName("Muted")
        y_row.addWidget(ymin_lbl)
        self.ymin_spin = QDoubleSpinBox()
        self.ymin_spin.setRange(-1000, 5000)
        y_row.addWidget(self.ymin_spin)
        ymax_lbl = QLabel("Y max")
        ymax_lbl.setObjectName("Muted")
        y_row.addWidget(ymax_lbl)
        self.ymax_spin = QDoubleSpinBox()
        self.ymax_spin.setRange(-1000, 5000)
        y_row.addWidget(self.ymax_spin)
        card.body_layout.addLayout(y_row)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Auto Y", self.auto_y_range))
        actions.addWidget(make_secondary_button("Reset View", self.reset_graph_view))
        actions.addWidget(make_secondary_button("Clear Data", self.clear_graph_data))
        card.body_layout.addLayout(actions)
        return card

    def _build_chart_card(self) -> SectionCard:
        card = SectionCard("Focused Key History")

        self.chart = QChart()
        self.chart.legend().hide()
        self.chart.setBackgroundVisible(False)

        self.series = QLineSeries()
        self.chart.addSeries(self.series)

        self.axis_x = QValueAxis()
        self.axis_x.setTitleText("Samples")
        self.axis_x.setLabelFormat("%d")
        self.axis_y = QValueAxis()

        self.chart.addAxis(self.axis_x, Qt.AlignBottom)
        self.chart.addAxis(self.axis_y, Qt.AlignLeft)
        self.series.attachAxis(self.axis_x)
        self.series.attachAxis(self.axis_y)

        self.chart_view = QChartView(self.chart)
        self.chart_view.setRenderHint(QPainter.Antialiasing)
        self.chart_view.setRubberBand(QChartView.RubberBand.RectangleRubberBand)
        self.chart_view.setMinimumHeight(340)
        self.chart_view.setBackgroundBrush(Qt.GlobalColor.transparent)
        self.chart_view.viewport().setAutoFillBackground(False)
        card.body_layout.addWidget(self.chart_view)
        return card

    def _build_snapshot_card(self) -> SectionCard:
        card = SectionCard(
            "Keyboard Snapshot",
            "Le layout montre la valeur courante sur les 82 touches pendant que le graphe conserve l’historique de la touche sélectionnée.",
        )
        self.snapshot_layout = KeyboardLayoutWidget(self.session, unit=32)
        card.body_layout.addWidget(self.snapshot_layout)
        self.snapshot_hint = QLabel("Snapshot ADC Raw")
        self.snapshot_hint.setObjectName("Muted")
        card.body_layout.addWidget(self.snapshot_hint)
        return card

    def _build_stats_card(self) -> SectionCard:
        card = SectionCard("Focused Key Stats")
        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)
        card.body_layout.addLayout(grid)
        self.stat_labels: dict[str, QLabel] = {}
        for row, key in enumerate(("current", "min", "max", "avg")):
            label = QLabel(key.upper())
            label.setObjectName("Muted")
            value = QLabel("--")
            grid.addWidget(label, row, 0)
            grid.addWidget(value, row, 1)
            self.stat_labels[key] = value
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

    def _set_default_y_range(self) -> None:
        dtype = self.dtype_combo.currentData()
        if dtype == "distance":
            self.ymin_spin.setDecimals(2)
            self.ymax_spin.setDecimals(2)
            self.ymin_spin.setValue(0.0)
            self.ymax_spin.setValue(4.0)
        elif dtype == "normalized":
            self.ymin_spin.setDecimals(0)
            self.ymax_spin.setDecimals(0)
            self.ymin_spin.setValue(0)
            self.ymax_spin.setValue(100)
        else:
            self.ymin_spin.setDecimals(0)
            self.ymax_spin.setDecimals(0)
            self.ymin_spin.setValue(1800)
            self.ymax_spin.setValue(2600)
        self._apply_axis_ranges()

    def _apply_axis_ranges(self) -> None:
        ymin = float(self.ymin_spin.value())
        ymax = float(self.ymax_spin.value())
        if ymax <= ymin:
            ymax = ymin + 1.0
            self.ymax_spin.setValue(ymax)
        self.axis_x.setRange(0, max(1, len(self._history) - 1))
        self.axis_y.setRange(ymin, ymax)

    def _trim_history(self) -> None:
        limit = int(self.points_spin.value())
        self._history = self._history[-limit:]
        self._refresh_chart()

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.live_info.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
        else:
            self.live_info.set_text_and_level("OFF", "neutral")

    def _on_selected_key_changed(self, key_index: int) -> None:
        self.focus_chip.setText(key_display_name(int(key_index)))
        self._refresh_chart()
        self._refresh_snapshot()

    def _selected_key(self) -> int:
        return _clamp(int(getattr(self.session, "selected_key", 0)), 0, KEY_COUNT - 1)

    def _collect_snapshot(self) -> list[float]:
        dtype = self.dtype_combo.currentData()
        if dtype == "distance":
            data = self.device.get_key_states() or {}
            return [float(v) for v in list(data.get("distances_mm") or [0.0] * KEY_COUNT)[:KEY_COUNT]]
        if dtype == "normalized":
            data = self.device.get_key_states() or {}
            return [float(int(v) * 100 / 255) for v in list(data.get("distances") or [0] * KEY_COUNT)[:KEY_COUNT]]
        values = self.device.get_all_raw_adc_values() or [0] * KEY_COUNT
        return [float(v) for v in values[:KEY_COUNT]]

    def _update_graph(self) -> None:
        try:
            self._snapshot_values = self._collect_snapshot()
        except Exception as exc:
            self._update_status(f"Graph update failed: {exc}", "error")
            return

        key_index = self._selected_key()
        current = self._snapshot_values[key_index] if key_index < len(self._snapshot_values) else 0.0
        self._history.append(float(current))
        self._trim_history()
        self._refresh_snapshot()
        self._refresh_chart()

    def _refresh_chart(self) -> None:
        points = [QPointF(i, value) for i, value in enumerate(self._history)]
        self.series.replace(points)
        self.axis_x.setRange(0, max(1, len(points) - 1))
        self._apply_axis_ranges()

        if self._history:
            current = self._history[-1]
            minimum = min(self._history)
            maximum = max(self._history)
            average = sum(self._history) / len(self._history)
            dtype = self.dtype_combo.currentData()
            self.stat_labels["current"].setText(self._format_value(current, dtype))
            self.stat_labels["min"].setText(self._format_value(minimum, dtype))
            self.stat_labels["max"].setText(self._format_value(maximum, dtype))
            self.stat_labels["avg"].setText(self._format_value(average, dtype))
        else:
            for label in self.stat_labels.values():
                label.setText("--")

    def _refresh_snapshot(self) -> None:
        dtype = self.dtype_combo.currentData()
        self.snapshot_hint.setText(f"Snapshot {self.dtype_combo.currentText()}")
        key_index = self._selected_key()
        if not self._snapshot_values:
            self.snapshot_layout.reset()
            return

        if dtype == "distance":
            max_value = 4.0
        elif dtype == "normalized":
            max_value = 100.0
        else:
            max_value = max(1.0, max(self._snapshot_values))

        for index, value in enumerate(self._snapshot_values[:KEY_COUNT]):
            ratio = max(0.0, min(1.0, float(value) / max_value if max_value else 0.0))
            fill = QColor(current_colors()["surface_muted"])
            accent = QColor(current_colors()["accent_soft"])
            r = int(fill.red() + (accent.red() - fill.red()) * ratio)
            g = int(fill.green() + (accent.green() - fill.green()) * ratio)
            b = int(fill.blue() + (accent.blue() - fill.blue()) * ratio)
            self.snapshot_layout.set_key_state(
                index,
                title=key_short_label(index),
                subtitle="",
                fill=QColor(r, g, b).name(),
                border=current_colors()["accent"] if index == key_index else current_colors()["border"],
                tooltip=f"{key_display_name(index)}\n{self._format_value(value, dtype)}",
            )

    def _format_value(self, value: float, dtype: str) -> str:
        if dtype == "distance":
            return f"{float(value):.2f} mm"
        if dtype == "normalized":
            return f"{int(round(value))}%"
        return str(int(round(value)))

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._update_graph()

    def reload(self) -> None:
        self._refresh_chart()
        self._refresh_snapshot()

    def on_graph_dtype_change(self) -> None:
        self.clear_graph_data(announce=False)
        self._set_default_y_range()
        self._update_status(f"Graph set to {self.dtype_combo.currentText()}.", "success")

    def auto_y_range(self) -> None:
        if not self._history:
            self._update_status("No samples available for auto-range yet.", "warning")
            return
        minimum = min(self._history)
        maximum = max(self._history)
        margin = max(0.5, (maximum - minimum or 1.0) * 0.1)
        self.ymin_spin.setValue(minimum - margin)
        self.ymax_spin.setValue(maximum + margin)
        self._apply_axis_ranges()
        self._update_status("Y range auto-adjusted.", "success")

    def reset_graph_view(self) -> None:
        self._set_default_y_range()
        self._refresh_chart()
        self._update_status("Graph view reset.", "success")

    def clear_graph_data(self, announce: bool = True) -> None:
        self._history.clear()
        self._refresh_chart()
        if announce:
            self._update_status("Graph buffers cleared.", "success")

    def apply_theme(self) -> None:
        colors = current_colors()
        self.series.setPen(QPen(QColor(colors["accent"]), 2))
        text_pen = QColor(colors["text_muted"])
        grid_pen = QPen(QColor(colors["border"]), 1)
        line_pen = QPen(QColor(colors["border"]), 1)
        for axis in (self.axis_x, self.axis_y):
            axis.setLabelsBrush(text_pen)
            axis.setTitleBrush(text_pen)
            axis.setGridLinePen(grid_pen)
            axis.setLinePen(line_pen)

    def on_page_activated(self) -> None:
        self._page_active = True
        if self.session.live_enabled:
            self._update_graph()

    def on_page_deactivated(self) -> None:
        self._page_active = False
