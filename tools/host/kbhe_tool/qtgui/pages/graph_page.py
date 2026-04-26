from __future__ import annotations

from PySide6.QtCharts import QChart, QChartView, QLineSeries, QValueAxis
from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox,
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
        ("adc_raw", "ADC Raw"),
        ("adc_filtered", "ADC Filtered"),
        ("adc_calibrated", "ADC Calibrated"),
        ("distance", "Distance (mm)"),
        ("normalized", "Normalized (%)"),
    ]

    PEN_STYLES = {
        "adc_raw": Qt.SolidLine,
        "adc_filtered": Qt.DashLine,
        "adc_calibrated": Qt.DotLine,
        "distance": Qt.DashDotLine,
        "normalized": Qt.DashDotDotLine,
    }

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        self._selected_keys: list[int] = [
            _clamp(int(getattr(self.session, "selected_key", 0)), 0, KEY_COUNT - 1)
        ]
        self._enabled_dtypes: list[str] = ["adc_raw", "adc_filtered"]
        self._history_by_type: dict[str, list[list[float]]] = {
            dtype: [] for dtype, _ in self.DATA_TYPES
        }
        self._snapshot_by_type: dict[str, list[float]] = {
            dtype: [0.0] * KEY_COUNT for dtype, _ in self.DATA_TYPES
        }
        self._series_map: dict[tuple[str, int], QLineSeries] = {}

        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
            self.session.selectedKeyChanged.connect(self._on_selected_key_changed)
        except Exception:
            pass

        self._build_ui()
        self._on_live_settings_changed(
            self.session.live_enabled, self.session.live_interval_ms
        )
        self._on_selected_key_changed(self.session.selected_key)
        self._refresh_selection_summary()
        self._set_default_y_range()
        self.apply_theme()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Live Graph",
            "Historique multi-touch et multi-signal. "
            "Tu peux superposer plusieurs touches et plusieurs types de données.",
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
        traced_lbl = QLabel("Traced")
        traced_lbl.setObjectName("Muted")
        focus_row.addWidget(traced_lbl)
        self.traced_chip = StatusChip("1 key", "neutral")
        focus_row.addWidget(self.traced_chip)
        dtype_lbl = QLabel("Types")
        dtype_lbl.setObjectName("Muted")
        focus_row.addWidget(dtype_lbl)
        self.dtype_chip = StatusChip("2", "neutral")
        focus_row.addWidget(self.dtype_chip)
        focus_row.addStretch(1)
        card.body_layout.addLayout(focus_row)

        actions_row = QHBoxLayout()
        actions_row.setSpacing(8)
        actions_row.addWidget(
            make_secondary_button("Add Focused Key", self.add_focused_key)
        )
        actions_row.addWidget(make_secondary_button("Select All", self.select_all_keys))
        actions_row.addWidget(
            make_secondary_button("Clear Selection", self.clear_trace_selection)
        )
        card.body_layout.addLayout(actions_row)

        self.selection_label = QLabel()
        self.selection_label.setObjectName("Muted")
        self.selection_label.setWordWrap(True)
        card.body_layout.addWidget(self.selection_label)

        types_card = SectionCard(
            "Plotted Data Types",
            "Les séries sélectionnées ici sont superposées sur le graphe. "
            "Le sélecteur ci-dessous choisit quelle donnée pilote le heatmap/snapshot et les stats détaillées.",
        )
        types_grid = QGridLayout()
        types_grid.setHorizontalSpacing(10)
        types_grid.setVerticalSpacing(6)
        self.dtype_checks: dict[str, QCheckBox] = {}
        for order, (dtype, label) in enumerate(self.DATA_TYPES):
            check = QCheckBox(label)
            check.setChecked(dtype in self._enabled_dtypes)
            check.toggled.connect(self._on_dtype_check_changed)
            self.dtype_checks[dtype] = check
            types_grid.addWidget(check, order // 2, order % 2)
        types_card.body_layout.addLayout(types_grid)

        type_actions = QHBoxLayout()
        type_actions.setSpacing(8)
        type_actions.addWidget(make_secondary_button("Raw + Filtered", self.enable_raw_and_filtered))
        type_actions.addWidget(make_secondary_button("Select All Types", self.select_all_dtypes))
        type_actions.addStretch(1)
        types_card.body_layout.addLayout(type_actions)
        card.body_layout.addWidget(types_card)

        source_row = QHBoxLayout()
        source_lbl = QLabel("Snapshot / stats source")
        source_lbl.setObjectName("Muted")
        source_row.addWidget(source_lbl)
        self.dtype_combo = QComboBox()
        for value, label in self.DATA_TYPES:
            self.dtype_combo.addItem(label, value)
        self.dtype_combo.currentIndexChanged.connect(self.on_graph_dtype_change)
        source_row.addWidget(self.dtype_combo, 1)
        card.body_layout.addLayout(source_row)

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

        range_actions = QHBoxLayout()
        range_actions.setSpacing(8)
        range_actions.addWidget(make_secondary_button("Auto Y", self.auto_y_range))
        range_actions.addWidget(make_secondary_button("Reset View", self.reset_graph_view))
        range_actions.addWidget(make_secondary_button("Clear Data", self.clear_graph_data))
        card.body_layout.addLayout(range_actions)
        return card

    def _build_chart_card(self) -> SectionCard:
        card = SectionCard("Selected Key Histories")

        self.chart = QChart()
        self.chart.legend().setVisible(True)
        self.chart.legend().setAlignment(Qt.AlignTop)
        self.chart.setBackgroundVisible(False)

        self.axis_x = QValueAxis()
        self.axis_x.setTitleText("Samples")
        self.axis_x.setLabelFormat("%d")
        self.axis_y = QValueAxis()

        self.chart.addAxis(self.axis_x, Qt.AlignBottom)
        self.chart.addAxis(self.axis_y, Qt.AlignLeft)

        self.chart_view = QChartView(self.chart)
        self.chart_view.setRenderHint(QPainter.Antialiasing)
        self.chart_view.setRubberBand(QChartView.RubberBand.RectangleRubberBand)
        self.chart_view.setMinimumHeight(360)
        self.chart_view.setBackgroundBrush(Qt.GlobalColor.transparent)
        self.chart_view.viewport().setAutoFillBackground(False)
        card.body_layout.addWidget(self.chart_view)
        return card

    def _build_snapshot_card(self) -> SectionCard:
        card = SectionCard(
            "Keyboard Selection",
            "Le layout montre la donnée sélectionnée comme source. "
            "Clique un keycap pour ajouter ou retirer sa trace.",
        )
        self.snapshot_layout = KeyboardLayoutWidget(self.session, unit=32)
        self.snapshot_layout.keyClicked.connect(self._toggle_key_trace)
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
        self.stats_hint = QLabel()
        self.stats_hint.setObjectName("Muted")
        self.stats_hint.setWordWrap(True)
        card.body_layout.addWidget(self.stats_hint)
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
        key_index = _clamp(int(key_index), 0, KEY_COUNT - 1)
        self.focus_chip.setText(key_display_name(key_index))
        if not self._selected_keys:
            self._selected_keys = [key_index]
        self._refresh_selection_summary()
        self._refresh_chart()
        self._refresh_snapshot()

    def _selected_key(self) -> int:
        return _clamp(int(getattr(self.session, "selected_key", 0)), 0, KEY_COUNT - 1)

    def _selected_dtypes(self) -> list[str]:
        selected = [dtype for dtype, check in self.dtype_checks.items() if check.isChecked()]
        if selected:
            return selected
        primary = self.dtype_combo.currentData() or "adc_raw"
        check = self.dtype_checks.get(primary)
        if check is not None:
            check.blockSignals(True)
            check.setChecked(True)
            check.blockSignals(False)
        return [primary]

    def _primary_dtype(self) -> str:
        dtype = self.dtype_combo.currentData() or "adc_raw"
        if dtype not in self._selected_dtypes():
            self.dtype_combo.blockSignals(True)
            idx = self.dtype_combo.findData(self._selected_dtypes()[0])
            if idx >= 0:
                self.dtype_combo.setCurrentIndex(idx)
                dtype = self.dtype_combo.currentData() or self._selected_dtypes()[0]
            self.dtype_combo.blockSignals(False)
        return dtype

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

    def _pen_for(self, key_order: int, dtype: str) -> QPen:
        pen = QPen(QColor(self._palette_for_index(key_order)), 2)
        pen.setStyle(self.PEN_STYLES.get(dtype, Qt.SolidLine))
        return pen

    def _set_default_y_range(self) -> None:
        dtype = self._primary_dtype()
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
        max_len = 0
        for dtype in self._selected_dtypes():
            max_len = max(max_len, len(self._history_by_type.get(dtype, [])))
        self.axis_x.setRange(0, max(1, max_len - 1))
        self.axis_y.setRange(ymin, ymax)

    def _trim_history(self) -> None:
        limit = int(self.points_spin.value())
        for dtype in self._history_by_type:
            self._history_by_type[dtype] = self._history_by_type[dtype][-limit:]
        self._refresh_chart()

    def _refresh_selection_summary(self) -> None:
        if not self._selected_keys:
            self._selected_keys = [self._selected_key()]

        count = len(self._selected_keys)
        self.traced_chip.set_text_and_level(f"{count} key{'s' if count != 1 else ''}", "info")
        labels = [key_display_name(index) for index in self._selected_keys[:6]]
        suffix = "" if count <= 6 else f" … +{count - 6}"
        self.selection_label.setText("Selected traces: " + ", ".join(labels) + suffix)

        dtypes = self._selected_dtypes()
        self.dtype_chip.set_text_and_level(
            f"{len(dtypes)} type{'s' if len(dtypes) != 1 else ''}", "info"
        )

    def add_focused_key(self) -> None:
        key_index = self._selected_key()
        if key_index not in self._selected_keys:
            self._selected_keys.append(key_index)
        self._refresh_selection_summary()
        self._refresh_chart()
        self._refresh_snapshot()

    def clear_trace_selection(self) -> None:
        self._selected_keys = [self._selected_key()]
        self._refresh_selection_summary()
        self._refresh_chart()
        self._refresh_snapshot()

    def select_all_keys(self) -> None:
        self._selected_keys = list(range(KEY_COUNT))
        self._refresh_selection_summary()
        self._refresh_chart()
        self._refresh_snapshot()

    def enable_raw_and_filtered(self) -> None:
        for dtype, check in self.dtype_checks.items():
            check.blockSignals(True)
            check.setChecked(dtype in ("adc_raw", "adc_filtered"))
            check.blockSignals(False)
        self._refresh_selection_summary()
        self.clear_graph_data(announce=False)
        self._set_default_y_range()
        self._update_status("Graph set to Raw + Filtered overlay.", "success")

    def select_all_dtypes(self) -> None:
        for check in self.dtype_checks.values():
            check.blockSignals(True)
            check.setChecked(True)
            check.blockSignals(False)
        self._refresh_selection_summary()
        self.clear_graph_data(announce=False)
        self._update_status("All data types enabled for overlay.", "success")

    def _toggle_key_trace(self, key_index: int) -> None:
        key_index = _clamp(int(key_index), 0, KEY_COUNT - 1)
        if key_index in self._selected_keys:
            if len(self._selected_keys) > 1:
                self._selected_keys.remove(key_index)
        else:
            self._selected_keys.append(key_index)
        self._refresh_selection_summary()
        self._refresh_chart()
        self._refresh_snapshot()

    def _on_dtype_check_changed(self) -> None:
        self._refresh_selection_summary()
        self.clear_graph_data(announce=False)
        self._refresh_snapshot()
        self._refresh_chart()
        selected_labels = [
            label for dtype, label in self.DATA_TYPES if dtype in self._selected_dtypes()
        ]
        self._update_status("Overlay types: " + ", ".join(selected_labels), "success")

    def _collect_snapshot(self, dtype: str) -> list[float]:
        if dtype == "distance":
            data = self.device.get_key_states() or {}
            values = list(data.get("distances_mm") or [0.0] * KEY_COUNT)[:KEY_COUNT]
            return [float(v) for v in values]
        if dtype == "normalized":
            data = self.device.get_key_states() or {}
            values = list(data.get("distances") or [0] * KEY_COUNT)[:KEY_COUNT]
            return [float(int(v) * 100 / 255) for v in values]
        if dtype == "adc_filtered":
            values = self.device.get_all_filtered_adc_values() or [0] * KEY_COUNT
        elif dtype == "adc_calibrated":
            values = self.device.get_all_calibrated_adc_values() or [0] * KEY_COUNT
        else:
            values = self.device.get_all_raw_adc_values() or [0] * KEY_COUNT
        return [float(v) for v in values[:KEY_COUNT]]

    def _update_graph(self) -> None:
        selected_dtypes = self._selected_dtypes()
        try:
            for dtype in selected_dtypes:
                snapshot = self._collect_snapshot(dtype)
                self._snapshot_by_type[dtype] = snapshot
                self._history_by_type[dtype].append(snapshot[:KEY_COUNT])
        except Exception as exc:
            self._update_status(f"Graph update failed: {exc}", "error")
            return

        self._trim_history()
        self._refresh_snapshot()
        self._refresh_chart()

    def _refresh_chart(self) -> None:
        selected_keys = [key for key in self._selected_keys if 0 <= key < KEY_COUNT]
        if not selected_keys:
            selected_keys = [self._selected_key()]
            self._selected_keys = selected_keys[:]

        selected_dtypes = self._selected_dtypes()
        wanted = {(dtype, key) for dtype in selected_dtypes for key in selected_keys}

        for key in list(self._series_map):
            if key in wanted:
                continue
            series = self._series_map.pop(key)
            self.chart.removeSeries(series)

        multi_dtype = len(selected_dtypes) > 1
        for dtype in selected_dtypes:
            history = self._history_by_type.get(dtype, [])
            dtype_label = next((label for value, label in self.DATA_TYPES if value == dtype), dtype)
            for key_order, key_index in enumerate(selected_keys):
                series_key = (dtype, key_index)
                series = self._series_map.get(series_key)
                if series is None:
                    series = QLineSeries()
                    self._series_map[series_key] = series
                    self.chart.addSeries(series)
                    series.attachAxis(self.axis_x)
                    series.attachAxis(self.axis_y)
                series.setName(
                    f"{key_display_name(key_index)} · {dtype_label}"
                    if multi_dtype
                    else key_display_name(key_index)
                )
                series.setPen(self._pen_for(key_order, dtype))
                points = [
                    QPointF(i, float(frame[key_index]) if key_index < len(frame) else 0.0)
                    for i, frame in enumerate(history)
                ]
                series.replace(points)

        self._apply_axis_ranges()

        focused_key = self._selected_key()
        primary_dtype = self._primary_dtype()
        focused_history = [
            float(frame[focused_key]) if focused_key < len(frame) else 0.0
            for frame in self._history_by_type.get(primary_dtype, [])
        ]
        if focused_history:
            current = focused_history[-1]
            minimum = min(focused_history)
            maximum = max(focused_history)
            average = sum(focused_history) / len(focused_history)
            self.stat_labels["current"].setText(self._format_value(current, primary_dtype))
            self.stat_labels["min"].setText(self._format_value(minimum, primary_dtype))
            self.stat_labels["max"].setText(self._format_value(maximum, primary_dtype))
            self.stat_labels["avg"].setText(self._format_value(average, primary_dtype))
            primary_label = next(
                (label for value, label in self.DATA_TYPES if value == primary_dtype),
                primary_dtype,
            )
            self.stats_hint.setText(
                f"{key_display_name(focused_key)} tracked on {primary_label}. "
                f"Visible overlays: {', '.join(label for value, label in self.DATA_TYPES if value in selected_dtypes)}."
            )
        else:
            for label in self.stat_labels.values():
                label.setText("--")
            self.stats_hint.setText("No history captured yet for the focused key.")

    def _refresh_snapshot(self) -> None:
        primary_dtype = self._primary_dtype()
        primary_label = next(
            (label for value, label in self.DATA_TYPES if value == primary_dtype),
            primary_dtype,
        )
        self.snapshot_hint.setText(f"Snapshot {primary_label}")

        values = self._snapshot_by_type.get(primary_dtype, [])
        if not values:
            self.snapshot_layout.reset()
            return

        if primary_dtype == "distance":
            max_value = 4.0
        elif primary_dtype == "normalized":
            max_value = 100.0
        else:
            max_value = max(1.0, max(values))

        focused_key = self._selected_key()
        selected_lookup = {idx: order for order, idx in enumerate(self._selected_keys)}
        colors = current_colors()
        for index, value in enumerate(values[:KEY_COUNT]):
            ratio = max(0.0, min(1.0, float(value) / max_value if max_value else 0.0))
            fill = QColor(colors["surface_muted"])
            accent = QColor(colors["accent_soft"])
            r = int(fill.red() + (accent.red() - fill.red()) * ratio)
            g = int(fill.green() + (accent.green() - fill.green()) * ratio)
            b = int(fill.blue() + (accent.blue() - fill.blue()) * ratio)
            border = colors["accent"] if index == focused_key else colors["border"]
            if index in selected_lookup and index != focused_key:
                border = self._palette_for_index(selected_lookup[index])
            self.snapshot_layout.set_key_state(
                index,
                title=key_short_label(index),
                subtitle="",
                fill=QColor(r, g, b).name(),
                border=border,
                tooltip=f"{key_display_name(index)}\n{self._format_value(value, primary_dtype)}",
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
        self._refresh_selection_summary()
        self._refresh_chart()
        self._refresh_snapshot()

    def on_graph_dtype_change(self) -> None:
        primary_dtype = self.dtype_combo.currentData() or "adc_raw"
        check = self.dtype_checks.get(primary_dtype)
        if check is not None and not check.isChecked():
            check.blockSignals(True)
            check.setChecked(True)
            check.blockSignals(False)
        self._set_default_y_range()
        self._refresh_chart()
        self._refresh_snapshot()
        self._update_status(
            f"Snapshot/stat source set to {self.dtype_combo.currentText()}.",
            "success",
        )

    def auto_y_range(self) -> None:
        values: list[float] = []
        for dtype in self._selected_dtypes():
            for frame in self._history_by_type.get(dtype, []):
                for key in self._selected_keys:
                    if key < len(frame):
                        values.append(float(frame[key]))
        if not values:
            self._update_status("No samples available for auto-range yet.", "warning")
            return
        minimum = min(values)
        maximum = max(values)
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
        for dtype in self._history_by_type:
            self._history_by_type[dtype].clear()
        self._refresh_chart()
        if announce:
            self._update_status("Graph buffers cleared.", "success")

    def apply_theme(self) -> None:
        for (dtype, key_index), series in self._series_map.items():
            try:
                key_order = self._selected_keys.index(key_index)
            except ValueError:
                key_order = 0
            series.setPen(self._pen_for(key_order, dtype))

        colors = current_colors()
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
