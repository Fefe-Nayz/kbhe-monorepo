from __future__ import annotations

try:
    from .theme import current_colors as _current_colors
    from .theme import current_theme_name as _current_theme_name
except ImportError:
    def _current_colors() -> dict[str, str]:  # type: ignore[misc]
        return {
            "surface": "#ffffff",
            "surface_muted": "#f5f8fd",
            "text": "#111827",
            "text_muted": "#6b7280",
            "accent": "#2563eb",
            "accent_soft": "#dbeafe",
            "border": "#e2e8f2",
            "success": "#059669",
            "warning": "#d97706",
            "danger": "#dc2626",
        }

    def _current_theme_name() -> str:  # type: ignore[misc]
        return "light"

from .common import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSize,
    QScrollArea,
    QSizePolicy,
    Signal,
    QToolButton,
    QVBoxLayout,
    QWidget,
    Qt,
)
from ..key_layout import KEY_LAYOUT, KEY_LAYOUT_HEIGHT, KEY_LAYOUT_WIDTH, key_display_name

try:
    from PySide6.QtWidgets import QSlider
except ImportError:
    QSlider = None  # type: ignore


# ---------------------------------------------------------------------------
# SectionCard – the primary content container used on all pages
# ---------------------------------------------------------------------------

class SectionCard(QFrame):
    """A titled card panel used across all pages for consistent visual grouping."""

    def __init__(self, title: str, subtitle: str | None = None, parent=None):
        super().__init__(parent)
        self.setObjectName("SectionCard")

        root = QVBoxLayout(self)
        root.setContentsMargins(18, 16, 18, 18)
        root.setSpacing(0)

        header = QHBoxLayout()
        header.setSpacing(8)
        root.addLayout(header)

        self.title_label = QLabel(title)
        self.title_label.setObjectName("CardTitle")
        header.addWidget(self.title_label, 1)

        self._badge_area = QHBoxLayout()
        self._badge_area.setSpacing(6)
        header.addLayout(self._badge_area)

        if subtitle:
            self.subtitle_label = QLabel(subtitle)
            self.subtitle_label.setObjectName("CardSubtitle")
            self.subtitle_label.setWordWrap(True)
            root.addSpacing(4)
            root.addWidget(self.subtitle_label)
        else:
            self.subtitle_label = None

        root.addSpacing(12)

        self.body = QWidget()
        self.body.setObjectName("SectionCardBody")
        self.body_layout = QVBoxLayout(self.body)
        self.body_layout.setContentsMargins(0, 0, 0, 0)
        self.body_layout.setSpacing(10)
        self.body_layout.setAlignment(Qt.AlignTop)
        root.addWidget(self.body)

    def add_header_widget(self, widget: QWidget) -> None:
        """Add a widget (e.g. a status badge) to the card header row."""
        self._badge_area.addWidget(widget)


# ---------------------------------------------------------------------------
# SubCard – lighter inset card for grouped sub-settings
# ---------------------------------------------------------------------------

class SubCard(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("SubCard")
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(14, 12, 14, 12)
        self._layout.setSpacing(8)

    @property
    def layout(self):  # type: ignore[override]
        return self._layout


# ---------------------------------------------------------------------------
# SliderRow – labelled horizontal slider with live value readout
# ---------------------------------------------------------------------------

class SliderRow(QWidget):
    """A single-row slider control: [Label][──────────────────][value]"""

    def __init__(
        self,
        label: str,
        minimum: float,
        maximum: float,
        default: float,
        *,
        suffix: str = "",
        decimals: int = 1,
        scale: int = 10,
        label_width: int = 160,
        value_width: int = 72,
        parent=None,
    ):
        super().__init__(parent)
        self._scale = scale
        self._decimals = decimals
        self._suffix = suffix

        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(10)

        lbl = QLabel(label)
        lbl.setFixedWidth(label_width)
        lbl.setObjectName("Muted")
        row.addWidget(lbl)

        self.slider = QSlider(Qt.Horizontal)
        self.slider.setRange(int(minimum * scale), int(maximum * scale))
        self.slider.setValue(int(default * scale))
        self.slider.setSingleStep(1)
        row.addWidget(self.slider, 1)

        self.value_label = QLabel()
        self.value_label.setFixedWidth(value_width)
        self.value_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        row.addWidget(self.value_label)

        self.slider.valueChanged.connect(self._on_value_changed)
        self._on_value_changed(self.slider.value())

    def _on_value_changed(self, raw: int) -> None:
        val = raw / self._scale
        self.value_label.setText(f"{val:.{self._decimals}f}{self._suffix}")

    def get_value(self) -> float:
        return self.slider.value() / self._scale

    def set_value(self, value: float, *, block_signals: bool = False) -> None:
        if block_signals:
            self.slider.blockSignals(True)
        self.slider.setValue(int(round(value * self._scale)))
        if block_signals:
            self.slider.blockSignals(False)
            self._on_value_changed(self.slider.value())

    @property
    def valueChanged(self):
        return self.slider.valueChanged


# ---------------------------------------------------------------------------
# FormRow – label + right-aligned control (combobox, spinbox, etc.)
# ---------------------------------------------------------------------------

class FormRow(QWidget):
    def __init__(self, label: str, control: QWidget, label_width: int = 160, parent=None):
        super().__init__(parent)
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(12)

        lbl = QLabel(label)
        lbl.setObjectName("Muted")
        lbl.setFixedWidth(label_width)
        row.addWidget(lbl)

        row.addWidget(control, 1)


# ---------------------------------------------------------------------------
# StatusChip – inline colour-coded status label
# ---------------------------------------------------------------------------

class StatusChip(QLabel):
    """Small pill-shaped coloured status label — drawn with QPainter for reliable rounding."""

    _STYLES = {
        "neutral": ("background:#f1f5f9;color:#475569;",  "background:#252d3d;color:#8b9ab3;"),
        "ok":      ("background:#ecfdf5;color:#059669;",  "background:#0d2e20;color:#4ade80;"),
        "warn":    ("background:#fffbeb;color:#d97706;",  "background:#2a1a04;color:#fbbf24;"),
        "bad":     ("background:#fef2f2;color:#dc2626;",  "background:#250808;color:#f87171;"),
        "info":    ("background:#eff6ff;color:#1d4ed8;",  "background:#162035;color:#60a5fa;"),
    }

    @staticmethod
    def _parse_pair(style_str: str) -> tuple[str, str]:
        """Extract (background_hex, color_hex) from 'background:X;color:Y;' string."""
        bg, fg = "#f1f5f9", "#475569"
        for part in style_str.rstrip(";").split(";"):
            k, _, v = part.strip().partition(":")
            k = k.strip(); v = v.strip()
            if k == "background":
                bg = v
            elif k == "color":
                fg = v
        return bg, fg

    def __init__(self, text: str = "", level: str = "neutral", parent=None):
        super().__init__(text, parent)
        self._level = level
        try:
            from PySide6.QtGui import QColor as _QC
            self._bg_color = _QC("#f1f5f9")
            self._text_color = _QC("#475569")
        except Exception:
            self._bg_color = None
            self._text_color = None
        # Ensure adequate padding via stylesheet (no background — we paint it)
        self.setStyleSheet("padding: 4px 10px; font-size: 9pt; font-weight: 600; background: transparent;")
        self._apply()

    def set_level(self, level: str) -> None:
        self._level = level
        self._apply()

    def set_text_and_level(self, text: str, level: str) -> None:
        self.setText(text)
        self.set_level(level)

    def changeEvent(self, event) -> None:  # type: ignore[override]
        super().changeEvent(event)
        try:
            from PySide6.QtCore import QEvent
            if event.type() == QEvent.Type.StyleChange:
                self._apply()
        except Exception:
            pass

    def _apply(self) -> None:
        try:
            from PySide6.QtGui import QColor as _QC
        except ImportError:
            return
        pair = self._STYLES.get(self._level, self._STYLES["neutral"])
        is_dark = _current_theme_name() == "dark"
        bg_hex, fg_hex = self._parse_pair(pair[1 if is_dark else 0])
        new_bg = _QC(bg_hex)
        new_fg = _QC(fg_hex)
        if new_bg == self._bg_color and new_fg == self._text_color:
            return
        self._bg_color = new_bg
        self._text_color = new_fg
        self.update()

    def paintEvent(self, event) -> None:  # type: ignore[override]
        if self._bg_color is None:
            super().paintEvent(event)
            return
        try:
            from PySide6.QtGui import QPainter, QPainterPath, QBrush
            from PySide6.QtCore import QRectF
        except ImportError:
            super().paintEvent(event)
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        radius = rect.height() / 2.0
        path = QPainterPath()
        path.addRoundedRect(rect, radius, radius)
        painter.fillPath(path, QBrush(self._bg_color))
        painter.setPen(self._text_color)
        painter.setFont(self.font())
        painter.drawText(self.rect(), Qt.AlignCenter, self.text())


# ---------------------------------------------------------------------------
# StatusPill – the main app status bar widget
# ---------------------------------------------------------------------------

class StatusPill(QLabel):
    def __init__(self, text: str = "", parent=None):
        super().__init__(text, parent)
        self.setObjectName("StatusPill")
        self.set_level("info")

    def set_level(self, level: str) -> None:
        normalized = {
            "success": "ok", "ok": "ok",
            "warning": "warn", "warn": "warn",
            "error": "danger", "danger": "danger",
        }.get(level, "info")
        self.setProperty("level", normalized)
        self.style().unpolish(self)
        self.style().polish(self)


# ---------------------------------------------------------------------------
# KeySelector – shared spatial keyboard picker
# ---------------------------------------------------------------------------

class KeySelector(QWidget):
    _UNIT = 24
    _GAP = 4

    def __init__(self, session, layout_mode: str = "row", parent=None):
        super().__init__(parent)
        self.session = session
        self.buttons: list[QPushButton] = []

        canvas = QWidget(self)
        canvas.setObjectName("KeySelectorCanvas")
        canvas.setMinimumSize(
            int(KEY_LAYOUT_WIDTH * self._UNIT) + self._GAP,
            int(KEY_LAYOUT_HEIGHT * self._UNIT) + self._GAP,
        )
        self._canvas = canvas
        self.setMinimumSize(canvas.minimumSize())

        for entry in KEY_LAYOUT:
            btn = QPushButton(entry.short_label, canvas)
            btn.setObjectName("KeyChip")
            btn.setToolTip(key_display_name(entry.index))
            btn.clicked.connect(
                lambda _c=False, idx=entry.index: session.set_selected_key(idx)
            )
            self.buttons.append(btn)

        session.selectedKeyChanged.connect(self.refresh)
        self.refresh(session.selected_key)

    def resizeEvent(self, event) -> None:  # type: ignore[override]
        super().resizeEvent(event)
        self._canvas.setGeometry(self.rect())
        for entry, btn in zip(KEY_LAYOUT, self.buttons):
            x = int(entry.x * self._UNIT)
            y = int(entry.y * self._UNIT)
            w = max(26, int(entry.w * self._UNIT) - self._GAP)
            h = max(24, int(entry.h * self._UNIT) - self._GAP)
            btn.setGeometry(x, y, w, h)

    def refresh(self, selected: int) -> None:
        for i, btn in enumerate(self.buttons):
            btn.setProperty("active", i == selected)
            btn.style().unpolish(btn)
            btn.style().polish(btn)


class KeyboardLayoutWidget(QWidget):
    keyClicked = Signal(int)

    def __init__(
        self,
        session=None,
        *,
        unit: int = 34,
        gap: int = 4,
        interactive: bool = True,
        parent=None,
    ):
        super().__init__(parent)
        self.session = session
        self._unit = int(unit)
        self._gap = int(gap)
        self._interactive = bool(interactive)
        self._selected_index = -1
        self._state: list[dict[str, object]] = []
        self.buttons: list[QPushButton] = []

        self._canvas = QWidget(self)
        self._canvas.setObjectName("KeyboardLayoutCanvas")
        fixed_size = QSize(
            int(KEY_LAYOUT_WIDTH * self._unit) + self._gap,
            int(KEY_LAYOUT_HEIGHT * self._unit) + self._gap,
        )
        self._canvas.setMinimumSize(fixed_size)
        self.setMinimumSize(fixed_size)
        self.setMaximumSize(fixed_size)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)

        for entry in KEY_LAYOUT:
            btn = QPushButton(self._compose_text(entry.short_label, ""), self._canvas)
            btn.setObjectName("KeyLayoutButton")
            btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            btn.clicked.connect(lambda _c=False, idx=entry.index: self._on_button_clicked(idx))
            self.buttons.append(btn)
            self._state.append(
                {
                    "title": entry.short_label,
                    "subtitle": "",
                    "fill": None,
                    "text": None,
                    "border": None,
                    "tooltip": key_display_name(entry.index),
                }
            )

        if self.session is not None and hasattr(self.session, "selectedKeyChanged"):
            try:
                self.session.selectedKeyChanged.connect(self.set_selected_key)
                self._selected_index = int(getattr(self.session, "selected_key", -1))
            except Exception:
                pass

        self.set_interactive(self._interactive)
        self.refresh()

    @staticmethod
    def _compose_text(title: str, subtitle: str) -> str:
        return title if not subtitle else f"{title}\n{subtitle}"

    @staticmethod
    def _auto_text_color(fill: str, fallback: str) -> str:
        try:
            hex_color = fill.lstrip("#")
            if len(hex_color) != 6:
                return fallback
            r = int(hex_color[0:2], 16)
            g = int(hex_color[2:4], 16)
            b = int(hex_color[4:6], 16)
            luminance = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
            return "#0f172a" if luminance >= 0.62 else "#f8fafc"
        except Exception:
            return fallback

    def resizeEvent(self, event) -> None:  # type: ignore[override]
        super().resizeEvent(event)
        self._canvas.setGeometry(self.rect())
        for entry, btn in zip(KEY_LAYOUT, self.buttons):
            x = int(entry.x * self._unit)
            y = int(entry.y * self._unit)
            w = max(28, int(entry.w * self._unit) - self._gap)
            h = max(26, int(entry.h * self._unit) - self._gap)
            btn.setGeometry(x, y, w, h)

    def _on_button_clicked(self, index: int) -> None:
        if not self._interactive:
            return
        if self.session is not None and hasattr(self.session, "set_selected_key"):
            try:
                self.session.set_selected_key(index)
            except Exception:
                pass
        else:
            self.set_selected_key(index)
        self.keyClicked.emit(index)

    def set_interactive(self, interactive: bool) -> None:
        self._interactive = bool(interactive)
        for button in self.buttons:
            button.setEnabled(True)
            button.setCursor(
                Qt.CursorShape.PointingHandCursor
                if self._interactive
                else Qt.CursorShape.ArrowCursor
            )
        self.refresh()

    def set_selected_key(self, index: int) -> None:
        self._selected_index = int(index)
        self.refresh()

    def set_key_state(
        self,
        index: int,
        *,
        title: str | None = None,
        subtitle: str | None = None,
        fill: str | None = None,
        text: str | None = None,
        border: str | None = None,
        tooltip: str | None = None,
    ) -> None:
        if not (0 <= index < len(self._state)):
            return
        state = self._state[index]
        if title is not None:
            state["title"] = title
        if subtitle is not None:
            state["subtitle"] = subtitle
        if fill is not None:
            state["fill"] = fill
        if text is not None:
            state["text"] = text
        if border is not None:
            state["border"] = border
        if tooltip is not None:
            state["tooltip"] = tooltip
        self._apply_button(index)

    def reset(self) -> None:
        for entry in KEY_LAYOUT:
            self._state[entry.index] = {
                "title": entry.short_label,
                "subtitle": "",
                "fill": None,
                "text": None,
                "border": None,
                "tooltip": key_display_name(entry.index),
            }
        self.refresh()

    def _apply_button(self, index: int) -> None:
        colors = _current_colors()
        state = self._state[index]
        fill = str(state.get("fill") or colors["surface"])
        border = str(state.get("border") or colors["border"])
        text = str(state.get("text") or self._auto_text_color(fill, colors["text"]))
        selected = index == self._selected_index
        if selected:
            border = colors["accent"]
        button = self.buttons[index]
        title = str(state.get("title") or KEY_LAYOUT[index].short_label)
        subtitle = str(state.get("subtitle") or "")
        button.setText(self._compose_text(title, subtitle))
        button.setToolTip(str(state.get("tooltip") or key_display_name(index)))
        button.setStyleSheet(
            "QPushButton {"
            f"background: {fill};"
            f"color: {text};"
            f"border: {'2px' if selected else '1px'} solid {border};"
            "border-radius: 10px;"
            "padding: 2px 4px;"
            "font-weight: 600;"
            "font-size: 8.5pt;"
            "text-align: center;"
            "}"
            "QPushButton:disabled {"
            f"background: {fill};"
            f"color: {text};"
            f"border: {'2px' if selected else '1px'} solid {border};"
            "}"
        )

    def refresh(self) -> None:
        for index in range(len(self.buttons)):
            self._apply_button(index)


# ---------------------------------------------------------------------------
# PageScaffold – top-level page container with hero header + scrollable content
# ---------------------------------------------------------------------------

class PageScaffold(QWidget):
    """Standard page layout: title bar at top, scrollable cards below."""

    def __init__(
        self,
        title: str,
        subtitle: str = "",
        parent=None,
    ):
        super().__init__(parent)

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ── Scrollable content area ────────────────────────
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        root.addWidget(scroll, 1)

        container = QWidget()
        container_layout = QVBoxLayout(container)
        container_layout.setContentsMargins(20, 20, 20, 20)
        container_layout.setSpacing(14)
        scroll.setWidget(container)

        # Page title within scroll (so it scrolls with content)
        if title:
            title_label = QLabel(title)
            title_label.setObjectName("CardTitle")
            title_label.setStyleSheet(
                "font-size: 18pt; font-weight: 700; margin-bottom: 2px;"
            )
            container_layout.addWidget(title_label)
        if subtitle:
            sub = QLabel(subtitle)
            sub.setObjectName("Muted")
            sub.setWordWrap(True)
            container_layout.addWidget(sub)

        if title or subtitle:
            container_layout.addSpacing(4)

        self.content_layout = container_layout

    def add_card(self, card: QWidget) -> None:
        self.content_layout.addWidget(card)

    def add_stretch(self) -> None:
        self.content_layout.addStretch(1)


# ---------------------------------------------------------------------------
# Convenience button factories
# ---------------------------------------------------------------------------

def make_primary_button(text: str, slot=None) -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("PrimaryButton")
    if slot is not None:
        btn.clicked.connect(slot)
    return btn


def make_secondary_button(text: str, slot=None) -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("SecondaryButton")
    if slot is not None:
        btn.clicked.connect(slot)
    return btn


def make_danger_button(text: str, slot=None) -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("DangerButton")
    if slot is not None:
        btn.clicked.connect(slot)
    return btn


def make_tool_button(text: str, slot=None) -> QToolButton:
    btn = QToolButton()
    btn.setText(text)
    btn.setObjectName("SecondaryButton")
    if slot is not None:
        btn.clicked.connect(slot)
    return btn


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def hline() -> QFrame:
    """A thin horizontal divider."""
    line = QFrame()
    line.setFrameShape(QFrame.HLine)
    line.setFrameShadow(QFrame.Sunken)
    return line
