from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QBrush, QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QSizePolicy,
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
    SubCard,
    make_secondary_button,
)

_MAX_MM = 4.0
_GRID_STEP = 0.5


def _mix_colors(color_a: str, color_b: str, ratio: float) -> str:
    ratio = max(0.0, min(1.0, float(ratio)))
    a = QColor(color_a)
    b = QColor(color_b)
    r = int(a.red() + (b.red() - a.red()) * ratio)
    g = int(a.green() + (b.green() - a.green()) * ratio)
    bl = int(a.blue() + (b.blue() - a.blue()) * ratio)
    return QColor(r, g, bl).name()


class KeyTravelBar(QWidget):
    def __init__(self, key_index: int, parent=None):
        super().__init__(parent)
        self.key_index = key_index
        self._distance_mm = 0.0
        self._state = False
        self._thresholds: dict = {}
        self._min_top_mm: float | None = None
        self._max_bottom_mm: float | None = None
        self._prev_state: bool | None = None
        self.setMinimumSize(180, 360)
        sp = QSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setSizePolicy(sp)

    def set_key_index(self, key_index: int) -> None:
        self.key_index = int(key_index)
        self.reset_dynamic()
        self.update()

    def set_thresholds(self, thresholds: dict) -> None:
        self._thresholds = dict(thresholds or {})
        self.update()

    def update_state(self, distance_mm: float, state: bool) -> None:
        if self._prev_state != state:
            self._min_top_mm = distance_mm
            self._max_bottom_mm = distance_mm
        else:
            if self._min_top_mm is None:
                self._min_top_mm = distance_mm
                self._max_bottom_mm = distance_mm
            self._min_top_mm = min(self._min_top_mm, distance_mm)
            self._max_bottom_mm = max(self._max_bottom_mm, distance_mm)
        self._distance_mm = distance_mm
        self._state = state
        self._prev_state = state
        self.update()

    def reset_dynamic(self) -> None:
        self._min_top_mm = None
        self._max_bottom_mm = None
        self._prev_state = None
        self.update()

    def _mm_to_y(self, mm: float, y0: int, y1: int) -> float:
        ratio = max(0.0, min(1.0, mm / _MAX_MM))
        return y1 - ratio * (y1 - y0)

    def paintEvent(self, _event) -> None:
        colors = current_colors()
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        width = self.width()
        height = self.height()
        label_w = 42
        margin_r = 12
        margin_top = 40
        margin_bot = 52

        bar_x0 = label_w
        bar_x1 = width - margin_r
        bar_y0 = margin_top
        bar_y1 = height - margin_bot
        bar_w = max(4, bar_x1 - bar_x0)
        bar_h = max(4, bar_y1 - bar_y0)

        painter.fillRect(self.rect(), QColor(colors["surface"]))

        small_font = QFont()
        small_font.setPointSize(7)
        painter.setFont(small_font)
        t = 0.0
        while t <= _MAX_MM + 1e-9:
            y = int(self._mm_to_y(t, bar_y0, bar_y1))
            painter.setPen(QPen(QColor(colors["border"]), 1))
            painter.drawLine(bar_x0 - 6, y, bar_x1, y)
            painter.setPen(QColor(colors["text_muted"]))
            painter.drawText(
                0,
                y - 7,
                label_w - 8,
                14,
                Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                f"{t:.1f}",
            )
            t = round(t + _GRID_STEP, 6)

        painter.setPen(QPen(QColor(colors["border"]), 1.5))
        painter.setBrush(QBrush(QColor(colors["surface_muted"])))
        painter.drawRect(bar_x0, bar_y0, bar_w, bar_h)

        fill_color = QColor(colors["success"] if self._state else colors["accent"])
        fill_color.setAlpha(180)
        fill_height = int((bar_h - 2) * max(0.0, min(1.0, self._distance_mm / _MAX_MM)))
        if fill_height > 0:
            painter.fillRect(bar_x0 + 1, bar_y1 - fill_height, bar_w - 2, fill_height, fill_color)

        thresholds = self._thresholds
        act_mm = float(thresholds.get("actuation_point_mm", 1.5))
        rel_mm = float(thresholds.get("release_point_mm", 1.5))
        rt_enabled = bool(thresholds.get("rapid_trigger_enabled", False))
        rt_press = float(thresholds.get("rapid_trigger_press", 0.3))
        rt_release = float(thresholds.get("rapid_trigger_release", 0.3))

        def draw_hline(mm_val: float, hex_color: str, dashed: bool = False) -> None:
            y = int(self._mm_to_y(mm_val, bar_y0, bar_y1))
            pen = QPen(QColor(hex_color), 2)
            if dashed:
                pen.setStyle(Qt.PenStyle.DashLine)
            painter.setPen(pen)
            painter.drawLine(bar_x0 - 4, y, bar_x1 + 2, y)

        draw_hline(act_mm, colors["success"])
        draw_hline(rel_mm, colors["warning"])

        if rt_enabled and self._min_top_mm is not None and self._max_bottom_mm is not None:
            rt_press_line = self._min_top_mm + rt_press
            rt_rel_line = self._max_bottom_mm - rt_release
            if 0.0 <= rt_press_line <= _MAX_MM:
                draw_hline(rt_press_line, "#38bdf8", dashed=True)
            if 0.0 <= rt_rel_line <= _MAX_MM:
                draw_hline(rt_rel_line, "#f472b6", dashed=True)

        y_cur = int(self._mm_to_y(self._distance_mm, bar_y0, bar_y1))
        painter.setPen(QPen(QColor("#ffffff"), 2))
        painter.drawLine(bar_x0 - 2, y_cur, bar_x1 + 2, y_cur)

        title_font = QFont()
        title_font.setPointSize(9)
        title_font.setBold(True)
        painter.setFont(title_font)
        painter.setPen(QColor(colors["text"]))
        painter.drawText(
            0,
            0,
            width,
            margin_top,
            Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter,
            f"K{self.key_index + 1:02d} · {key_short_label(self.key_index)}",
        )

        painter.setFont(small_font)
        painter.setPen(QColor(colors["text_muted"]))
        state_str = "PRESSED" if self._state else "IDLE"
        painter.drawText(
            0,
            bar_y1 + 4,
            width,
            margin_bot - 4,
            Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop,
            f"{state_str}\n{self._distance_mm:.2f} mm",
        )


class SelectedTravelCard(SubCard):
    def __init__(self, key_index: int, parent=None):
        super().__init__(parent)
        self.key_index = int(key_index)
        self.title = QLabel(key_display_name(self.key_index))
        self.title.setObjectName("CardTitle")
        self.layout.addWidget(self.title)

        self.travel_bar = KeyTravelBar(self.key_index)
        self.travel_bar.setMinimumSize(150, 260)
        self.layout.addWidget(self.travel_bar)

        row = QHBoxLayout()
        row.setSpacing(6)
        self.state_chip = StatusChip("IDLE", "neutral")
        self.distance_chip = StatusChip("0.00 mm", "info")
        row.addWidget(self.state_chip)
        row.addWidget(self.distance_chip)
        row.addStretch(1)
        self.layout.addLayout(row)

        self.thresholds = QLabel("--")
        self.thresholds.setObjectName("Muted")
        self.thresholds.setWordWrap(True)
        self.layout.addWidget(self.thresholds)

    def set_key_index(self, key_index: int) -> None:
        self.key_index = int(key_index)
        self.title.setText(key_display_name(self.key_index))
        self.travel_bar.set_key_index(self.key_index)

    def apply_values(self, *, distance_mm: float, pressed: bool, thresholds: dict) -> None:
        self.state_chip.set_text_and_level("PRESSED" if pressed else "IDLE", "ok" if pressed else "neutral")
        self.distance_chip.set_text_and_level(f"{distance_mm:.2f} mm", "info")
        self.travel_bar.set_thresholds(thresholds or {})
        self.travel_bar.update_state(float(distance_mm), bool(pressed))
        self.thresholds.setText(
            f"Act {float(thresholds.get('actuation_point_mm', 0.0)):.2f} mm  ·  "
            f"Rel {float(thresholds.get('release_point_mm', 0.0)):.2f} mm  ·  "
            f"RT {float(thresholds.get('rapid_trigger_press', 0.0)):.2f}/{float(thresholds.get('rapid_trigger_release', 0.0)):.2f} mm"
        )


class TravelPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        self._thresholds: list[dict] = [dict() for _ in range(KEY_COUNT)]
        self._states: list[bool] = [False] * KEY_COUNT
        self._distances_mm: list[float] = [0.0] * KEY_COUNT
        self._selected_keys: list[int] = [max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))]
        self._selected_cards: dict[int, SelectedTravelCard] = {}

        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
            self.session.selectedKeyChanged.connect(self._on_selected_key_changed)
        except Exception:
            pass

        self._build_ui()
        self._update_live_info(self.session.live_enabled, self.session.live_interval_ms)
        self._on_selected_key_changed(self.session.selected_key)
        self._load_all_thresholds()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Travel",
            "Vue live des 82 touches sur le layout physique. Clique une touche pour ouvrir la jauge détaillée et visualiser les seuils RT.",
        )
        root.addWidget(scaffold, 1)

        overview_card = SectionCard(
            "Travel Monitor",
            "Le layout physique montre la course live des 82 touches. Clique une touche pour synchroniser immédiatement l’inspecteur.",
        )
        live_row = QHBoxLayout()
        live_row.setSpacing(12)
        label = QLabel("Global live")
        label.setObjectName("Muted")
        live_row.addWidget(label)
        self.live_info = StatusChip("OFF", "neutral")
        live_row.addWidget(self.live_info)
        live_row.addSpacing(10)
        pressed_label = QLabel("Pressed")
        pressed_label.setObjectName("Muted")
        live_row.addWidget(pressed_label)
        self.pressed_chip = StatusChip("0 / 82", "neutral")
        live_row.addWidget(self.pressed_chip)
        live_row.addSpacing(10)
        focus_label = QLabel("Focused")
        focus_label.setObjectName("Muted")
        live_row.addWidget(focus_label)
        self.focus_chip = StatusChip("K01", "info")
        live_row.addWidget(self.focus_chip)
        selected_label = QLabel("Selected")
        selected_label.setObjectName("Muted")
        live_row.addWidget(selected_label)
        self.selected_chip = StatusChip("1 key", "neutral")
        live_row.addWidget(self.selected_chip)
        live_row.addStretch(1)
        live_row.addWidget(make_secondary_button("Add Focused", self.add_focused_key))
        live_row.addWidget(make_secondary_button("Select All", self.select_all_keys))
        live_row.addWidget(make_secondary_button("Clear Selection", self.clear_selected_keys))
        live_row.addWidget(make_secondary_button("Reload Thresholds", self._load_all_thresholds))
        overview_card.body_layout.addLayout(live_row)

        self.layout_view = KeyboardLayoutWidget(self.session, unit=38)
        self.layout_view.keyClicked.connect(self._toggle_selected_key)
        overview_card.body_layout.addWidget(self.layout_view, 0, Qt.AlignTop | Qt.AlignLeft)

        self.selection_label = QLabel()
        self.selection_label.setObjectName("Muted")
        self.selection_label.setWordWrap(True)
        overview_card.body_layout.addWidget(self.selection_label)

        self.overview_hint = QLabel("Appuie sur une touche ou clique un keycap pour inspecter ses seuils.")
        self.overview_hint.setObjectName("Muted")
        self.overview_hint.setWordWrap(True)
        overview_card.body_layout.addWidget(self.overview_hint)
        scaffold.add_card(overview_card)

        self.selected_travel_card = SectionCard(
            "Selected Travel Graphs",
            "Une jauge de course est affichée pour chaque touche sélectionnée dans le layout.",
        )
        self.selected_travel_grid = QGridLayout()
        self.selected_travel_grid.setHorizontalSpacing(10)
        self.selected_travel_grid.setVerticalSpacing(10)
        self.selected_travel_card.body_layout.addLayout(self.selected_travel_grid)
        scaffold.add_card(self.selected_travel_card)

        details_row = QHBoxLayout()
        details_row.setSpacing(14)
        details_row.setAlignment(Qt.AlignTop)
        scaffold.content_layout.addLayout(details_row)

        gauge_card = SectionCard(
            "Focused Key Gauge",
            "Jauge complète de la touche sélectionnée avec points d’actuation, release et fenêtre Rapid Trigger.",
        )
        self.focus_bar = KeyTravelBar(self.session.selected_key)
        gauge_card.body_layout.addWidget(self.focus_bar, 0, Qt.AlignTop)
        details_row.addWidget(gauge_card, 1)

        details_card = SectionCard(
            "Focused Key Thresholds",
            "Les valeurs affichées suivent la touche sélectionnée dans le layout global.",
        )
        self.focus_name = QLabel(key_display_name(self.session.selected_key))
        self.focus_name.setObjectName("CardTitle")
        details_card.body_layout.addWidget(self.focus_name)

        info_row = QHBoxLayout()
        info_row.setSpacing(8)
        self.focus_state = StatusChip("IDLE", "neutral")
        self.focus_distance = QLabel("0.00 mm")
        self.focus_distance.setObjectName("Muted")
        info_row.addWidget(self.focus_state)
        info_row.addWidget(self.focus_distance)
        info_row.addStretch(1)
        details_card.body_layout.addLayout(info_row)

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)
        self.threshold_labels: dict[str, QLabel] = {}
        for row_index, (label_text, attr) in enumerate(
            [
                ("Actuation", "actuation"),
                ("Release", "release"),
                ("RT Activation", "rt_activation"),
                ("RT Press", "rt_press"),
                ("RT Release", "rt_release"),
                ("SOCD Pair", "socd"),
            ]
        ):
            label = QLabel(label_text)
            label.setObjectName("Muted")
            value = QLabel("--")
            grid.addWidget(label, row_index, 0)
            grid.addWidget(value, row_index, 1)
            self.threshold_labels[attr] = value
        details_card.body_layout.addLayout(grid)
        details_card.body_layout.addStretch(1)
        details_row.addWidget(details_card, 1)

        self.status_chip = StatusChip("Travel page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _set_status(self, message: str, level: str = "info") -> None:
        level_map = {"success": "ok", "warning": "warn", "error": "bad", "info": "info"}
        self.status_chip.set_text_and_level(message, level_map.get(level, "neutral"))
        try:
            self.session.set_status(
                message,
                {"success": "ok", "warning": "warn", "error": "danger"}.get(level, "info"),
            )
        except Exception:
            pass

    def _update_live_info(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.live_info.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
        else:
            self.live_info.set_text_and_level("OFF", "neutral")

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        self._update_live_info(enabled, interval_ms)

    def _on_selected_key_changed(self, key_index: int) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(key_index)))
        if key_index not in self._selected_keys:
            self._selected_keys.append(key_index)
        self.focus_bar.set_key_index(key_index)
        self.focus_name.setText(key_display_name(key_index))
        self.focus_chip.set_text_and_level(f"K{key_index + 1:02d}", "info")
        self._refresh_selected_keys_summary()
        self._update_focus_card()

    def _refresh_selected_keys_summary(self) -> None:
        if not self._selected_keys:
            self._selected_keys = [max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))]

        count = len(self._selected_keys)
        self.selected_chip.set_text_and_level(f"{count} key{'s' if count != 1 else ''}", "info")
        labels = [key_display_name(index) for index in self._selected_keys[:6]]
        suffix = "" if count <= 6 else f" … +{count - 6}"
        self.selection_label.setText("Selected travel sensors: " + ", ".join(labels) + suffix)
        self._refresh_selected_travel_cards()

    def add_focused_key(self) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))
        if key_index not in self._selected_keys:
            self._selected_keys.append(key_index)
        self._refresh_selected_keys_summary()
        self._apply_overview()

    def clear_selected_keys(self) -> None:
        self._selected_keys = [max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))]
        self._refresh_selected_keys_summary()
        self._apply_overview()

    def select_all_keys(self) -> None:
        self._selected_keys = list(range(KEY_COUNT))
        self._refresh_selected_keys_summary()
        self._apply_overview()

    def _toggle_selected_key(self, key_index: int) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(key_index)))
        if key_index in self._selected_keys:
            if len(self._selected_keys) > 1:
                self._selected_keys.remove(key_index)
        else:
            self._selected_keys.append(key_index)
        self._refresh_selected_keys_summary()
        self._apply_overview()

    def _load_all_thresholds(self) -> None:
        try:
            settings = self.device.get_all_key_settings() or []
        except Exception as exc:
            self._set_status(f"Threshold load failed: {exc}", "error")
            return

        if not settings:
            self._set_status("Threshold load failed: firmware returned no key settings.", "error")
            return

        for index in range(KEY_COUNT):
            self._thresholds[index] = settings[index] if index < len(settings) else {}

        self._update_focus_card()
        self._set_status("82 key thresholds loaded.", "success")

    def _update_focus_card(self) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))
        thresholds = self._thresholds[key_index] if key_index < len(self._thresholds) else {}
        self.focus_bar.set_thresholds(thresholds)
        distance_mm = self._distances_mm[key_index] if key_index < len(self._distances_mm) else 0.0
        pressed = bool(self._states[key_index]) if key_index < len(self._states) else False
        self.focus_bar.update_state(distance_mm, pressed)
        self.focus_state.set_text_and_level("PRESSED" if pressed else "IDLE", "ok" if pressed else "neutral")
        self.focus_distance.setText(f"{distance_mm:.2f} mm")
        self.threshold_labels["actuation"].setText(f"{thresholds.get('actuation_point_mm', 0):.2f} mm")
        self.threshold_labels["release"].setText(f"{thresholds.get('release_point_mm', 0):.2f} mm")
        self.threshold_labels["rt_activation"].setText(
            f"{thresholds.get('rapid_trigger_activation', 0):.2f} mm"
        )
        self.threshold_labels["rt_press"].setText(f"{thresholds.get('rapid_trigger_press', 0):.2f} mm")
        self.threshold_labels["rt_release"].setText(f"{thresholds.get('rapid_trigger_release', 0):.2f} mm")
        socd_pair = thresholds.get("socd_pair", None)
        self.threshold_labels["socd"].setText(
            "None" if socd_pair is None else f"K{int(socd_pair) + 1:02d}"
        )

    def _apply_overview(self) -> None:
        colors = current_colors()
        pressed_count = 0
        max_distance = 0.0
        max_key = 0
        focused_key = max(0, min(KEY_COUNT - 1, int(self.session.selected_key)))
        selected_lookup = {idx: order for order, idx in enumerate(self._selected_keys)}

        for key_index in range(KEY_COUNT):
            distance = float(self._distances_mm[key_index])
            pressed = bool(self._states[key_index])
            if pressed:
                pressed_count += 1
            if distance >= max_distance:
                max_distance = distance
                max_key = key_index

            ratio = max(0.0, min(1.0, distance / _MAX_MM))
            fill = _mix_colors(colors["surface_muted"], colors["accent_soft"], ratio)
            border = colors["success"] if pressed else colors["border"]
            if key_index in selected_lookup:
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
                border = palette[selected_lookup[key_index] % len(palette)]
            if key_index == focused_key:
                border = colors["accent"]
            if pressed:
                fill = _mix_colors(fill, colors["success"], 0.38)
            tooltip = (
                f"{key_display_name(key_index)}\n"
                f"State: {'PRESSED' if pressed else 'IDLE'}\n"
                f"Travel: {distance:.2f} mm"
            )
            self.layout_view.set_key_state(
                key_index,
                title=key_short_label(key_index),
                subtitle="",
                fill=fill,
                border=border,
                tooltip=tooltip,
            )

        self.pressed_chip.set_text_and_level(f"{pressed_count} / {KEY_COUNT}", "ok" if pressed_count else "neutral")
        selected_distances = [self._distances_mm[index] for index in self._selected_keys]
        avg_selected = sum(selected_distances) / len(selected_distances) if selected_distances else 0.0
        self.overview_hint.setText(
            f"Max travel now: {key_display_name(max_key)} at {max_distance:.2f} mm. "
            f"Selected avg travel: {avg_selected:.2f} mm."
        )

    def _refresh_selected_travel_cards(self) -> None:
        selected = [idx for idx in self._selected_keys if 0 <= idx < KEY_COUNT]
        wanted = set(selected)

        for key_index in list(self._selected_cards):
            if key_index in wanted:
                continue
            card = self._selected_cards.pop(key_index)
            self.selected_travel_grid.removeWidget(card)
            card.deleteLater()

        for order, key_index in enumerate(selected):
            card = self._selected_cards.get(key_index)
            if card is None:
                card = SelectedTravelCard(key_index)
                self._selected_cards[key_index] = card
            else:
                card.set_key_index(key_index)

            thresholds = self._thresholds[key_index] if key_index < len(self._thresholds) else {}
            card.apply_values(
                distance_mm=self._distances_mm[key_index],
                pressed=bool(self._states[key_index]),
                thresholds=thresholds,
            )
            row = order // 2
            col = order % 2
            self.selected_travel_grid.addWidget(card, row, col)

    def _poll(self) -> None:
        try:
            data = self.device.get_key_states() or {}
        except Exception as exc:
            self._set_status(f"Travel poll failed: {exc}", "error")
            return

        states = list(data.get("states") or [])
        distances = list(data.get("distances_mm") or [])
        for key_index in range(KEY_COUNT):
            self._states[key_index] = bool(states[key_index]) if key_index < len(states) else False
            self._distances_mm[key_index] = float(distances[key_index]) if key_index < len(distances) else 0.0

        self._refresh_selected_keys_summary()
        self._apply_overview()
        self._update_focus_card()

    def reload(self) -> None:
        self._load_all_thresholds()
        if self._page_active and self.session.live_enabled:
            self._poll()

    def on_page_activated(self) -> None:
        self._page_active = True
        self._load_all_thresholds()
        if self.session.live_enabled:
            self._poll()

    def on_page_deactivated(self) -> None:
        self._page_active = False

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return
        self._poll()
