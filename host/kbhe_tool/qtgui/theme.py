from __future__ import annotations

import base64
import pathlib
import sys
import tempfile

from .common import HAS_GUI, QApplication

try:
    import winreg
except ImportError:
    winreg = None

# ---------------------------------------------------------------------------
# Colour palettes
# ---------------------------------------------------------------------------


LIGHT_COLORS: dict[str, str] = {
    # App chrome
    "bg":               "#f0f3f8",
    "surface":          "#ffffff",
    "surface_alt":      "#eef2f8",
    "surface_muted":    "#f5f8fd",
    "surface_emphasis": "#e8edf6",
    # Text
    "text":             "#111827",
    "text_muted":       "#6b7280",
    # Accent
    "accent":           "#2563eb",
    "accent_hover":     "#1d4ed8",
    "accent_soft":      "#dbeafe",
    "accent_text":      "#1e40af",
    # Semantic
    "success":          "#059669",
    "success_bg":       "#d1fae5",
    "warning":          "#d97706",
    "warning_bg":       "#fef3c7",
    "danger":           "#dc2626",
    "danger_bg":        "#fee2e2",
    "danger_border":    "#fca5a5",
    # Borders / dividers
    "border":           "#e2e8f2",
    "border_strong":    "#c5d1e0",
    # Input fields
    "input_bg":         "#ffffff",
    # Scrollbar
    "scrollbar":        "#cbd5e1",
    "scrollbar_hover":  "#94a3b8",
    # Graph channels
    "graph_1": "#2563eb",
    "graph_2": "#16a34a",
    "graph_3": "#f97316",
    "graph_4": "#9333ea",
    "graph_5": "#dc2626",
    "graph_6": "#0891b2",
    # Sidebar (light-adapted)
    "sidebar_bg":          "#e8edf7",
    "sidebar_border":      "#d1daea",
    "sidebar_text":        "#1e2f46",
    "sidebar_muted":       "#7b8ea8",
    "sidebar_group":       "#8a9db8",
    "sidebar_hover_bg":    "#dde4f0",
    "sidebar_active_bg":   "#d3e4fc",
    "sidebar_active_text": "#1d4ed8",
    "sidebar_active_bar":  "#2563eb",
}

DARK_COLORS: dict[str, str] = {
    # App chrome
    "bg":               "#0f1117",
    "surface":          "#1a1e2a",
    "surface_alt":      "#232836",
    "surface_muted":    "#161a23",
    "surface_emphasis": "#1e2435",
    # Text
    "text":             "#f1f5f9",
    "text_muted":       "#8b9ab3",
    # Accent
    "accent":           "#3b82f6",
    "accent_hover":     "#2563eb",
    "accent_soft":      "#172554",
    "accent_text":      "#93c5fd",
    # Semantic
    "success":          "#34d399",
    "success_bg":       "#064e3b",
    "warning":          "#fbbf24",
    "warning_bg":       "#451a03",
    "danger":           "#f87171",
    "danger_bg":        "#450a0a",
    "danger_border":    "#7f1d1d",
    # Borders / dividers
    "border":           "#2a3245",
    "border_strong":    "#374256",
    # Input fields
    "input_bg":         "#1e2435",
    # Scrollbar
    "scrollbar":        "#374151",
    "scrollbar_hover":  "#4b5563",
    # Graph channels
    "graph_1": "#60a5fa",
    "graph_2": "#4ade80",
    "graph_3": "#fb923c",
    "graph_4": "#c084fc",
    "graph_5": "#f87171",
    "graph_6": "#22d3ee",
    # Sidebar (slightly different shade in dark mode)
    "sidebar_bg":       "#0d1018",
    "sidebar_border":   "#1c2233",
    "sidebar_text":     "#dce3ef",
    "sidebar_muted":    "#4e5e78",
    "sidebar_group":    "#3a4a62",
    "sidebar_hover_bg": "#151b2a",
    "sidebar_active_bg": "#162040",
    "sidebar_active_text": "#60a5fa",
    "sidebar_active_bar":  "#3b82f6",
}

THEMES: dict[str, dict[str, str]] = {"light": LIGHT_COLORS, "dark": DARK_COLORS}

_ACTIVE_THEME_MODE = "system"
_ACTIVE_THEME_NAME = "light"
_ACTIVE_COLORS: dict[str, str] = LIGHT_COLORS


# ---------------------------------------------------------------------------
# Live colour proxy – pages can keep a reference and always read the right values
# ---------------------------------------------------------------------------

class _ColorProxy(dict):
    def __getitem__(self, key):
        return current_colors()[key]

    def get(self, key, default=None):
        return current_colors().get(key, default)

    def keys(self):
        return current_colors().keys()

    def items(self):
        return current_colors().items()

    def values(self):
        return current_colors().values()


COLORS = _ColorProxy()


# ---------------------------------------------------------------------------
# Theme detection & resolution helpers
# ---------------------------------------------------------------------------

def detect_system_theme() -> str:
    if sys.platform != "win32" or winreg is None:
        return "light"
    key_path = r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
            value, _ = winreg.QueryValueEx(key, "AppsUseLightTheme")
        return "light" if int(value) else "dark"
    except OSError:
        return "light"


def resolve_theme_name(theme_mode: str | None = None) -> str:
    mode = (theme_mode or _ACTIVE_THEME_MODE or "system").lower()
    if mode == "system":
        return detect_system_theme()
    return mode if mode in THEMES else "light"


def current_theme_mode() -> str:
    return _ACTIVE_THEME_MODE


def current_theme_name() -> str:
    return _ACTIVE_THEME_NAME


def current_colors() -> dict[str, str]:
    return _ACTIVE_COLORS


# ---------------------------------------------------------------------------
# Pre-rendered PNG arrows (base64 RGBA, transparent bg)
# Light: #6b7280 (slate-500)   Dark: #8b9ab3 (slate-400/blue tint)
# Generated via Python struct+zlib — no external dependencies
# ---------------------------------------------------------------------------
_PNG_CHEVRON_L  = "iVBORw0KGgoAAAANSUhEUgAAAAoAAAAGCAYAAAD68A/GAAAAJ0lEQVR42mPILmr4z0AAwNXgU4wiB+JgU4xVHF0Ql2YUSbyKCJkEADdtKnniyRxoAAAAAElFTkSuQmCC"
_PNG_ARROW_UP_L = "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAFCAYAAAB4ka1VAAAAJElEQVR42mNgQALZRQ3/QZgBG4BJYlWELoihCJuxcDGcdkLlADWQIQlxzDFJAAAAAElFTkSuQmCC"
_PNG_ARROW_DN_L = "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAFCAYAAAB4ka1VAAAAIklEQVR42mPILmr4z4ADwOWwKUIRA3GQBdD5KIJYJfHpBAD+AiEJs1NhCAAAAABJRU5ErkJggg=="
_PNG_CHEVRON_D  = "iVBORw0KGgoAAAANSUhEUgAAAAoAAAAGCAYAAAD68A/GAAAAJ0lEQVR42mPonrX5PwMBAFeDTzGKHIiDTTFWcXRBXJpRJPEqImQSACdPMx98nHNZAAAAAElFTkSuQmCC"
_PNG_ARROW_UP_D = "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAFCAYAAAB4ka1VAAAAJUlEQVR42mNgQALdszb/B2EGbAAmiVURuiCGImzGwsVw2gmVAwCCqCfDNfvmDAAAAABJRU5ErkJggg=="
_PNG_ARROW_DN_D = "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAFCAYAAAB4ka1VAAAAIklEQVR42mPonrX5PwMOAJfDpghFDMRBFkDnowhilcSnEwALvSfDwitHQgAAAABJRU5ErkJggg=="

# Decoded PNG files written to a temp dir once per process (Qt QSS needs file
# paths — data: URIs are not supported for ::down-arrow / ::up-arrow images)
_ARROW_PATHS: dict[str, str] = {}


def _ensure_arrows() -> dict[str, str]:
    global _ARROW_PATHS
    if _ARROW_PATHS:
        return _ARROW_PATHS
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="kbhe_arrows_"))
    for name, b64 in [
        ("chevron_l",  _PNG_CHEVRON_L),
        ("chevron_d",  _PNG_CHEVRON_D),
        ("arrow_up_l", _PNG_ARROW_UP_L),
        ("arrow_up_d", _PNG_ARROW_UP_D),
        ("arrow_dn_l", _PNG_ARROW_DN_L),
        ("arrow_dn_d", _PNG_ARROW_DN_D),
    ]:
        p = tmp / f"{name}.png"
        p.write_bytes(base64.b64decode(b64))
        _ARROW_PATHS[name] = str(p).replace("\\", "/")
    return _ARROW_PATHS


# ---------------------------------------------------------------------------
# Stylesheet builders
# ---------------------------------------------------------------------------

def _is_light(c: dict[str, str]) -> bool:
    return c is LIGHT_COLORS or c.get("bg") == LIGHT_COLORS["bg"]


def build_app_style_sheet(c: dict[str, str]) -> str:
    light = _is_light(c)
    status_ok_bg    = "#ecfdf5" if light else c["success_bg"]
    status_warn_bg  = "#fffbeb" if light else c["warning_bg"]
    status_err_bg   = "#fef2f2" if light else c["danger_bg"]

    _arrows = _ensure_arrows()
    _sfx = "l" if light else "d"
    chevron_down = f"url({_arrows[f'chevron_{_sfx}']})"
    arrow_up     = f"url({_arrows[f'arrow_up_{_sfx}']})"
    arrow_down   = f"url({_arrows[f'arrow_dn_{_sfx}']})"

    return f"""
/* ── Global reset ─────────────────────────────────────────── */
QMainWindow, QDialog {{
    background: {c["bg"]};
    color: {c["text"]};
    font-family: "Segoe UI", system-ui, sans-serif;
    font-size: 10pt;
}}
QWidget {{
    color: {c["text"]};
    font-family: "Segoe UI", system-ui, sans-serif;
    font-size: 10pt;
}}
QLabel, QCheckBox, QRadioButton {{
    background: transparent;
}}

/* ── Shell chrome ──────────────────────────────────────────── */
QFrame#ShellRoot {{
    background: {c["bg"]};
}}

/* ── Sidebar ───────────────────────────────────────────────── */
QFrame#Sidebar {{
    background: {c["sidebar_bg"]};
    border-right: 1px solid {c["sidebar_border"]};
}}
QLabel#SidebarTitle {{
    color: {c["sidebar_text"]};
    font-size: 16pt;
    font-weight: 700;
    letter-spacing: -0.3px;
}}
QLabel#SidebarVersion {{
    color: {c["sidebar_muted"]};
    font-size: 8pt;
}}
QLabel#SidebarGroupLabel {{
    color: {c["sidebar_group"]};
    font-size: 8pt;
    font-weight: 700;
    letter-spacing: 0.8px;
    text-transform: uppercase;
    padding: 0px 6px;
}}
QPushButton#NavButton {{
    background: transparent;
    color: {c["sidebar_text"]};
    border: none;
    border-radius: 8px;
    padding: 9px 12px 9px 14px;
    text-align: left;
    font-size: 10pt;
    font-weight: 500;
}}
QPushButton#NavButton:hover {{
    background: {c["sidebar_hover_bg"]};
}}
QPushButton#NavButton[active="true"] {{
    background: {c["sidebar_active_bg"]};
    color: {c["sidebar_active_text"]};
    font-weight: 600;
    border-left: 3px solid {c["sidebar_active_bar"]};
    padding-left: 11px;
}}
/* Sidebar footer card */
QFrame#SidebarFooter {{
    background: {c["sidebar_hover_bg"]};
    border: 1px solid {c["sidebar_border"]};
    border-radius: 12px;
}}
QLabel#SidebarFooterTitle {{
    color: {c["sidebar_text"]};
    font-size: 9pt;
    font-weight: 600;
}}
QLabel#SidebarFooterMuted {{
    color: {c["sidebar_muted"]};
    font-size: 8.5pt;
}}
QLabel#ConnectionDot[connected="true"]  {{ color: #4ade80; font-size: 10pt; }}
QLabel#ConnectionDot[connected="false"] {{ color: {c["danger"]}; font-size: 10pt; }}
/* Subtle theme toggle buttons */
QPushButton#ThemeToggleBtn {{
    background: transparent;
    color: {c["sidebar_muted"]};
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 3px 8px;
    font-size: 8.5pt;
    font-weight: 500;
    min-width: 0;
}}
QPushButton#ThemeToggleBtn:hover {{
    background: {c["sidebar_hover_bg"]};
    color: {c["sidebar_text"]};
}}
QPushButton#ThemeToggleBtn[active="true"] {{
    background: {c["sidebar_active_bg"]};
    color: {c["sidebar_active_text"]};
    border-color: {c["sidebar_active_bar"]};
    font-weight: 600;
}}

/* ── Top action bar ────────────────────────────────────────── */
QFrame#ActionBar {{
    background: {c["surface"]};
    border-bottom: 1px solid {c["border"]};
}}
QLabel#ActionBarPageTitle {{
    color: {c["text"]};
    font-size: 13pt;
    font-weight: 700;
}}
QLabel#SectionEyebrow {{
    color: {c["text_muted"]};
    font-size: 8pt;
    font-weight: 700;
    letter-spacing: 0.5px;
}}

/* ── Key chip selector ─────────────────────────────────────── */
QPushButton#KeyChip {{
    background: {c["surface_alt"]};
    color: {c["text"]};
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 7px 12px;
    font-size: 9.5pt;
    font-weight: 500;
    min-width: 56px;
}}
QPushButton#KeyChip:hover {{
    background: {c["surface_emphasis"]};
    border-color: {c["border"]};
}}
QPushButton#KeyChip[active="true"] {{
    background: {c["accent_soft"]};
    color: {c["accent"]};
    border-color: {c["accent"]};
    font-weight: 700;
}}

/* ── Standard buttons ──────────────────────────────────────── */
QPushButton#PrimaryButton {{
    background: {c["accent"]};
    color: #ffffff;
    border: none;
    border-radius: 8px;
    padding: 8px 16px;
    font-weight: 600;
}}
QPushButton#PrimaryButton:hover {{
    background: {c["accent_hover"]};
}}
QPushButton#PrimaryButton:disabled {{
    background: {c["surface_alt"]};
    color: {c["text_muted"]};
}}
QPushButton#SecondaryButton, QToolButton#SecondaryButton {{
    background: {c["surface_alt"]};
    color: {c["text"]};
    border: 1px solid {c["border"]};
    border-radius: 8px;
    padding: 8px 14px;
    font-weight: 500;
}}
QPushButton#SecondaryButton:hover, QToolButton#SecondaryButton:hover {{
    background: {c["surface_emphasis"]};
    border-color: {c["border_strong"]};
}}
QPushButton#DangerButton {{
    background: {c["danger_bg"]};
    color: {c["danger"]};
    border: 1px solid {c["danger_border"]};
    border-radius: 8px;
    padding: 8px 14px;
    font-weight: 600;
}}

/* ── Matrix LED buttons ─────────────────────────────────────── */
QPushButton#MatrixButton {{
    border-radius: 8px;
    border: 1px solid {c["border"]};
    min-width: 30px;
    min-height: 30px;
    max-width: 30px;
    max-height: 30px;
    padding: 0px;
    background: {c["surface_alt"]};
}}
QPushButton#MatrixButton:hover {{
    border-color: {c["accent"]};
}}

/* ── Status pill (status bar) ──────────────────────────────── */
QLabel#StatusPill {{
    border-radius: 6px;
    padding: 5px 10px;
    background: {c["surface_alt"]};
    color: {c["text_muted"]};
    font-size: 9pt;
    font-weight: 500;
}}
QLabel#StatusPill[level="ok"] {{
    background: {status_ok_bg};
    color: {c["success"]};
}}
QLabel#StatusPill[level="warn"] {{
    background: {status_warn_bg};
    color: {c["warning"]};
}}
QLabel#StatusPill[level="danger"] {{
    background: {status_err_bg};
    color: {c["danger"]};
}}

/* ── Cards ─────────────────────────────────────────────────── */
QFrame#SectionCard {{
    background: {c["surface"]};
    border: 1px solid {c["border"]};
    border-radius: 12px;
}}
QFrame#SubCard {{
    background: {c["surface_muted"]};
    border: 1px solid {c["border"]};
    border-radius: 10px;
}}
QLabel#CardTitle {{
    color: {c["text"]};
    font-size: 11pt;
    font-weight: 700;
}}
QLabel#CardSubtitle, QLabel#Muted, QLabel#BodyMuted {{
    color: {c["text_muted"]};
    font-size: 9pt;
}}

/* ── Form controls ─────────────────────────────────────────── */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {{
    background: {c["input_bg"]};
    color: {c["text"]};
    border: 1px solid {c["border"]};
    border-radius: 7px;
    padding: 6px 10px;
    min-height: 28px;
    selection-background-color: {c["accent"]};
}}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {{
    border-color: {c["accent"]};
}}
QTextEdit, QPlainTextEdit {{
    background: {c["surface_muted"]};
    color: {c["text"]};
    border: 1px solid {c["border"]};
    border-radius: 8px;
    padding: 8px;
    selection-background-color: {c["accent"]};
}}
QComboBox::drop-down {{
    background: transparent;
    border: none;
    width: 26px;
}}
QComboBox::down-arrow {{
    image: {chevron_down};
    width: 10px;
    height: 6px;
}}
QComboBox QAbstractItemView {{
    background: {c["surface"]};
    color: {c["text"]};
    border: 1px solid {c["border_strong"]};
    selection-background-color: {c["accent_soft"]};
    selection-color: {c["accent_text"]};
    padding: 4px;
    outline: none;
}}
QComboBox QAbstractItemView::item {{
    padding: 6px 10px;
    min-height: 26px;
    border-radius: 5px;
}}
QComboBox QAbstractItemView::item:hover {{
    background: {c["surface_alt"]};
    color: {c["text"]};
}}
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {{
    background: {c["surface_alt"]};
    border: none;
    width: 20px;
    subcontrol-origin: border;
}}
QAbstractSpinBox::up-button {{
    border-left: 1px solid {c["border"]};
    subcontrol-position: top right;
    border-top-right-radius: 7px;
}}
QAbstractSpinBox::down-button {{
    border-left: 1px solid {c["border"]};
    subcontrol-position: bottom right;
    border-bottom-right-radius: 7px;
}}
QAbstractSpinBox::up-arrow {{
    image: {arrow_up};
    width: 8px;
    height: 5px;
}}
QAbstractSpinBox::down-arrow {{
    image: {arrow_down};
    width: 8px;
    height: 5px;
}}
QCheckBox, QRadioButton {{ spacing: 8px; }}
QCheckBox::indicator, QRadioButton::indicator {{
    width: 16px;
    height: 16px;
    border: 1.5px solid {c["border_strong"]};
    border-radius: 4px;
    background: {c["input_bg"]};
}}
QCheckBox::indicator:checked {{
    background: {c["accent"]};
    border-color: {c["accent"]};
}}
QRadioButton::indicator {{ border-radius: 8px; }}
QRadioButton::indicator:checked {{
    background: {c["accent"]};
    border-color: {c["accent"]};
}}

/* ── Sliders ───────────────────────────────────────────────── */
QSlider::groove:horizontal {{
    height: 5px;
    background: {c["surface_alt"]};
    border-radius: 3px;
    margin: 0;
}}
QSlider::sub-page:horizontal {{
    background: {c["accent"]};
    border-radius: 3px;
}}
QSlider::handle:horizontal {{
    width: 16px;
    height: 16px;
    margin: -6px 0;
    border-radius: 8px;
    background: {c["accent"]};
    border: 2px solid {c["surface"]};
}}
QSlider::handle:horizontal:hover {{
    background: {c["accent_hover"]};
}}

/* ── Progress bar ──────────────────────────────────────────── */
QProgressBar {{
    background: {c["surface_alt"]};
    border: none;
    border-radius: 5px;
    min-height: 8px;
    text-align: center;
    color: {c["text"]};
    font-size: 8pt;
}}
QProgressBar::chunk {{
    background: {c["accent"]};
    border-radius: 5px;
}}

/* ── Tab widget ────────────────────────────────────────────── */
QTabWidget::pane {{
    background: transparent;
    border: none;
}}
QTabWidget::tab-bar {{
    alignment: left;
}}
QTabBar::tab {{
    background: transparent;
    color: {c["text_muted"]};
    border: none;
    border-bottom: 2px solid transparent;
    padding: 9px 16px;
    margin-right: 4px;
    font-size: 9.5pt;
    font-weight: 500;
}}
QTabBar::tab:selected {{
    color: {c["accent"]};
    border-bottom: 2px solid {c["accent"]};
    font-weight: 700;
}}
QTabBar::tab:hover:!selected {{
    color: {c["text"]};
    background: {c["surface_alt"]};
    border-radius: 8px 8px 0 0;
}}

/* ── Scroll areas ──────────────────────────────────────────── */
QScrollArea {{
    border: none;
    background: transparent;
}}
QScrollArea > QWidget > QWidget {{
    background: transparent;
}}
QScrollBar:vertical {{
    background: transparent;
    width: 12px;
    margin: 2px 2px 2px 0;
}}
QScrollBar::handle:vertical {{
    background: {c["scrollbar"]};
    border-radius: 5px;
    min-height: 28px;
}}
QScrollBar::handle:vertical:hover {{
    background: {c["scrollbar_hover"]};
}}
QScrollBar:horizontal {{
    background: transparent;
    height: 12px;
    margin: 0 2px 2px 2px;
}}
QScrollBar::handle:horizontal {{
    background: {c["scrollbar"]};
    border-radius: 5px;
    min-width: 28px;
}}
QScrollBar::handle:horizontal:hover {{
    background: {c["scrollbar_hover"]};
}}
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page {{
    background: transparent;
    border: none;
    width: 0; height: 0;
}}

/* ── Tables ────────────────────────────────────────────────── */
QTableWidget {{
    background: {c["surface"]};
    color: {c["text"]};
    border: 1px solid {c["border"]};
    border-radius: 10px;
    gridline-color: {c["border"]};
}}
QHeaderView::section {{
    background: {c["surface_muted"]};
    color: {c["text"]};
    border: none;
    border-bottom: 1px solid {c["border"]};
    padding: 8px 10px;
    font-size: 9pt;
    font-weight: 700;
}}

/* ── Status bar ────────────────────────────────────────────── */
QStatusBar {{
    background: {c["surface"]};
    border-top: 1px solid {c["border"]};
    padding: 2px 6px;
}}
QStatusBar QLabel {{
    background: transparent;
}}

/* ── Splitter ──────────────────────────────────────────────── */
QSplitter::handle {{
    background: transparent;
}}
QSplitter::handle:horizontal {{
    width: 8px;
}}
QSplitter::handle:vertical {{
    height: 8px;
}}
"""


def apply_app_style(app: QApplication, theme_mode: str = "system") -> str:
    global _ACTIVE_THEME_MODE, _ACTIVE_THEME_NAME, _ACTIVE_COLORS

    if not HAS_GUI:
        return "light"

    _ACTIVE_THEME_MODE = (theme_mode or "system").lower()
    _ACTIVE_THEME_NAME = resolve_theme_name(_ACTIVE_THEME_MODE)
    _ACTIVE_COLORS = THEMES.get(_ACTIVE_THEME_NAME, LIGHT_COLORS)
    app.setStyleSheet(build_app_style_sheet(_ACTIVE_COLORS))
    try:
        from PySide6.QtCore import Qt as _Qt
        app.setEffectEnabled(_Qt.UIEffect.UI_AnimateCombo, False)
    except Exception:
        pass
    return _ACTIVE_THEME_NAME
