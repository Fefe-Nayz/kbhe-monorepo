from __future__ import annotations

try:
    from .theme import current_theme_name as _current_theme_name
except ImportError:
    def _current_theme_name() -> str:  # type: ignore[misc]
        return "light"

from .common import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QScrollArea,
    QToolButton,
    QVBoxLayout,
    QWidget,
    Qt,
)

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
# KeySelector – 6-button shared focused-key picker
# ---------------------------------------------------------------------------

class KeySelector(QWidget):
    def __init__(self, session, layout_mode: str = "row", parent=None):
        super().__init__(parent)
        self.session = session
        self.buttons: list[QPushButton] = []

        if layout_mode == "column":
            outer = QVBoxLayout(self)
        elif layout_mode == "grid":
            outer = QGridLayout(self)
        else:
            outer = QHBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(6)

        for i in range(6):
            btn = QPushButton(f"K{i + 1}")
            btn.setObjectName("KeyChip")
            btn.setFixedWidth(44)
            btn.clicked.connect(lambda _c=False, idx=i: session.set_selected_key(idx))
            if layout_mode == "grid":
                outer.addWidget(btn, i // 3, i % 3)
            else:
                outer.addWidget(btn)
            self.buttons.append(btn)

        session.selectedKeyChanged.connect(self.refresh)
        self.refresh(session.selected_key)

    def refresh(self, selected: int) -> None:
        for i, btn in enumerate(self.buttons):
            btn.setProperty("active", i == selected)
            btn.style().unpolish(btn)
            btn.style().polish(btn)


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
