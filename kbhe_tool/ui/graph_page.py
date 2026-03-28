from __future__ import annotations

import math
import time
from typing import Any

from PySide6.QtCore import QPointF, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import (
    QComboBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QSplitter,
    QToolTip,
    QVBoxLayout,
    QWidget,
)


def _clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


def _sample_list(values):
    samples = []
    for value in list(values)[:6]:
        try:
            samples.append(int(value))
        except Exception:
            samples.append(0)
    return samples


class _GraphPlotWidget(QWidget):
    def __init__(self, page: "GraphPage"):
        super().__init__(page)
        self.page = page
        self.setMouseTracking(True)
        self._dragging = False
        self._drag_last_y = 0
        self._hover_pos = None
        self.setMinimumHeight(420)

    def paintEvent(self, _event):
        page = self.page
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.fillRect(self.rect(), QColor("#0f172a"))

        rect = self._plot_rect()
        painter.setPen(QPen(QColor("#243041"), 1))
        painter.drawRoundedRect(rect.adjusted(0, 0, -1, -1), 10, 10)

        ymin, ymax = page.graph_y_bounds()
        y_range = max(1, ymax - ymin)
        visible = [i for i in range(6) if page.graph_channel_checks[i].isChecked()]
        sample_count = page.visible_sample_count()
        if sample_count <= 1:
            painter.setPen(QColor("#94a3b8"))
            painter.drawText(rect, Qt.AlignCenter, "Enable live graph to capture samples.")
            self._draw_axes(painter, rect, ymin, ymax, y_range)
            return

        self._draw_axes(painter, rect, ymin, ymax, y_range)
        self._draw_grid(painter, rect)
        for idx in visible:
            data = page.graph_data[idx]
            if len(data) < 2:
                continue
            points = []
            x_span = max(1, len(data) - 1)
            for sample_index, value in enumerate(data):
                x = rect.left() + (sample_index / x_span) * rect.width()
                y = rect.bottom() - ((value - ymin) / y_range) * rect.height()
                points.append(QPointF(x, _clamp(int(y), int(rect.top()), int(rect.bottom()))))
            painter.setPen(QPen(QColor(page.graph_colors[idx]), 2))
            painter.drawPolyline(QPolygonF(points))

        if self._hover_pos is not None and rect.contains(self._hover_pos):
            self._draw_hover(painter, rect, ymin, ymax, y_range, visible, sample_count)

    def _plot_rect(self) -> QRectF:
        return QRectF(54, 18, max(10, self.width() - 72), max(10, self.height() - 48))

    def _draw_grid(self, painter: QPainter, rect: QRectF):
        painter.setPen(QPen(QColor("#223044"), 1))
        for i in range(1, 10):
            x = rect.left() + rect.width() * i / 10.0
            painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()))
        for i in range(1, 6):
            y = rect.top() + rect.height() * i / 6.0
            painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y))

    def _draw_axes(self, painter: QPainter, rect: QRectF, ymin: int, ymax: int, y_range: int):
        painter.setPen(QColor("#cbd5e1"))
        font = painter.font()
        font.setPointSize(8)
        painter.setFont(font)
        for i in range(7):
            value = ymax - (y_range * i / 6.0)
            y = rect.top() + rect.height() * i / 6.0
            painter.drawText(6, int(y) + 4, f"{int(value)}")
        points = max((len(series) for series in self.page.graph_data.values()), default=0)
        painter.drawText(int(rect.left()), 14, f"{self.page.data_type_label()} | {points} points")

    def _draw_hover(self, painter: QPainter, rect: QRectF, ymin: int, ymax: int, y_range: int, visible: list[int], sample_count: int):
        x = self._hover_pos.x()
        y = self._hover_pos.y()
        painter.setPen(QPen(QColor("#38bdf8"), 1, Qt.DashLine))
        painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()))
        painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y))

        sample_index = int(((x - rect.left()) / max(1.0, rect.width())) * max(0, sample_count - 1))
        sample_index = _clamp(sample_index, 0, max(0, sample_count - 1))
        lines = [f"Sample {sample_index}", f"Y {self.page.y_value_at(y, rect, ymin, ymax):.1f}"]
        for idx in visible:
            data = self.page.graph_data[idx]
            if sample_index < len(data):
                lines.append(f"Key {idx + 1}: {data[sample_index]}")
        QToolTip.showText(self.mapToGlobal(self._hover_pos.toPoint()), "\n".join(lines), self)

    def mouseMoveEvent(self, event):
        pos = event.position()
        self._hover_pos = pos
        if self._dragging:
            dy = int(pos.y() - self._drag_last_y)
            self._drag_last_y = int(pos.y())
            self.page.shift_y_range(dy, max(1, self.height()))
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self._dragging = True
            self._drag_last_y = int(event.position().y())
            self.setCursor(Qt.ClosedHandCursor)

    def mouseReleaseEvent(self, event):
        del event
        self._dragging = False
        self.unsetCursor()

    def leaveEvent(self, event):
        del event
        self._hover_pos = None
        QToolTip.hideText()
        self.update()

    def wheelEvent(self, event):
        delta = event.angleDelta().y()
        if delta == 0:
            return
        factor = 0.9 if delta > 0 else 1.1
        self.page.zoom_y_range(factor)
        self.update()


class GraphPage(QWidget):
    statusChanged = Signal(str)

    def __init__(self, device: Any | None = None, parent: QWidget | None = None):
        super().__init__(parent)
        self.device = device
        self._page_active = False
        self._last_error: str | None = None
        self.graph_data = {i: [] for i in range(6)}
        self.graph_colors = ["#ff5c5c", "#67e480", "#5c8cff", "#ffd166", "#e07cff", "#57d6d9"]
        self.graph_channel_checks: list[Any] = []
        self.graph_stats_labels: list[QLabel] = []

        self.setObjectName("GraphPage")
        self._build_ui()
        self._apply_style()
        self._sync_enabled_state()
        self._init_timer()
        self.reload()

    def create_graph_widgets(self, parent: QWidget | None = None):
        del parent
        return self

    def set_device(self, device: Any | None):
        self.device = device
        self._sync_enabled_state()
        self.reload()

    def reload(self):
        self._update_view_hint()
        self.draw_graph_stats()
        self.graph_plot.update()
        self._refresh_live_state()

    def on_page_activated(self):
        self._page_active = True
        self.reload()
        if self.graph_live_check.isChecked() and self.device is not None:
            self.graph_timer.start(self.graph_update_spin.value())
            self.update_graph()
        self._refresh_live_state()

    def on_page_deactivated(self):
        self._page_active = False
        self.graph_timer.stop()
        QToolTip.hideText()
        self._refresh_live_state()

    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(16, 16, 16, 16)
        root.setSpacing(12)

        header = self._card("Live Graph", "Responsive capture and plotting for ADC, distance, and normalized key channels.")
        hl = header.layout()
        self.graph_status_label = QLabel("Ready.")
        self.graph_status_label.setWordWrap(True)
        self.graph_state_label = QLabel("Live polling is disabled.")
        self.graph_state_label.setProperty("muted", True)
        self.graph_state_label.setWordWrap(True)
        hl.addWidget(self.graph_status_label)
        hl.addWidget(self.graph_state_label)
        header_actions = QHBoxLayout()
        self.reload_button = QPushButton("Reload")
        self.reload_button.clicked.connect(self.reload)
        self.clear_button = QPushButton("Clear Data")
        self.clear_button.clicked.connect(self.clear_graph_data)
        header_actions.addWidget(self.reload_button)
        header_actions.addWidget(self.clear_button)
        header_actions.addStretch(1)
        hl.addLayout(header_actions)
        root.addWidget(header)

        splitter = QSplitter(Qt.Horizontal)
        root.addWidget(splitter, 1)

        left_scroll = QScrollArea()
        left_scroll.setWidgetResizable(True)
        left_scroll.setFrameShape(QFrame.NoFrame)
        splitter.addWidget(left_scroll)

        left = QWidget()
        left_scroll.setWidget(left)
        left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.setSpacing(12)

        capture = self._card("Live Capture", "Timer-driven sampling only runs while the page is active.")
        cl = capture.layout()
        capture_row = QGridLayout()
        self.graph_live_check = QCheckBox("Enable live graph")
        self.graph_live_check.toggled.connect(self.toggle_graph_update)
        self.graph_points_spin = QSpinBox()
        self.graph_points_spin.setRange(50, 5000)
        self.graph_points_spin.setValue(200)
        self.graph_update_spin = QSpinBox()
        self.graph_update_spin.setRange(50, 1000)
        self.graph_update_spin.setValue(100)
        self.graph_update_spin.valueChanged.connect(self._on_update_interval_changed)
        capture_row.addWidget(self.graph_live_check, 0, 0, 1, 2)
        capture_row.addWidget(QLabel("Data points"), 1, 0)
        capture_row.addWidget(self.graph_points_spin, 1, 1)
        capture_row.addWidget(QLabel("Update (ms)"), 2, 0)
        capture_row.addWidget(self.graph_update_spin, 2, 1)
        self.buffer_label = QLabel("0 buffered samples")
        self.buffer_label.setProperty("muted", True)
        capture_row.addWidget(self.buffer_label, 3, 0, 1, 2)
        cl.addLayout(capture_row)
        left_layout.addWidget(capture)

        dtype = self._card("Data Type", "Switch the y-axis units and capture source.")
        dl = dtype.layout()
        self.graph_dtype_combo = QComboBox()
        self.graph_dtype_combo.addItem("ADC Raw (2000-2700)", "adc")
        self.graph_dtype_combo.addItem("Distance (0-4mm)", "distance")
        self.graph_dtype_combo.addItem("Normalized (0-100%)", "normalized")
        self.graph_dtype_combo.currentIndexChanged.connect(self.on_graph_dtype_change)
        dl.addWidget(self.graph_dtype_combo)
        self.dtype_hint_label = QLabel("")
        self.dtype_hint_label.setProperty("muted", True)
        self.dtype_hint_label.setWordWrap(True)
        dl.addWidget(self.dtype_hint_label)
        left_layout.addWidget(dtype)

        channels = self._card("Channels", "Hide any key line you do not need right now.")
        chl = channels.layout()
        ch_grid = QGridLayout()
        for i in range(6):
            box = QFrame()
            box.setProperty("subcard", True)
            box_l = QVBoxLayout(box)
            box_l.setContentsMargins(10, 10, 10, 10)
            box_l.setSpacing(4)
            check = QCheckBox(f"Key {i + 1}")
            check.setChecked(True)
            check.stateChanged.connect(self._on_channel_visibility_changed)
            color = QLabel("●")
            color.setStyleSheet(f"color: {self.graph_colors[i]}; font-size: 16px;")
            box_l.addWidget(check)
            box_l.addWidget(color)
            ch_grid.addWidget(box, i // 3, i % 3)
            self.graph_channel_checks.append(check)
        chl.addLayout(ch_grid)
        left_layout.addWidget(channels)

        view = self._card("View Controls", "Resize the y-range, auto-fit the visible data, or reset the current data type.")
        vl = view.layout()
        view_form = QGridLayout()
        self.graph_ymin_spin = QSpinBox()
        self.graph_ymax_spin = QSpinBox()
        for spin in (self.graph_ymin_spin, self.graph_ymax_spin):
            spin.setRange(-100000, 100000)
        self.graph_ymin_spin.setValue(2000)
        self.graph_ymax_spin.setValue(2700)
        view_form.addWidget(QLabel("Y min"), 0, 0)
        view_form.addWidget(self.graph_ymin_spin, 0, 1)
        view_form.addWidget(QLabel("Y max"), 0, 2)
        view_form.addWidget(self.graph_ymax_spin, 0, 3)
        self.graph_ymin_spin.valueChanged.connect(self._on_y_range_changed)
        self.graph_ymax_spin.valueChanged.connect(self._on_y_range_changed)
        self.auto_y_button = QPushButton("Auto Y")
        self.auto_y_button.clicked.connect(self.auto_y_range)
        self.reset_view_button = QPushButton("Reset View")
        self.reset_view_button.clicked.connect(self.reset_graph_view)
        view_form.addWidget(self.auto_y_button, 1, 0, 1, 2)
        view_form.addWidget(self.reset_view_button, 1, 2, 1, 2)
        vl.addLayout(view_form)
        left_layout.addWidget(view)
        left_layout.addStretch(1)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(12)
        plot_card = self._card("Graph", "Drag to pan vertically. Use the wheel to zoom the y-range.")
        pl = plot_card.layout()
        self.graph_plot = _GraphPlotWidget(self)
        self.graph_plot.setMinimumWidth(520)
        pl.addWidget(self.graph_plot, 1)
        right_layout.addWidget(plot_card, 1)

        stats_card = self._card("Statistics", "Current value and running average for each visible key.")
        sl = stats_card.layout()
        stats_grid = QGridLayout()
        self.graph_stats_labels = []
        for i in range(6):
            lbl = QLabel(f"K{i + 1}: --")
            lbl.setStyleSheet(f"color: {self.graph_colors[i]}; font-family: Consolas, monospace;")
            stats_grid.addWidget(lbl, 0, i)
            self.graph_stats_labels.append(lbl)
        sl.addLayout(stats_grid)
        right_layout.addWidget(stats_card)

        splitter.addWidget(right)
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([340, 980])

    def _apply_style(self):
        self.setStyleSheet(
            """
            QWidget#GraphPage { background: #f4f7fb; }
            QFrame[card="true"] { background: white; border: 1px solid #d8e0ea; border-radius: 16px; }
            QFrame[subcard="true"] { background: #fbfcfe; border: 1px solid #e3e9f2; border-radius: 12px; }
            QLabel[muted="true"] { color: #667085; }
            QPushButton { padding: 6px 12px; }
            QSpinBox, QComboBox { min-height: 28px; }
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

    def _init_timer(self):
        self.graph_timer = QTimer(self)
        self.graph_timer.timeout.connect(self.update_graph)

    def _set_status(self, message: str, kind: str = "info"):
        colors = {"info": "#175cd3", "ok": "#067647", "warn": "#b54708", "error": "#b42318"}
        self.graph_status_label.setText(message)
        self.graph_status_label.setStyleSheet(f"color: {colors.get(kind, '#175cd3')}; font-weight: 600;")
        self.statusChanged.emit(message)

    def _refresh_live_state(self):
        if self.graph_live_check.isChecked() and self._page_active and self.device is not None:
            self.graph_state_label.setText(f"Live polling is running every {self.graph_update_spin.value()} ms.")
        elif self.graph_live_check.isChecked():
            self.graph_state_label.setText("Live polling is armed and will start when this page becomes active.")
        else:
            self.graph_state_label.setText("Live polling is disabled.")

    def _sync_enabled_state(self):
        has_device = self.device is not None
        for widget in [
            self.reload_button,
            self.clear_button,
            self.graph_live_check,
            self.graph_points_spin,
            self.graph_update_spin,
            self.graph_dtype_combo,
            self.auto_y_button,
            self.reset_view_button,
            self.graph_ymin_spin,
            self.graph_ymax_spin,
        ]:
            widget.setEnabled(has_device)
        for check in self.graph_channel_checks:
            check.setEnabled(has_device)
        self._refresh_live_state()

    def _on_update_interval_changed(self, _value: int):
        if self._page_active and self.graph_live_check.isChecked() and self.device is not None:
            self.graph_timer.start(self.graph_update_spin.value())
        self._refresh_live_state()

    def toggle_graph_update(self, _checked: bool):
        if self.graph_live_check.isChecked() and self._page_active and self.device is not None:
            self.graph_timer.start(self.graph_update_spin.value())
            self.update_graph()
        else:
            self.graph_timer.stop()
        self._refresh_live_state()

    def data_type(self) -> str:
        return self.graph_dtype_combo.currentData() or "adc"

    def data_type_label(self) -> str:
        return self.graph_dtype_combo.currentText()

    def _set_dtype_defaults(self, clear: bool = True):
        dtype = self.data_type()
        if dtype == "adc":
            self.graph_ymin_spin.setValue(2000)
            self.graph_ymax_spin.setValue(2700)
            self.dtype_hint_label.setText("ADC values are plotted in raw counts with the 2000-2700 window.")
        elif dtype == "distance":
            self.graph_ymin_spin.setValue(0)
            self.graph_ymax_spin.setValue(400)
            self.dtype_hint_label.setText("Distance uses the 0.01 mm values returned by the firmware.")
        else:
            self.graph_ymin_spin.setValue(0)
            self.graph_ymax_spin.setValue(100)
            self.dtype_hint_label.setText("Normalized values are scaled to 0-100% from key distance readings.")
        if clear:
            self.clear_graph_data(redraw=False, announce=False)

    def on_graph_dtype_change(self, _index: int):
        self._set_dtype_defaults(clear=True)
        self.graph_plot.update()
        self.draw_graph_stats()
        self._set_status(f"Graph view set to {self.data_type_label()}.", "ok")

    def _on_y_range_changed(self, _value: int):
        self.graph_plot.update()

    def _on_channel_visibility_changed(self, _state: int):
        self.graph_plot.update()
        self.draw_graph_stats()

    def graph_y_bounds(self):
        ymin = self.graph_ymin_spin.value()
        ymax = self.graph_ymax_spin.value()
        if ymax <= ymin:
            ymax = ymin + 1
            self.graph_ymax_spin.blockSignals(True)
            self.graph_ymax_spin.setValue(ymax)
            self.graph_ymax_spin.blockSignals(False)
        return ymin, ymax

    def visible_sample_count(self):
        lengths = [len(self.graph_data[i]) for i in range(6) if self.graph_channel_checks[i].isChecked()]
        return max(lengths, default=0)

    def _collect_samples(self, dtype: str):
        if self.device is None:
            return []
        if dtype == "adc":
            data = self.device.get_adc_values() or {}
            return _sample_list(data.get("adc") or [])
        key_states = self.device.get_key_states() or {}
        if dtype == "distance":
            return _sample_list(key_states.get("distances_01mm") or [])
        return [max(0, min(255, value)) * 100 // 255 for value in _sample_list(key_states.get("distances") or [])]

    def _append_samples(self, samples):
        limit = max(1, self.graph_points_spin.value())
        for i, value in enumerate(samples):
            if i not in self.graph_data:
                break
            self.graph_data[i].append(value)
            if len(self.graph_data[i]) > limit:
                self.graph_data[i] = self.graph_data[i][-limit:]
        self.buffer_label.setText(f"{self.visible_sample_count()} buffered samples")

    def update_graph(self):
        if self.device is None:
            self._set_status("Connect a device to start graph capture.", "warn")
            self.graph_timer.stop()
            return
        try:
            samples = self._collect_samples(self.data_type())
            if samples:
                self._append_samples(samples)
            self.draw_graph_stats()
            self.graph_plot.update()
            self._last_error = None
        except Exception as exc:
            message = f"Graph update error: {exc}"
            if message != self._last_error:
                self._last_error = message
                self._set_status(message, "error")

    def draw_graph_stats(self):
        dtype = self.data_type()
        suffix = {"adc": "", "distance": " 0.01mm", "normalized": "%"}[dtype]
        for i, lbl in enumerate(self.graph_stats_labels):
            data = self.graph_data[i]
            if data and self.graph_channel_checks[i].isChecked():
                current = data[-1]
                avg = sum(data) / len(data)
                lbl.setText(f"K{i + 1}: {current}{suffix} (avg {avg:.0f}{suffix})")
            else:
                lbl.setText(f"K{i + 1}: --")

    def clear_graph_data(self, redraw: bool = True, announce: bool = True):
        self.graph_data = {i: [] for i in range(6)}
        self.buffer_label.setText("0 buffered samples")
        self.draw_graph_stats()
        if redraw:
            self.graph_plot.update()
        if announce:
            self._set_status("Graph data cleared.", "ok")

    def auto_y_range(self):
        values = [value for series in self.graph_data.values() for value in series]
        if not values:
            self._set_status("No graph data available yet for auto-ranging.", "warn")
            return
        low = min(values)
        high = max(values)
        margin = max(1, int(max(1, high - low) * 0.1))
        self.graph_ymin_spin.setValue(int(low - margin))
        self.graph_ymax_spin.setValue(int(high + margin))
        self.graph_plot.update()

    def reset_graph_view(self):
        self._set_dtype_defaults(clear=True)
        self.graph_plot.update()
        self.draw_graph_stats()

    def shift_y_range(self, delta_pixels: int, height: int):
        ymin, ymax = self.graph_y_bounds()
        y_range = max(1, ymax - ymin)
        delta = int(-delta_pixels / max(1, height) * y_range)
        self.graph_ymin_spin.setValue(ymin + delta)
        self.graph_ymax_spin.setValue(ymax + delta)
        self.graph_plot.update()

    def zoom_y_range(self, factor: float):
        ymin, ymax = self.graph_y_bounds()
        center = (ymin + ymax) / 2.0
        y_range = max(1.0, ymax - ymin)
        new_range = max(1, int(y_range * factor))
        self.graph_ymin_spin.setValue(int(center - new_range / 2.0))
        self.graph_ymax_spin.setValue(int(center + new_range / 2.0))
        self.graph_plot.update()

    def y_value_at(self, y: float, rect: QRectF, ymin: int, ymax: int) -> float:
        return ymax - ((y - rect.top()) / max(1.0, rect.height())) * (ymax - ymin)

    def _update_view_hint(self):
        dtype = self.data_type()
        if dtype == "adc":
            self.dtype_hint_label.setText("ADC values are plotted in raw counts with the 2000-2700 window.")
        elif dtype == "distance":
            self.dtype_hint_label.setText("Distance uses the 0.01 mm values returned by the firmware.")
        else:
            self.dtype_hint_label.setText("Normalized values are scaled to 0-100% from key distance readings.")


GraphPageMixin = GraphPage
