from __future__ import annotations

from PySide6.QtCore import QTimer, Qt, QRectF
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
)
from ..theme import current_colors
from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
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
        self._current_key = int(getattr(session, "selected_key", 0))
        self._page_active = False
        self._loading = False
        self.preview_timer = QTimer(self)
        self.preview_timer.timeout.connect(self._poll_preview)
        self._build_ui()
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

        # ── Top row: settings + live preview ──────────────────────────
        top_row = QHBoxLayout()
        top_row.setSpacing(14)
        top_row.addWidget(self._build_settings_card(), 1)
        top_row.addWidget(self._build_preview_card(), 1)
        scaffold.content_layout.addLayout(top_row)

        # ── Mapping table card ─────────────────────────────────────────
        scaffold.add_card(self._build_mapping_card())

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
        self.focused_key_label = StatusChip("Key 1", "info")
        focused_row.addWidget(self.focused_key_label)
        focused_row.addStretch(1)
        card.body_layout.addLayout(focused_row)

        # Deadzone slider
        dz_label_row = QHBoxLayout()
        dz_lbl = QLabel("Deadzone")
        dz_lbl.setObjectName("Muted")
        dz_label_row.addWidget(dz_lbl)
        dz_label_row.addStretch(1)
        self.deadzone_value_label = QLabel()
        self.deadzone_value_label.setObjectName("Muted")
        dz_label_row.addWidget(self.deadzone_value_label)
        card.body_layout.addLayout(dz_label_row)

        self.deadzone_slider = QSlider(Qt.Orientation.Horizontal)
        self.deadzone_slider.setRange(0, 255)
        self.deadzone_slider.valueChanged.connect(self._update_deadzone_label)
        card.body_layout.addWidget(self.deadzone_slider)

        # Curve combo
        curve_row = QHBoxLayout()
        curve_lbl = QLabel("Curve")
        curve_lbl.setObjectName("Muted")
        curve_row.addWidget(curve_lbl)
        self.curve_combo = QComboBox()
        for value, label in self.CURVE_LABELS.items():
            self.curve_combo.addItem(label, value)
        curve_row.addWidget(self.curve_combo, 1)
        card.body_layout.addLayout(curve_row)

        self.square_check = QCheckBox("Square mode")
        self.snappy_check = QCheckBox("Snappy mode")
        self.mirror_check = QCheckBox("Mirror keyboard output with gamepad")
        card.body_layout.addWidget(self.square_check)
        card.body_layout.addWidget(self.snappy_check)
        card.body_layout.addWidget(self.mirror_check)

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
        self.preview_toggle = QCheckBox("Enable live preview")
        self.preview_toggle.toggled.connect(self._sync_preview_timer)
        preview_header.addWidget(self.preview_toggle)
        preview_header.addStretch(1)
        interval_lbl = QLabel("Interval (ms)")
        interval_lbl.setObjectName("Muted")
        preview_header.addWidget(interval_lbl)
        self.preview_interval_spin = QSpinBox()
        self.preview_interval_spin.setRange(20, 500)
        self.preview_interval_spin.setValue(75)
        self.preview_interval_spin.valueChanged.connect(self._sync_preview_timer)
        preview_header.addWidget(self.preview_interval_spin)
        card.body_layout.addLayout(preview_header)

        self.preview_widget = StickPreview()
        card.body_layout.addWidget(self.preview_widget)

        bar_grid = QGridLayout()
        bar_grid.setHorizontalSpacing(8)
        bar_grid.setVerticalSpacing(6)
        self.key_distance_bars: list[QFrame] = []
        self.key_distance_bar_hosts: list[QFrame] = []
        self.key_distance_labels: list[QLabel] = []
        c = current_colors()
        for i in range(6):
            row_lbl = QLabel(f"Key {i + 1}")
            row_lbl.setObjectName("Muted")
            val_lbl = QLabel("0%")

            bar_host = QFrame()
            bar_host.setStyleSheet(
                f"background: {c['surface_alt']}; border-radius: 6px;"
            )
            bar_host_layout = QHBoxLayout(bar_host)
            bar_host_layout.setContentsMargins(0, 0, 0, 0)
            fill = QFrame()
            fill.setStyleSheet(
                f"background: {c['accent']}; border-radius: 6px;"
            )
            fill.setMaximumWidth(0)
            fill.setMinimumHeight(12)
            bar_host_layout.addWidget(fill)

            bar_grid.addWidget(row_lbl, i, 0)
            bar_grid.addWidget(bar_host, i, 1)
            bar_grid.addWidget(val_lbl, i, 2)
            self.key_distance_bars.append(fill)
            self.key_distance_bar_hosts.append(bar_host)
            self.key_distance_labels.append(val_lbl)

        card.body_layout.addLayout(bar_grid)
        return card

    def _build_mapping_card(self) -> SectionCard:
        card = SectionCard(
            "Per-Key Mapping",
            "Each key can drive one axis/direction pair and one optional gamepad button.",
        )

        self.mapping_table = QTableWidget(6, 5)
        self.mapping_table.setHorizontalHeaderLabels(
            ["Key", "Axis", "Direction", "Button", "Summary"]
        )
        self.mapping_table.verticalHeader().setVisible(False)
        self.mapping_table.horizontalHeader().setStretchLastSection(True)
        self.mapping_table.setAlternatingRowColors(False)
        self.mapping_table.setMinimumHeight(300)
        self.mapping_table.verticalHeader().setDefaultSectionSize(44)

        for row in range(6):
            self.mapping_table.setItem(row, 0, QTableWidgetItem(f"Key {row + 1}"))
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
        self.focused_key_label.setText(f"Key {self._current_key + 1}")
        self.mapping_table.selectRow(self._current_key)

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
        for row in range(6):
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
        if missing:
            self._update_status(
                f"Loaded mappings with gaps: {', '.join(f'Key {n}' for n in missing)}.",
                "warning",
            )
        else:
            self._update_status("Per-key mappings loaded.", "success")

    def apply_gamepad_mapping(self) -> None:
        failed = []
        for row in range(6):
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
                f"Mapping update failed for {', '.join(f'Key {n}' for n in failed)}.", "error"
            )
        else:
            self._update_status("All per-key mappings applied.", "success")

    def _sync_preview_timer(self) -> None:
        if self.preview_toggle.isChecked() and self._page_active:
            self.preview_timer.start(int(self.preview_interval_spin.value()))
            self._poll_preview()
        else:
            self.preview_timer.stop()

    def _set_preview_bar(self, index: int, normalized_value: int) -> None:
        fill = self.key_distance_bars[index]
        bar_host = fill.parentWidget()
        host_width = max(1, bar_host.width())
        fill.setMaximumWidth(int((host_width - 4) * (normalized_value / 255.0)))
        self.key_distance_labels[index].setText(f"{int(normalized_value * 100 / 255)}%")

    def _poll_preview(self) -> None:
        try:
            key_states = self.device.get_key_states() or {}
        except Exception as exc:
            self._update_status(f"Preview error: {exc}", "warning")
            self.preview_timer.stop()
            return

        distances = list(key_states.get("distances") or [0] * 6)
        for i in range(6):
            self._set_preview_bar(i, int(distances[i]) if i < len(distances) else 0)

        x, y = 0.0, 0.0
        if len(distances) >= 4:
            x = (float(distances[1]) - float(distances[0])) / 255.0
            y = (float(distances[3]) - float(distances[2])) / 255.0
        self.preview_widget.set_state(x, y, self.square_check.isChecked(), self.deadzone_slider.value())

    def apply_theme(self) -> None:
        c = current_colors()
        for bar_host, fill in zip(self.key_distance_bar_hosts, self.key_distance_bars):
            bar_host.setStyleSheet(f"background: {c['surface_alt']}; border-radius: 6px;")
            fill.setStyleSheet(f"background: {c['accent']}; border-radius: 6px;")

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_selected_key_changed(self, key_index: int) -> None:
        self._current_key = _clamp(key_index, 0, 5)
        self._set_selected_key_label()

    def on_page_activated(self) -> None:
        self._page_active = True
        self.reload()
        self._sync_preview_timer()

    def on_page_deactivated(self) -> None:
        self._page_active = False
        self.preview_timer.stop()
