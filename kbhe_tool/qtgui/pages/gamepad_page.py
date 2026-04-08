from __future__ import annotations

from PySide6.QtCore import Qt, QRectF
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

from kbhe_tool.protocol import (
    GAMEPAD_AXES,
    GAMEPAD_BUTTONS,
    GAMEPAD_DIRECTIONS,
    KEY_COUNT,
)
from kbhe_tool.key_layout import key_display_name
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


# ---------------------------------------------------------------------------
# StickPreview – custom QPainter joystick visualisation
# ---------------------------------------------------------------------------

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
        self.deadzone_raw = _clamp(deadzone_raw, 0, 255)
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
        painter.drawText(12, 20, "Live joystick preview")
        painter.drawText(12, self.height() - 12, f"X {self.x:+.2f}   Y {self.y:+.2f}")


# ---------------------------------------------------------------------------
# GamepadPage
# ---------------------------------------------------------------------------

class GamepadPage(QWidget):
    CURVE_LABELS = {0: "Linear", 1: "Smooth", 2: "Aggressive"}

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._current_key = _clamp(int(getattr(session, "selected_key", 0)), 0, KEY_COUNT - 1)
        self._page_active = False
        self._loading = False
        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
        except Exception:
            pass
        self._build_ui()
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
            "Gamepad",
            "Shape analog response, preview travel live, and configure per-key gamepad mappings.",
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

        # ── Status chip ───────────────────────────────────────────────
        self.status_chip = StatusChip("Gamepad page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

        self._set_selected_key_label()
        self._update_deadzone_label(self.deadzone_slider.value())

    def _build_settings_card(self) -> SectionCard:
        card = SectionCard(
            "Global Response",
            "Tune deadzone, curve type, and output behavior. "
            "Changes are applied when you click Apply Global Settings.",
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

        # Deadzone slider
        dz_label_row = QHBoxLayout()
        dz_lbl = QLabel("Deadzone")
        dz_lbl.setObjectName("Muted")
        dz_label_row.addWidget(dz_lbl)
        dz_label_row.addStretch(1)
        self.deadzone_value_label = QLabel()
        self.deadzone_value_label.setObjectName("Muted")
        dz_label_row.addWidget(self.deadzone_value_label)
        tuning.layout.addLayout(dz_label_row)

        self.deadzone_slider = QSlider(Qt.Orientation.Horizontal)
        self.deadzone_slider.setRange(0, 255)
        self.deadzone_slider.valueChanged.connect(self._update_deadzone_label)
        tuning.layout.addWidget(self.deadzone_slider)

        # Curve combo
        curve_row = QHBoxLayout()
        curve_lbl = QLabel("Curve")
        curve_lbl.setObjectName("Muted")
        curve_row.addWidget(curve_lbl)
        self.curve_combo = QComboBox()
        for value, label in self.CURVE_LABELS.items():
            self.curve_combo.addItem(label, value)
        curve_row.addWidget(self.curve_combo, 1)
        tuning.layout.addLayout(curve_row)

        self.square_check = QCheckBox("Square mode")
        self.snappy_check = QCheckBox("Snappy mode")
        self.mirror_check = QCheckBox("Mirror keyboard output with gamepad")
        tuning.layout.addWidget(self.square_check)
        tuning.layout.addWidget(self.snappy_check)
        tuning.layout.addWidget(self.mirror_check)
        card.body_layout.addWidget(tuning)

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
            "Read the current key travel and preview joystick response while this page is active.",
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
        self.preview_widget.setMinimumSize(220, 220)
        card.body_layout.addWidget(self.preview_widget)

        meters_card = SubCard()
        meters = QGridLayout()
        meters.setHorizontalSpacing(8)
        meters.setVerticalSpacing(8)
        meters_card.layout.addLayout(meters)

        self.focus_axis_labels: dict[str, QLabel] = {}
        for row, key in enumerate(("normalized", "distance", "mapping")):
            label = QLabel(
                {
                    "normalized": "Focused key travel",
                    "distance": "Focused key distance",
                    "mapping": "Focused mapping",
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
            "Le layout 82 touches sert de sélecteur principal. La colonne de droite édite le mapping gamepad de la touche focalisée.",
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
        body.setAlignment(left, Qt.AlignTop)

        self.preview_layout = KeyboardLayoutWidget(self.session, unit=34)
        left.addWidget(self.preview_layout, 0, Qt.AlignTop | Qt.AlignLeft)

        layout_hint = QLabel(
            "La teinte reflète la course live de chaque touche. Clique un keycap pour éditer directement son mapping."
        )
        layout_hint.setObjectName("Muted")
        layout_hint.setWordWrap(True)
        left.addWidget(layout_hint)

        right = QVBoxLayout()
        right.setSpacing(10)
        body.addLayout(right, 1)
        body.setAlignment(right, Qt.AlignTop)

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
            "Vue d’ensemble des 82 mappings. Elle reste disponible pour les edits de masse, mais la carte ci-dessus est le workflow principal.",
        )

        self.mapping_table = QTableWidget(KEY_COUNT, 5)
        self.mapping_table.setHorizontalHeaderLabels(
            ["Key", "Axis", "Direction", "Button", "Summary"]
        )
        self.mapping_table.verticalHeader().setVisible(False)
        self.mapping_table.horizontalHeader().setStretchLastSection(True)
        self.mapping_table.setAlternatingRowColors(False)
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
            axis_combo.currentIndexChanged.connect(
                lambda _=None, r=row: self._refresh_mapping_summary(r)
            )
            direction_combo.currentIndexChanged.connect(
                lambda _=None, r=row: self._refresh_mapping_summary(r)
            )
            button_combo.currentIndexChanged.connect(
                lambda _=None, r=row: self._refresh_mapping_summary(r)
            )
        self.mapping_table.itemSelectionChanged.connect(self._on_table_selection_changed)

        card.body_layout.addWidget(self.mapping_table)

        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(make_secondary_button("Reload Mappings", self.load_gamepad_mapping))
        actions.addWidget(make_primary_button("Apply Mappings", self.apply_gamepad_mapping))
        actions.addStretch(1)
        card.body_layout.addLayout(actions)

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

    def _set_selected_key_label(self) -> None:
        self.focused_key_label.setText(key_display_name(self._current_key))
        self.focus_mapping_chip.setText(key_display_name(self._current_key))
        self.mapping_table.selectRow(self._current_key)
        self._sync_focused_mapping_from_table()

    def _update_deadzone_label(self, value: int) -> None:
        raw = _clamp(value, 0, 255)
        percent = raw * 100.0 / 255.0
        self.deadzone_value_label.setText(f"{raw}/255 (~{percent:.1f}%)")
        self.preview_widget.set_state(
            self.preview_widget.x, self.preview_widget.y, self.square_check.isChecked(), raw
        )

    def _curve_value(self) -> int:
        return int(self.curve_combo.currentData())

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
        self.mapping_table.item(row, 4).setText(" + ".join(parts) if parts else "Unassigned")
        if row == self._current_key:
            self.focus_mapping_summary.set_text_and_level(
                self.mapping_table.item(row, 4).text(), "neutral"
            )
            self.focus_mapping_live_labels["mapping"].setText(self.mapping_table.item(row, 4).text())

    def _sync_focused_mapping_from_table(self) -> None:
        row = self._current_key
        axis_combo = self.mapping_table.cellWidget(row, 1)
        direction_combo = self.mapping_table.cellWidget(row, 2)
        button_combo = self.mapping_table.cellWidget(row, 3)
        self.focus_axis_combo.setCurrentIndex(max(0, self.focus_axis_combo.findData(int(axis_combo.currentData()))))
        self.focus_direction_combo.setCurrentIndex(
            max(0, self.focus_direction_combo.findData(int(direction_combo.currentData())))
        )
        self.focus_button_combo.setCurrentIndex(max(0, self.focus_button_combo.findData(int(button_combo.currentData()))))
        self.focus_mapping_summary.set_text_and_level(self.mapping_table.item(row, 4).text(), "neutral")
        self.focus_mapping_live_labels["mapping"].setText(self.mapping_table.item(row, 4).text())

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

    # ------------------------------------------------------------------
    # Data loading / actions
    # ------------------------------------------------------------------

    def reload(self) -> None:
        self._set_selected_key_label()
        self.load_global_settings()
        self.load_gamepad_mapping()

    def load_global_settings(self) -> None:
        try:
            settings = self.device.get_gamepad_settings() or {}
            mirror = self.device.get_gamepad_with_keyboard()
        except Exception as exc:
            self._update_status(f"Failed to load global settings: {exc}", "error")
            return

        self._loading = True
        self.deadzone_slider.setValue(int(settings.get("deadzone", 0)))
        curve_index = max(0, self.curve_combo.findData(int(settings.get("curve_type", 0))))
        self.curve_combo.setCurrentIndex(curve_index)
        self.square_check.setChecked(bool(settings.get("square_mode", False)))
        self.snappy_check.setChecked(bool(settings.get("snappy_mode", False)))
        self.mirror_check.setChecked(bool(mirror))
        self._loading = False
        self._update_deadzone_label(self.deadzone_slider.value())
        self._update_status("Global gamepad settings loaded.", "success")

    def apply_gamepad_settings(self) -> None:
        try:
            ok_settings = self.device.set_gamepad_settings(
                _clamp(self.deadzone_slider.value(), 0, 255),
                self._curve_value(),
                self.square_check.isChecked(),
                self.snappy_check.isChecked(),
            )
            ok_mirror = self.device.set_gamepad_with_keyboard(self.mirror_check.isChecked())
            if not ok_settings or not ok_mirror:
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
                axis_combo.setCurrentIndex(
                    max(0, axis_combo.findData(int(mapping.get("axis", 0))))
                )
                direction_combo.setCurrentIndex(
                    max(0, direction_combo.findData(int(mapping.get("direction", 0))))
                )
                button_combo.setCurrentIndex(
                    max(0, button_combo.findData(int(mapping.get("button", 0))))
                )
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
                f"Mapping update failed for {', '.join(key_display_name(n - 1) for n in failed)}.", "error"
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
        self.focus_direction_combo.setCurrentIndex(
            max(0, self.focus_direction_combo.findData(int(mapping.get("direction", 0))))
        )
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
        x, y = 0.0, 0.0
        if len(distances) >= 4:
            x = (float(distances[1]) - float(distances[0])) / 255.0
            y = (float(distances[3]) - float(distances[2])) / 255.0
        self.preview_widget.set_state(x, y, self.square_check.isChecked(), self.deadzone_slider.value())

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
        self.focus_axis_labels["normalized"].setText(f"{focused_norm}/255 ({focused_norm * 100 // 255}%)")
        self.focus_axis_labels["distance"].setText(f"{focused_mm:.2f} mm")
        self.focus_axis_labels["mapping"].setText(self.mapping_table.item(self._current_key, 4).text())
        self.focus_mapping_live_labels["normalized"].setText(f"{focused_norm}/255 ({focused_norm * 100 // 255}%)")
        self.focus_mapping_live_labels["distance"].setText(f"{focused_mm:.2f} mm")
        self.focus_mapping_live_labels["mapping"].setText(self.mapping_table.item(self._current_key, 4).text())

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._poll_preview()

    def apply_theme(self) -> None:
        pass

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_selected_key_changed(self, key_index: int) -> None:
        self._current_key = _clamp(key_index, 0, KEY_COUNT - 1)
        self._set_selected_key_label()

    def on_page_activated(self) -> None:
        self._page_active = True
        self.reload()
        if self.session.live_enabled:
            self._poll_preview()

    def on_page_deactivated(self) -> None:
        self._page_active = False
