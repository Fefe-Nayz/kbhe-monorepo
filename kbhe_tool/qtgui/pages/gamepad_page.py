from __future__ import annotations

from copy import deepcopy
import math

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QSlider,
    QSpinBox,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from kbhe_tool.key_layout import key_display_name
from kbhe_tool.protocol import (
    GAMEPAD_AXES,
    GAMEPAD_BUTTONS,
    GAMEPAD_CURVE_MAX_DISTANCE_MM,
    GAMEPAD_CURVE_POINT_COUNT,
    GAMEPAD_DIRECTIONS,
    GAMEPAD_KEYBOARD_ROUTING,
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


def _clamp(value: float | int, low: float | int, high: float | int):
    return max(low, min(high, value))


def _default_curve_points() -> list[dict[str, float | int]]:
    return [
        {"x_mm": 0.00, "x_01mm": 0, "y": 0},
        {"x_mm": 1.33, "x_01mm": 133, "y": 85},
        {"x_mm": 2.66, "x_01mm": 266, "y": 170},
        {"x_mm": 4.00, "x_01mm": 400, "y": 255},
    ]


def _sanitize_curve_points(points) -> list[dict[str, float | int]]:
    defaults = _default_curve_points()
    sanitized = []
    previous_x = 0

    for index in range(GAMEPAD_CURVE_POINT_COUNT):
        source = points[index] if isinstance(points, (list, tuple)) and index < len(points) else defaults[index]
        if isinstance(source, dict):
            if "x_01mm" in source:
                x_01mm = int(source.get("x_01mm", defaults[index]["x_01mm"]))
            else:
                x_01mm = int(round(float(source.get("x_mm", defaults[index]["x_mm"])) * 100.0))
            y = int(source.get("y", defaults[index]["y"]))
        else:
            x_01mm = defaults[index]["x_01mm"]
            y = defaults[index]["y"]

        x_01mm = int(_clamp(x_01mm, previous_x, int(GAMEPAD_CURVE_MAX_DISTANCE_MM * 100.0)))
        y = int(_clamp(y, 0, 255))
        previous_x = x_01mm
        sanitized.append({"x_01mm": x_01mm, "x_mm": x_01mm / 100.0, "y": y})

    return sanitized


def _curve_eval(points: list[dict[str, float | int]], distance_mm: float) -> int:
    curve = _sanitize_curve_points(points)
    distance_01mm = int(round(_clamp(distance_mm, 0.0, GAMEPAD_CURVE_MAX_DISTANCE_MM) * 100.0))

    if distance_01mm <= int(curve[0]["x_01mm"]):
        return int(curve[0]["y"])

    for index in range(1, len(curve)):
        x0 = int(curve[index - 1]["x_01mm"])
        x1 = int(curve[index]["x_01mm"])
        y0 = int(curve[index - 1]["y"])
        y1 = int(curve[index]["y"])
        if distance_01mm <= x1:
            if x1 <= x0:
                return y1
            t = (distance_01mm - x0) / float(x1 - x0)
            return int(round(y0 + (y1 - y0) * t))

    return int(curve[-1]["y"])


def _curve_floor_output(points: list[dict[str, float | int]]) -> int:
    curve = _sanitize_curve_points(points)
    if not curve:
        return 0
    return int(_clamp(int(curve[0]["y"]), 0, 255))


def _effective_radial_deadzone(radial_deadzone: int, points: list[dict[str, float | int]]) -> int:
    raw = int(_clamp(radial_deadzone, 0, 255))
    return max(raw, _curve_floor_output(points))


def _compose_axis_value(positive: int, negative: int, reactive: bool) -> float:
    pos = int(_clamp(positive, 0, 255))
    neg = int(_clamp(negative, 0, 255))
    if reactive and pos > 0 and neg > 0:
        if pos >= neg:
            neg = 0
        else:
            pos = 0
    return float(pos - neg)


def _shape_stick_pair(x: float, y: float, radial_deadzone: int, square_mode: bool) -> tuple[float, float]:
    if not square_mode:
        magnitude = math.hypot(x, y)
        if magnitude > 255.0 and magnitude > 0.0:
            scale = 255.0 / magnitude
            x *= scale
            y *= scale

    magnitude = math.hypot(x, y)
    deadzone = float(_clamp(radial_deadzone, 0, 255))
    if magnitude <= 0.0 or deadzone >= 255.0 or magnitude <= deadzone:
        return 0.0, 0.0

    if deadzone > 0.0:
        scaled = ((magnitude - deadzone) * 255.0) / (255.0 - deadzone)
        if magnitude > 0.0:
            factor = scaled / magnitude
            x *= factor
            y *= factor

    return float(_clamp(x, -255.0, 255.0)), float(_clamp(y, -255.0, 255.0))


class StickPreview(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(260, 260)
        self.x = 0.0
        self.y = 0.0
        self.square_mode = False
        self.deadzone_raw = 0

    def set_state(self, x: float, y: float, square_mode: bool, deadzone_raw: int) -> None:
        self.x = max(-1.0, min(1.0, float(x)))
        self.y = max(-1.0, min(1.0, float(y)))
        self.square_mode = bool(square_mode)
        self.deadzone_raw = int(_clamp(deadzone_raw, 0, 255))
        self.update()

    def paintEvent(self, event) -> None:
        del event
        c = current_colors()
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.rect(), QColor(c["surface"]))

        margin = 24
        side = min(self.width(), self.height()) - margin * 2
        rect = QRectF((self.width() - side) / 2, (self.height() - side) / 2, side, side)
        center = rect.center()
        radius = side / 2

        painter.setPen(QPen(QColor(c["border"]), 1))
        painter.drawLine(rect.left(), center.y(), rect.right(), center.y())
        painter.drawLine(center.x(), rect.top(), center.x(), rect.bottom())

        painter.setPen(QPen(QColor(c["text_muted"]), 2))
        painter.setBrush(QColor(c["surface_muted"]))
        if self.square_mode:
            painter.drawRoundedRect(rect, 14, 14)
        else:
            painter.drawEllipse(rect)

        deadzone_radius = radius * (self.deadzone_raw / 255.0)
        painter.setPen(QPen(QColor(c["warning"]), 1, Qt.PenStyle.DashLine))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(center, deadzone_radius, deadzone_radius)

        thumb_x = center.x() + self.x * radius
        thumb_y = center.y() - self.y * radius
        painter.setPen(QPen(QColor(c["accent"]), 2))
        painter.setBrush(QColor(c["accent_soft"]))
        painter.drawEllipse(QRectF(thumb_x - 10, thumb_y - 10, 20, 20))

        painter.setPen(QColor(c["text_muted"]))
        painter.drawText(12, 20, "Live left-stick preview")
        painter.drawText(12, self.height() - 12, f"X {self.x:+.2f}   Y {self.y:+.2f}")


class GamepadCurveEditor(QWidget):
    pointsChanged = Signal(list)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(280)
        self._points = _default_curve_points()
        self._active_index = None

    def set_points(self, points) -> None:
        self._points = _sanitize_curve_points(points)
        self.update()

    def points(self) -> list[dict[str, float | int]]:
        return deepcopy(self._points)

    def _graph_rect(self) -> QRectF:
        return QRectF(34, 16, max(10, self.width() - 58), max(10, self.height() - 42))

    def _point_to_scene(self, point: dict[str, float | int]) -> QPointF:
        rect = self._graph_rect()
        x_ratio = float(point["x_mm"]) / GAMEPAD_CURVE_MAX_DISTANCE_MM
        y_ratio = float(point["y"]) / 255.0
        return QPointF(rect.left() + x_ratio * rect.width(), rect.bottom() - y_ratio * rect.height())

    def _scene_to_point(self, pos: QPointF, index: int) -> dict[str, float | int]:
        rect = self._graph_rect()
        left_bound = 0.0 if index == 0 else float(self._points[index - 1]["x_mm"])
        right_bound = GAMEPAD_CURVE_MAX_DISTANCE_MM if index == len(self._points) - 1 else float(self._points[index + 1]["x_mm"])
        x_ratio = (pos.x() - rect.left()) / rect.width()
        y_ratio = (rect.bottom() - pos.y()) / rect.height()
        x_mm = float(_clamp(x_ratio * GAMEPAD_CURVE_MAX_DISTANCE_MM, left_bound, right_bound))
        y = int(round(_clamp(y_ratio * 255.0, 0.0, 255.0)))
        x_01mm = int(round(x_mm * 100.0))
        return {"x_mm": x_01mm / 100.0, "x_01mm": x_01mm, "y": y}

    def _active_point_for_pos(self, pos: QPointF):
        for index, point in enumerate(self._points):
            scene = self._point_to_scene(point)
            if math.hypot(scene.x() - pos.x(), scene.y() - pos.y()) <= 14.0:
                return index
        return None

    def mousePressEvent(self, event) -> None:
        if event.button() != Qt.MouseButton.LeftButton:
            return
        index = self._active_point_for_pos(event.position())
        if index is not None:
            self._active_index = index
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event) -> None:
        if self._active_index is None:
            super().mouseMoveEvent(event)
            return
        self._points[self._active_index] = self._scene_to_point(event.position(), self._active_index)
        self._points = _sanitize_curve_points(self._points)
        self.pointsChanged.emit(self.points())
        self.update()
        event.accept()

    def mouseReleaseEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._active_index = None
        super().mouseReleaseEvent(event)

    def paintEvent(self, event) -> None:
        del event
        c = current_colors()
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.rect(), QColor(c["surface"]))

        rect = self._graph_rect()
        painter.setPen(QPen(QColor(c["border"]), 1))
        painter.setBrush(QColor(c["surface_muted"]))
        painter.drawRoundedRect(rect, 12, 12)

        painter.setPen(QPen(QColor(c["border"]), 1, Qt.PenStyle.DashLine))
        for ratio in (0.25, 0.5, 0.75):
            x = rect.left() + rect.width() * ratio
            y = rect.top() + rect.height() * ratio
            painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()))
            painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y))

        painter.setPen(QColor(c["text_muted"]))
        painter.drawText(8, 20, "0 mm")
        painter.drawText(self.width() - 58, 20, "255")
        painter.drawText(8, self.height() - 10, "0")
        painter.drawText(self.width() - 58, self.height() - 10, "4.00 mm")

        poly_points = [self._point_to_scene(point) for point in self._points]
        painter.setPen(QPen(QColor(c["accent"]), 2))
        for index in range(1, len(poly_points)):
            painter.drawLine(poly_points[index - 1], poly_points[index])

        for index, point in enumerate(self._points):
            scene = self._point_to_scene(point)
            fill = QColor(c["accent_soft"]) if index != self._active_index else QColor(c["warning"])
            painter.setPen(QPen(QColor(c["accent"]), 2))
            painter.setBrush(fill)
            painter.drawEllipse(scene, 6, 6)


class GamepadPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._current_key = int(_clamp(int(getattr(session, "selected_key", 0)), 0, KEY_COUNT - 1))
        self._page_active = False
        self._loading = False
        self._curve_points = _default_curve_points()
        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
        except Exception:
            pass
        try:
            self.session.selectedKeyChanged.connect(self.on_selected_key_changed)
        except Exception:
            pass
        self._build_ui()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        self.reload()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Gamepad",
            "Configure global routing, analog response, and per-key gamepad mappings for the full 82-key runtime.",
        )
        root.addWidget(scaffold, 1)

        scaffold.add_card(self._build_focus_mapping_card())

        lower_row = QHBoxLayout()
        lower_row.setSpacing(14)
        lower_row.setAlignment(Qt.AlignTop)
        lower_row.addWidget(self._build_settings_card(), 1)
        lower_row.addWidget(self._build_preview_card(), 1)
        scaffold.content_layout.addLayout(lower_row)

        scaffold.add_card(self._build_mapping_table_card())

        self.status_chip = StatusChip("Gamepad page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

        self._set_selected_key_label()
        self._update_deadzone_label(self.deadzone_slider.value())
        self._sync_curve_spinboxes()

    def _build_settings_card(self) -> SectionCard:
        card = SectionCard(
            "Global Response",
            "Tune keyboard routing, the shared 4-point analog curve, and the radial stick deadzone floor applied after that curve.",
        )

        focused_row = QHBoxLayout()
        focused_row.setSpacing(8)
        lbl = QLabel("Focused key")
        lbl.setObjectName("Muted")
        focused_row.addWidget(lbl)
        self.focused_key_label = StatusChip(key_display_name(0), "info")
        focused_row.addWidget(self.focused_key_label)
        focused_row.addStretch(1)
        card.body_layout.addLayout(focused_row)

        tuning = SubCard()

        dz_row = QHBoxLayout()
        dz_label = QLabel("Radial deadzone")
        dz_label.setObjectName("Muted")
        dz_row.addWidget(dz_label)
        dz_row.addStretch(1)
        self.deadzone_value_label = QLabel("--")
        self.deadzone_value_label.setObjectName("Muted")
        dz_row.addWidget(self.deadzone_value_label)
        tuning.layout.addLayout(dz_row)

        self.deadzone_slider = QSlider(Qt.Orientation.Horizontal)
        self.deadzone_slider.setRange(0, 255)
        self.deadzone_slider.valueChanged.connect(self._update_deadzone_label)
        tuning.layout.addWidget(self.deadzone_slider)

        self.keep_keyboard_check = QCheckBox("Keep keyboard output in gamepad mode")
        self.replace_mapped_check = QCheckBox("Mapped keys replace keyboard output")
        self.square_check = QCheckBox("Square stick")
        self.reactive_check = QCheckBox("Reactive stick")
        tuning.layout.addWidget(self.keep_keyboard_check)
        tuning.layout.addWidget(self.replace_mapped_check)
        tuning.layout.addWidget(self.square_check)
        tuning.layout.addWidget(self.reactive_check)

        hint = QLabel(
            "The radial deadzone is clamped to the curve start output so both controls stay aligned. "
            "If keyboard output is kept on, you can still suppress it only on mapped keys."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        tuning.layout.addWidget(hint)
        card.body_layout.addWidget(tuning)

        curve_card = SubCard()
        curve_layout = curve_card.layout
        curve_layout.setSpacing(10)

        curve_header = QHBoxLayout()
        curve_header.setSpacing(8)
        curve_title = QLabel("Shared analog curve")
        curve_title.setObjectName("Muted")
        curve_header.addWidget(curve_title)
        curve_header.addStretch(1)
        curve_hint = QLabel("Drag points directly or edit the numbers below.")
        curve_hint.setObjectName("Muted")
        curve_header.addWidget(curve_hint)
        curve_layout.addLayout(curve_header)

        self.curve_editor = GamepadCurveEditor()
        self.curve_editor.pointsChanged.connect(self._on_curve_editor_changed)
        curve_layout.addWidget(self.curve_editor)

        points_grid = QGridLayout()
        points_grid.setHorizontalSpacing(10)
        points_grid.setVerticalSpacing(8)
        self.curve_x_spins = []
        self.curve_y_spins = []

        for index in range(GAMEPAD_CURVE_POINT_COUNT):
            title = QLabel(f"P{index + 1}")
            title.setObjectName("Muted")
            x_spin = QSpinBox()
            x_spin.setRange(0, int(GAMEPAD_CURVE_MAX_DISTANCE_MM * 100.0))
            x_spin.setSuffix(" x0.01mm")
            y_spin = QSpinBox()
            y_spin.setRange(0, 255)
            x_spin.valueChanged.connect(self._on_curve_spin_changed)
            y_spin.valueChanged.connect(self._on_curve_spin_changed)
            self.curve_x_spins.append(x_spin)
            self.curve_y_spins.append(y_spin)
            points_grid.addWidget(title, index, 0)
            points_grid.addWidget(x_spin, index, 1)
            points_grid.addWidget(y_spin, index, 2)

        curve_layout.addLayout(points_grid)
        card.body_layout.addWidget(curve_card)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Reload Global", self.load_global_settings))
        actions.addWidget(make_primary_button("Apply Global Settings", self.apply_gamepad_settings))
        actions.addStretch(1)
        card.body_layout.addLayout(actions)
        card.body_layout.addStretch(1)
        return card

    def _build_preview_card(self) -> SectionCard:
        card = SectionCard(
            "Live Preview",
            "Uses the current mappings, shared curve, and routing controls to preview the left stick while this page is active.",
        )

        preview_header = QHBoxLayout()
        preview_header.setSpacing(8)
        live_lbl = QLabel("Global live")
        live_lbl.setObjectName("Muted")
        preview_header.addWidget(live_lbl)
        self.preview_live_info = StatusChip("OFF", "neutral")
        preview_header.addWidget(self.preview_live_info)
        preview_header.addStretch(1)
        card.body_layout.addLayout(preview_header)

        self.preview_widget = StickPreview()
        card.body_layout.addWidget(self.preview_widget)

        meters_card = SubCard()
        meters = QGridLayout()
        meters.setHorizontalSpacing(8)
        meters.setVerticalSpacing(8)
        meters_card.layout.addLayout(meters)

        self.focus_axis_labels: dict[str, QLabel] = {}
        for row, key in enumerate(("normalized", "distance", "mapping", "vector")):
            label = QLabel(
                {
                    "normalized": "Focused key travel",
                    "distance": "Focused key distance",
                    "mapping": "Focused mapping",
                    "vector": "Live left stick",
                }[key]
            )
            label.setObjectName("Muted")
            value = QLabel("--")
            meters.addWidget(label, row, 0)
            meters.addWidget(value, row, 1)
            self.focus_axis_labels[key] = value
        card.body_layout.addWidget(meters_card)
        return card

    def _build_focus_mapping_card(self) -> SectionCard:
        card = SectionCard(
            "Focused Key Mapping",
            "The 82-key layout is the primary selector. Click a keycap to edit the focused key directly.",
        )

        top = QHBoxLayout()
        top.setSpacing(8)
        self.focus_mapping_chip = StatusChip(key_display_name(self._current_key), "info")
        self.focus_mapping_summary = StatusChip("Unassigned", "neutral")
        top.addWidget(self.focus_mapping_chip)
        top.addWidget(self.focus_mapping_summary)
        top.addStretch(1)
        card.body_layout.addLayout(top)

        body = QHBoxLayout()
        body.setSpacing(18)
        body.setAlignment(Qt.AlignTop)
        card.body_layout.addLayout(body)

        left = QVBoxLayout()
        left.setSpacing(10)
        body.addLayout(left, 2)
        self.preview_layout = KeyboardLayoutWidget(self.session, unit=34)
        left.addWidget(self.preview_layout, 0, Qt.AlignTop | Qt.AlignLeft)

        layout_hint = QLabel(
            "The keycap tint reflects live travel. The current border follows the focused key."
        )
        layout_hint.setObjectName("Muted")
        layout_hint.setWordWrap(True)
        left.addWidget(layout_hint)

        right = QVBoxLayout()
        right.setSpacing(10)
        body.addLayout(right, 1)

        editor_card = SubCard()
        form = QGridLayout()
        form.setHorizontalSpacing(10)
        form.setVerticalSpacing(10)
        editor_card.layout.addLayout(form)

        axis_label = QLabel("Axis")
        axis_label.setObjectName("Muted")
        self.focus_axis_combo = QComboBox()
        for label, value in GAMEPAD_AXES.items():
            self.focus_axis_combo.addItem(label, value)
        form.addWidget(axis_label, 0, 0)
        form.addWidget(self.focus_axis_combo, 0, 1)

        direction_label = QLabel("Direction")
        direction_label.setObjectName("Muted")
        self.focus_direction_combo = QComboBox()
        for label, value in GAMEPAD_DIRECTIONS.items():
            self.focus_direction_combo.addItem(label, value)
        form.addWidget(direction_label, 1, 0)
        form.addWidget(self.focus_direction_combo, 1, 1)

        button_label = QLabel("Button")
        button_label.setObjectName("Muted")
        self.focus_button_combo = QComboBox()
        for label, value in GAMEPAD_BUTTONS.items():
            self.focus_button_combo.addItem(label, value)
        form.addWidget(button_label, 2, 0)
        form.addWidget(self.focus_button_combo, 2, 1)
        right.addWidget(editor_card)

        stats_card = SubCard()
        focused_stats = QGridLayout()
        focused_stats.setHorizontalSpacing(8)
        focused_stats.setVerticalSpacing(8)
        stats_card.layout.addLayout(focused_stats)
        self.focus_mapping_live_labels: dict[str, QLabel] = {}
        for row, key in enumerate(("normalized", "distance", "mapping")):
            label = QLabel(
                {
                    "normalized": "Focused key travel",
                    "distance": "Focused key distance",
                    "mapping": "Current summary",
                }[key]
            )
            label.setObjectName("Muted")
            value = QLabel("--")
            focused_stats.addWidget(label, row, 0)
            focused_stats.addWidget(value, row, 1)
            self.focus_mapping_live_labels[key] = value
        right.addWidget(stats_card)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Load Focused Mapping", self.load_focused_mapping))
        actions.addWidget(make_primary_button("Apply Focused Mapping", self.apply_focused_mapping))
        actions.addStretch(1)
        right.addLayout(actions)
        right.addStretch(1)
        return card

    def _build_mapping_table_card(self) -> SectionCard:
        card = SectionCard(
            "Bulk Mapping Table",
            "Overview of all 82 stored mappings. Use it for mass edits, but the focused editor above remains the main workflow.",
        )

        self.mapping_table = QTableWidget(KEY_COUNT, 5)
        self.mapping_table.setHorizontalHeaderLabels(["Key", "Axis", "Direction", "Button", "Summary"])
        self.mapping_table.verticalHeader().setVisible(False)
        self.mapping_table.horizontalHeader().setStretchLastSection(True)
        self.mapping_table.setMinimumHeight(260)
        self.mapping_table.verticalHeader().setDefaultSectionSize(34)
        self.mapping_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)

        for row in range(KEY_COUNT):
            self.mapping_table.setItem(row, 0, QTableWidgetItem(key_display_name(row)))
            axis_combo = QComboBox()
            for label, value in GAMEPAD_AXES.items():
                axis_combo.addItem(label, value)
            direction_combo = QComboBox()
            for label, value in GAMEPAD_DIRECTIONS.items():
                direction_combo.addItem(label, value)
            button_combo = QComboBox()
            for label, value in GAMEPAD_BUTTONS.items():
                button_combo.addItem(label, value)
            self.mapping_table.setCellWidget(row, 1, axis_combo)
            self.mapping_table.setCellWidget(row, 2, direction_combo)
            self.mapping_table.setCellWidget(row, 3, button_combo)
            self.mapping_table.setItem(row, 4, QTableWidgetItem("Unassigned"))
            axis_combo.currentIndexChanged.connect(lambda _=None, r=row: self._refresh_mapping_summary(r))
            direction_combo.currentIndexChanged.connect(lambda _=None, r=row: self._refresh_mapping_summary(r))
            button_combo.currentIndexChanged.connect(lambda _=None, r=row: self._refresh_mapping_summary(r))

        self.mapping_table.itemSelectionChanged.connect(self._on_table_selection_changed)
        card.body_layout.addWidget(self.mapping_table)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Reload Mappings", self.load_gamepad_mapping))
        actions.addWidget(make_primary_button("Apply Mappings", self.apply_gamepad_mapping))
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

    def _set_selected_key_label(self) -> None:
        self.focused_key_label.setText(key_display_name(self._current_key))
        self.focus_mapping_chip.setText(key_display_name(self._current_key))
        self.mapping_table.selectRow(self._current_key)
        self._sync_focused_mapping_from_table()

    def _update_deadzone_label(self, value: int) -> None:
        raw = int(_clamp(value, 0, 255))
        effective = _effective_radial_deadzone(raw, self._curve_points)
        curve_floor = _curve_floor_output(self._curve_points)
        percent = effective * 100.0 / 255.0
        if effective != raw:
            self.deadzone_value_label.setText(
                f"{effective}/255 effective (~{percent:.1f}%) • manual {raw}/255 • curve floor {curve_floor}/255"
            )
        else:
            self.deadzone_value_label.setText(f"{effective}/255 (~{percent:.1f}%)")
        self.preview_widget.set_state(
            self.preview_widget.x,
            self.preview_widget.y,
            self.square_check.isChecked(),
            effective,
        )

    def _sync_deadzone_floor(self) -> None:
        floor = _curve_floor_output(self._curve_points)
        if self.deadzone_slider.value() < floor:
            self.deadzone_slider.setValue(floor)
            return
        self._update_deadzone_label(self.deadzone_slider.value())

    def _sync_curve_spinboxes(self) -> None:
        self._curve_points = _sanitize_curve_points(self._curve_points)
        self.curve_editor.blockSignals(True)
        self.curve_editor.set_points(self._curve_points)
        self.curve_editor.blockSignals(False)
        for index, point in enumerate(self._curve_points):
            self.curve_x_spins[index].blockSignals(True)
            self.curve_y_spins[index].blockSignals(True)
            self.curve_x_spins[index].setValue(int(point["x_01mm"]))
            self.curve_y_spins[index].setValue(int(point["y"]))
            self.curve_x_spins[index].blockSignals(False)
            self.curve_y_spins[index].blockSignals(False)

    def _on_curve_editor_changed(self, points) -> None:
        if self._loading:
            return
        self._curve_points = _sanitize_curve_points(points)
        self._sync_curve_spinboxes()
        self._sync_deadzone_floor()

    def _on_curve_spin_changed(self) -> None:
        if self._loading:
            return
        points = []
        for index in range(GAMEPAD_CURVE_POINT_COUNT):
            points.append({"x_01mm": int(self.curve_x_spins[index].value()), "y": int(self.curve_y_spins[index].value())})
        self._curve_points = _sanitize_curve_points(points)
        self._sync_curve_spinboxes()
        self._sync_deadzone_floor()

    def _refresh_mapping_summary(self, row: int) -> None:
        axis_combo = self.mapping_table.cellWidget(row, 1)
        direction_combo = self.mapping_table.cellWidget(row, 2)
        button_combo = self.mapping_table.cellWidget(row, 3)
        axis_name = axis_combo.currentText()
        direction_name = direction_combo.currentText()
        button_name = button_combo.currentText()
        parts = []
        if axis_name != "None":
            parts.append(f"{axis_name} {direction_name}")
        if button_name != "None":
            parts.append(button_name)
        summary = " + ".join(parts) if parts else "Unassigned"
        self.mapping_table.item(row, 4).setText(summary)
        if row == self._current_key:
            self.focus_mapping_summary.set_text_and_level(summary, "neutral")
            self.focus_mapping_live_labels["mapping"].setText(summary)

    def _sync_focused_mapping_from_table(self) -> None:
        row = self._current_key
        axis_combo = self.mapping_table.cellWidget(row, 1)
        direction_combo = self.mapping_table.cellWidget(row, 2)
        button_combo = self.mapping_table.cellWidget(row, 3)
        self.focus_axis_combo.setCurrentIndex(max(0, self.focus_axis_combo.findData(int(axis_combo.currentData()))))
        self.focus_direction_combo.setCurrentIndex(max(0, self.focus_direction_combo.findData(int(direction_combo.currentData()))))
        self.focus_button_combo.setCurrentIndex(max(0, self.focus_button_combo.findData(int(button_combo.currentData()))))
        summary = self.mapping_table.item(row, 4).text()
        self.focus_mapping_summary.set_text_and_level(summary, "neutral")
        self.focus_mapping_live_labels["mapping"].setText(summary)

    def _copy_focused_mapping_to_table(self) -> None:
        row = self._current_key
        axis_combo = self.mapping_table.cellWidget(row, 1)
        direction_combo = self.mapping_table.cellWidget(row, 2)
        button_combo = self.mapping_table.cellWidget(row, 3)
        axis_combo.setCurrentIndex(max(0, axis_combo.findData(int(self.focus_axis_combo.currentData()))))
        direction_combo.setCurrentIndex(max(0, direction_combo.findData(int(self.focus_direction_combo.currentData()))))
        button_combo.setCurrentIndex(max(0, button_combo.findData(int(self.focus_button_combo.currentData()))))
        self._refresh_mapping_summary(row)

    def _on_table_selection_changed(self) -> None:
        row = self.mapping_table.currentRow()
        if row < 0 or row >= KEY_COUNT:
            return
        try:
            self.session.set_selected_key(row)
        except Exception:
            self._current_key = row
            self._set_selected_key_label()

    def _current_keyboard_routing(self) -> int:
        if not self.keep_keyboard_check.isChecked():
            return int(GAMEPAD_KEYBOARD_ROUTING["Disabled"])
        if self.replace_mapped_check.isChecked():
            return int(GAMEPAD_KEYBOARD_ROUTING["Unmapped Only"])
        return int(GAMEPAD_KEYBOARD_ROUTING["All Keys"])

    def _set_keyboard_routing_controls(self, routing: int) -> None:
        routing = int(_clamp(routing, 0, 2))
        self.keep_keyboard_check.setChecked(routing != int(GAMEPAD_KEYBOARD_ROUTING["Disabled"]))
        self.replace_mapped_check.setChecked(routing == int(GAMEPAD_KEYBOARD_ROUTING["Unmapped Only"]))

    def _collect_axis_preview(self, distances_mm: list[float]) -> tuple[float, float]:
        positive = {axis: 0 for axis in GAMEPAD_AXES.values()}
        negative = {axis: 0 for axis in GAMEPAD_AXES.values()}

        for row in range(KEY_COUNT):
            distance_mm = float(distances_mm[row]) if row < len(distances_mm) else 0.0
            axis = int(self.mapping_table.cellWidget(row, 1).currentData())
            direction = int(self.mapping_table.cellWidget(row, 2).currentData())
            if axis <= 0:
                continue
            value = _curve_eval(self._curve_points, distance_mm)
            if direction == GAMEPAD_DIRECTIONS["-"]:
                negative[axis] = max(negative[axis], value)
            else:
                positive[axis] = max(positive[axis], value)

        x = _compose_axis_value(
            positive[GAMEPAD_AXES["Left Stick X"]],
            negative[GAMEPAD_AXES["Left Stick X"]],
            self.reactive_check.isChecked(),
        )
        y = _compose_axis_value(
            positive[GAMEPAD_AXES["Left Stick Y"]],
            negative[GAMEPAD_AXES["Left Stick Y"]],
            self.reactive_check.isChecked(),
        )
        x, y = _shape_stick_pair(
            x,
            y,
            _effective_radial_deadzone(self.deadzone_slider.value(), self._curve_points),
            self.square_check.isChecked(),
        )
        return x / 255.0, y / 255.0

    def reload(self) -> None:
        self._set_selected_key_label()
        self.load_global_settings()
        self.load_gamepad_mapping()

    def load_global_settings(self) -> None:
        try:
            settings = self.device.get_gamepad_settings() or {}
        except Exception as exc:
            self._update_status(f"Failed to load global settings: {exc}", "error")
            return

        self._loading = True
        self._curve_points = _sanitize_curve_points(settings.get("curve_points") or _default_curve_points())
        self._sync_curve_spinboxes()
        self.deadzone_slider.setValue(
            _effective_radial_deadzone(
                int(settings.get("radial_deadzone", settings.get("deadzone", 0))),
                self._curve_points,
            )
        )
        self._set_keyboard_routing_controls(int(settings.get("keyboard_routing", GAMEPAD_KEYBOARD_ROUTING["All Keys"])))
        self.square_check.setChecked(bool(settings.get("square_mode", False)))
        self.reactive_check.setChecked(bool(settings.get("reactive_stick", settings.get("snappy_mode", False))))
        self._loading = False
        self._update_deadzone_label(self.deadzone_slider.value())
        self._update_status("Global gamepad settings loaded.", "success")

    def apply_gamepad_settings(self) -> None:
        payload = {
            "radial_deadzone": _effective_radial_deadzone(self.deadzone_slider.value(), self._curve_points),
            "keyboard_routing": self._current_keyboard_routing(),
            "square_mode": self.square_check.isChecked(),
            "reactive_stick": self.reactive_check.isChecked(),
            "curve_points": self._curve_points,
        }
        try:
            if not self.device.set_gamepad_settings(payload):
                raise RuntimeError("device rejected one or more settings")
        except Exception as exc:
            self._update_status(f"Failed to apply gamepad settings: {exc}", "error")
            return
        self._update_status("Gamepad settings applied live.", "success")

    def load_gamepad_mapping(self) -> None:
        missing = []
        for row in range(KEY_COUNT):
            try:
                mapping = self.device.get_key_gamepad_map(row)
            except Exception:
                mapping = None
            axis_combo = self.mapping_table.cellWidget(row, 1)
            direction_combo = self.mapping_table.cellWidget(row, 2)
            button_combo = self.mapping_table.cellWidget(row, 3)
            if not mapping:
                missing.append(row + 1)
                axis_combo.setCurrentIndex(0)
                direction_combo.setCurrentIndex(0)
                button_combo.setCurrentIndex(0)
            else:
                axis_combo.setCurrentIndex(max(0, axis_combo.findData(int(mapping.get("axis", 0)))))
                direction_combo.setCurrentIndex(max(0, direction_combo.findData(int(mapping.get("direction", 0)))))
                button_combo.setCurrentIndex(max(0, button_combo.findData(int(mapping.get("button", 0)))))
            self._refresh_mapping_summary(row)
        self._sync_focused_mapping_from_table()
        if missing:
            self._update_status(
                f"Loaded mappings with gaps: {', '.join(key_display_name(n - 1) for n in missing)}.",
                "warning",
            )
        else:
            self._update_status("Per-key mappings loaded.", "success")

    def apply_gamepad_mapping(self) -> None:
        failed = []
        for row in range(KEY_COUNT):
            axis_combo = self.mapping_table.cellWidget(row, 1)
            direction_combo = self.mapping_table.cellWidget(row, 2)
            button_combo = self.mapping_table.cellWidget(row, 3)
            try:
                ok = self.device.set_key_gamepad_map(
                    row,
                    int(axis_combo.currentData()),
                    int(direction_combo.currentData()),
                    int(button_combo.currentData()),
                )
                if not ok:
                    failed.append(row + 1)
            except Exception:
                failed.append(row + 1)
        if failed:
            self._update_status(
                f"Mapping update failed for {', '.join(key_display_name(n - 1) for n in failed)}.",
                "error",
            )
        else:
            self._update_status("All per-key mappings applied.", "success")

    def load_focused_mapping(self) -> None:
        row = self._current_key
        try:
            mapping = self.device.get_key_gamepad_map(row)
        except Exception as exc:
            self._update_status(f"Focused mapping load failed: {exc}", "error")
            return
        if not mapping:
            self._update_status(f"No mapping returned for {key_display_name(row)}.", "warning")
            return
        self.focus_axis_combo.setCurrentIndex(max(0, self.focus_axis_combo.findData(int(mapping.get("axis", 0)))))
        self.focus_direction_combo.setCurrentIndex(max(0, self.focus_direction_combo.findData(int(mapping.get("direction", 0)))))
        self.focus_button_combo.setCurrentIndex(max(0, self.focus_button_combo.findData(int(mapping.get("button", 0)))))
        self._copy_focused_mapping_to_table()
        self._update_status(f"Loaded focused mapping for {key_display_name(row)}.", "success")

    def apply_focused_mapping(self) -> None:
        row = self._current_key
        try:
            ok = self.device.set_key_gamepad_map(
                row,
                int(self.focus_axis_combo.currentData()),
                int(self.focus_direction_combo.currentData()),
                int(self.focus_button_combo.currentData()),
            )
            if not ok:
                raise RuntimeError("device rejected focused mapping")
        except Exception as exc:
            self._update_status(f"Focused mapping apply failed: {exc}", "error")
            return
        self._copy_focused_mapping_to_table()
        self._update_status(f"Applied mapping for {key_display_name(row)}.", "success")

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.preview_live_info.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
        else:
            self.preview_live_info.set_text_and_level("OFF", "neutral")

    def _poll_preview(self) -> None:
        try:
            key_states = self.device.get_key_states() or {}
        except Exception as exc:
            self._update_status(f"Preview error: {exc}", "warning")
            return

        distances = list(key_states.get("distances") or [0] * KEY_COUNT)
        distances_mm = list(key_states.get("distances_mm") or [0.0] * KEY_COUNT)
        preview_x, preview_y = self._collect_axis_preview(distances_mm)
        self.preview_widget.set_state(
            preview_x,
            preview_y,
            self.square_check.isChecked(),
            _effective_radial_deadzone(self.deadzone_slider.value(), self._curve_points),
        )

        c = current_colors()
        for i in range(KEY_COUNT):
            norm = int(distances[i]) if i < len(distances) else 0
            ratio = max(0.0, min(1.0, norm / 255.0))
            fill = QColor(c["surface_muted"])
            accent = QColor(c["accent_soft"])
            if ratio > 0:
                r = int(fill.red() + (accent.red() - fill.red()) * ratio)
                g = int(fill.green() + (accent.green() - fill.green()) * ratio)
                b = int(fill.blue() + (accent.blue() - fill.blue()) * ratio)
                fill_hex = QColor(r, g, b).name()
            else:
                fill_hex = c["surface_muted"]
            self.preview_layout.set_key_state(
                i,
                fill=fill_hex,
                border=c["accent"] if i == self._current_key else c["border"],
                tooltip=(
                    f"{key_display_name(i)}\n"
                    f"Normalized: {norm}/255\n"
                    f"Distance: {float(distances_mm[i]) if i < len(distances_mm) else 0.0:.2f} mm"
                ),
            )

        focused_norm = int(distances[self._current_key]) if self._current_key < len(distances) else 0
        focused_mm = float(distances_mm[self._current_key]) if self._current_key < len(distances_mm) else 0.0
        mapping_summary = self.mapping_table.item(self._current_key, 4).text()
        self.focus_axis_labels["normalized"].setText(f"{focused_norm}/255 ({focused_norm * 100 // 255}%)")
        self.focus_axis_labels["distance"].setText(f"{focused_mm:.2f} mm")
        self.focus_axis_labels["mapping"].setText(mapping_summary)
        self.focus_axis_labels["vector"].setText(f"X {preview_x:+.2f} | Y {preview_y:+.2f}")
        self.focus_mapping_live_labels["normalized"].setText(f"{focused_norm}/255 ({focused_norm * 100 // 255}%)")
        self.focus_mapping_live_labels["distance"].setText(f"{focused_mm:.2f} mm")
        self.focus_mapping_live_labels["mapping"].setText(mapping_summary)

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._poll_preview()

    def apply_theme(self) -> None:
        pass

    def on_selected_key_changed(self, key_index: int) -> None:
        self._current_key = int(_clamp(key_index, 0, KEY_COUNT - 1))
        self._set_selected_key_label()

    def on_page_activated(self) -> None:
        self._page_active = True
        self.reload()
        if self.session.live_enabled:
            self._poll_preview()

    def on_page_deactivated(self) -> None:
        self._page_active = False
