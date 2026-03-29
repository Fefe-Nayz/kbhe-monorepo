from __future__ import annotations

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QBrush, QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QSpinBox,
    QSizePolicy,
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

_MAX_MM = 4.0
_GRID_STEP = 0.5  # mm between horizontal grid lines


# ---------------------------------------------------------------------------
# KeyTravelBar – one vertical gauge per key
# ---------------------------------------------------------------------------

class KeyTravelBar(QWidget):
    """
    A vertical bar showing live key travel with threshold marker lines.

    Scale: bottom = 0 mm (resting), top = 4 mm (fully pressed).
    Fill rises upward as the key is pressed.
    """

    def __init__(self, key_index: int, parent=None):
        super().__init__(parent)
        self.key_index = key_index
        self._distance_mm = 0.0
        self._state = False
        self._thresholds: dict = {}
        self._min_top_mm: float | None = None
        self._max_bottom_mm: float | None = None
        self._prev_state: bool | None = None
        self.setMinimumSize(90, 300)
        sp = QSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setSizePolicy(sp)

    def set_thresholds(self, t: dict) -> None:
        self._thresholds = dict(t)
        self.update()

    def update_state(self, distance_mm: float, state: bool) -> None:
        if self._prev_state != state:
            # State transition — reset dynamic RT window
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

    # ------------------------------------------------------------------

    def _mm_to_y(self, mm: float, y0: int, y1: int) -> float:
        """Map travel (0 = resting, MAX = fully pressed) to pixel Y.
        y0 = top pixel (MAX mm), y1 = bottom pixel (0 mm).
        """
        ratio = max(0.0, min(1.0, mm / _MAX_MM))
        return y1 - ratio * (y1 - y0)

    def paintEvent(self, _event) -> None:
        c = current_colors()
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        W = self.width()
        H = self.height()

        # Layout metrics
        label_w = 34   # mm scale on the left
        margin_r = 8
        margin_top = 26
        margin_bot = 38

        bar_x0 = label_w
        bar_x1 = W - margin_r
        bar_y0 = margin_top          # top pixel (MAX mm)
        bar_y1 = H - margin_bot      # bottom pixel (0 mm)
        bar_w = max(4, bar_x1 - bar_x0)
        bar_h = max(4, bar_y1 - bar_y0)

        # ── Background ───────────────────────────────────────────────
        painter.fillRect(self.rect(), QColor(c["surface"]))

        # ── Scale grid + labels ──────────────────────────────────────
        small_font = QFont()
        small_font.setPointSize(7)
        painter.setFont(small_font)
        painter.setPen(QPen(QColor(c["border"]), 1))
        t = 0.0
        while t <= _MAX_MM + 1e-9:
            y = int(self._mm_to_y(t, bar_y0, bar_y1))
            painter.setPen(QPen(QColor(c["border"]), 1))
            painter.drawLine(bar_x0 - 6, y, bar_x1, y)
            painter.setPen(QColor(c["text_muted"]))
            painter.drawText(0, y - 7, label_w - 8, 14,
                             Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                             f"{t:.1f}")
            t = round(t + _GRID_STEP, 6)

        # ── Gauge background ─────────────────────────────────────────
        painter.setPen(QPen(QColor(c["border"]), 1.5))
        painter.setBrush(QBrush(QColor(c["surface_muted"])))
        painter.drawRect(bar_x0, bar_y0, bar_w, bar_h)

        # ── Travel fill (bottom-anchored, grows upward on press) ─────
        y_travel = int(self._mm_to_y(self._distance_mm, bar_y0, bar_y1))
        fill_color = QColor("#00c463" if self._state else c["accent"])
        fill_color.setAlpha(160)
        press_ratio = max(0.0, min(1.0, self._distance_mm / _MAX_MM))
        fill_height = int((bar_h - 2) * press_ratio)
        if fill_height > 0:
            painter.fillRect(bar_x0 + 1, bar_y1 - fill_height, bar_w - 2, fill_height, fill_color)

        # ── Threshold lines ───────────────────────────────────────────
        th = self._thresholds
        act_mm = float(th.get("actuation_point_mm", 1.5))
        rel_mm = float(th.get("release_point_mm", 1.5))
        rt_enabled = bool(th.get("rapid_trigger_enabled", False))
        rt_press = float(th.get("rapid_trigger_press", 0.3))
        rt_release = float(th.get("rapid_trigger_release", 0.3))

        def draw_hline(mm_val: float, hex_color: str, dashed: bool = False) -> None:
            y = int(self._mm_to_y(mm_val, bar_y0, bar_y1))
            pen = QPen(QColor(hex_color), 2)
            if dashed:
                pen.setStyle(Qt.PenStyle.DashLine)
            painter.setPen(pen)
            painter.drawLine(bar_x0 - 4, y, bar_x1 + 2, y)

        draw_hline(act_mm, "#00d26a")
        draw_hline(rel_mm, "#ffb000")

        if rt_enabled and self._min_top_mm is not None and self._max_bottom_mm is not None:
            rt_press_line = self._min_top_mm + rt_press
            rt_rel_line = self._max_bottom_mm - rt_release
            if 0.0 <= rt_press_line <= _MAX_MM:
                draw_hline(rt_press_line, "#6cd4ff", dashed=True)
            if 0.0 <= rt_rel_line <= _MAX_MM:
                draw_hline(rt_rel_line, "#ff7cff", dashed=True)

        # ── Current position marker ───────────────────────────────────
        y_cur = int(self._mm_to_y(self._distance_mm, bar_y0, bar_y1))
        painter.setPen(QPen(QColor("white"), 2))
        painter.drawLine(bar_x0 - 2, y_cur, bar_x1 + 2, y_cur)

        # ── Key label (top) ───────────────────────────────────────────
        title_font = QFont()
        title_font.setPointSize(9)
        title_font.setBold(True)
        painter.setFont(title_font)
        painter.setPen(QColor(c["text"]))
        painter.drawText(0, 0, W, margin_top,
                         Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter,
                         f"K{self.key_index + 1}")

        # ── State + distance (bottom) ─────────────────────────────────
        painter.setFont(small_font)
        painter.setPen(QColor(c["text_muted"]))
        state_str = "PRESSED" if self._state else "IDLE"
        mm_str = f"{self._distance_mm:.2f} mm"
        painter.drawText(0, bar_y1 + 4, W, margin_bot - 4,
                         Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop,
                         f"{state_str}\n{mm_str}")


# ---------------------------------------------------------------------------
# TravelPage
# ---------------------------------------------------------------------------

class TravelPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        self._poll_timer = QTimer(self)
        self._poll_timer.timeout.connect(self._poll)
        self._build_ui()
        self._load_all_thresholds()

    # ------------------------------------------------------------------
    # UI
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Key Travel",
            "Live visualization of key travel distance and rapid-trigger threshold windows. "
            "Green = actuation · Orange = release · Dashed cyan = RT press · Dashed pink = RT release.",
        )
        root.addWidget(scaffold, 1)

        # ── Controls card ─────────────────────────────────────────────
        ctrl_card = SectionCard("Live Controls")
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(14)

        self.live_check = QCheckBox("Live preview")
        self.live_check.toggled.connect(self._on_live_toggled)
        ctrl_row.addWidget(self.live_check)

        interval_lbl = QLabel("Interval (ms):")
        interval_lbl.setObjectName("Muted")
        ctrl_row.addWidget(interval_lbl)

        self.interval_spin = QSpinBox()
        self.interval_spin.setRange(16, 500)
        self.interval_spin.setValue(33)
        self.interval_spin.valueChanged.connect(self._on_interval_changed)
        ctrl_row.addWidget(self.interval_spin)

        ctrl_row.addStretch(1)
        ctrl_row.addWidget(make_secondary_button("Reload Thresholds", self._load_all_thresholds))

        ctrl_card.body_layout.addLayout(ctrl_row)

        # Legend
        legend_row = QHBoxLayout()
        legend_row.setSpacing(16)
        legend_row.addStretch(1)
        for color, label in [
            ("#00d26a", "Actuation"),
            ("#ffb000", "Release"),
            ("#6cd4ff", "RT Press (dyn.)"),
            ("#ff7cff", "RT Release (dyn.)"),
        ]:
            dot = QLabel("━")
            dot.setStyleSheet(f"color: {color}; font-size: 12pt;")
            legend_row.addWidget(dot)
            lbl = QLabel(label)
            lbl.setObjectName("Muted")
            legend_row.addWidget(lbl)
        ctrl_card.body_layout.addLayout(legend_row)
        scaffold.add_card(ctrl_card)

        # ── Travel bars card ──────────────────────────────────────────
        bars_card = SectionCard(
            "Travel Gauges",
            "Each bar shows 0 mm (bottom) → 4 mm (top). Fill rises as the key is pressed.",
        )
        bars_row = QHBoxLayout()
        bars_row.setSpacing(10)
        self.bars: list[KeyTravelBar] = []
        for i in range(6):
            bar = KeyTravelBar(i)
            bar.setMinimumHeight(420)
            bars_row.addWidget(bar)
            self.bars.append(bar)
        bars_card.body_layout.addLayout(bars_row)
        scaffold.add_card(bars_card)

        # ── Status ────────────────────────────────────────────────────
        self.status_chip = StatusChip("Travel page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

    # ------------------------------------------------------------------
    # Data
    # ------------------------------------------------------------------

    def _load_all_thresholds(self) -> None:
        errors = []
        for i, bar in enumerate(self.bars):
            try:
                t = self.device.get_key_settings(i)
                if t:
                    bar.set_thresholds(t)
            except Exception as exc:
                errors.append(f"K{i+1}: {exc}")
        if errors:
            self._set_status("Threshold load warnings: " + "; ".join(errors), "warn")
        else:
            self._set_status("Thresholds loaded.", "ok")

    def _poll(self) -> None:
        try:
            data = self.device.get_key_states()
        except Exception as exc:
            self._set_status(f"Poll error: {exc}", "bad")
            self._poll_timer.stop()
            return
        if not data:
            return
        states = data.get("states") or [False] * 6
        distances_mm = data.get("distances_mm") or [0.0] * 6
        for i, bar in enumerate(self.bars):
            d = float(distances_mm[i]) if i < len(distances_mm) else 0.0
            s = bool(states[i]) if i < len(states) else False
            bar.update_state(d, s)

    def _set_status(self, msg: str, level: str = "info") -> None:
        self.status_chip.set_text_and_level(msg, level)

    # ------------------------------------------------------------------
    # Live toggle
    # ------------------------------------------------------------------

    def _on_live_toggled(self, checked: bool) -> None:
        if checked and self._page_active:
            self._poll_timer.start(self.interval_spin.value())
            self._poll()
        else:
            self._poll_timer.stop()

    def _on_interval_changed(self, value: int) -> None:
        if self._poll_timer.isActive():
            self._poll_timer.setInterval(value)

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def reload(self) -> None:
        self._load_all_thresholds()

    def on_page_activated(self) -> None:
        self._page_active = True
        self._load_all_thresholds()
        if self.live_check.isChecked():
            self._poll_timer.start(self.interval_spin.value())

    def on_page_deactivated(self) -> None:
        self._page_active = False
        self._poll_timer.stop()
