import sys

from .common import HAS_GUI, ttk


APP_COLORS = {
    "bg": "#f5f6f8",
    "surface": "#ffffff",
    "surface_alt": "#eef1f5",
    "surface_alt_2": "#e6ebf2",
    "sidebar": "#f7f8fa",
    "sidebar_active": "#e8f0fe",
    "sidebar_hover": "#edf2f7",
    "text": "#1b1c1f",
    "text_muted": "#61656d",
    "accent": "#0f6cbd",
    "accent_alt": "#2b88d8",
    "accent_hover": "#115ea3",
    "accent_pressed": "#0f548c",
    "accent_soft": "#dbeafe",
    "success": "#0f7b0f",
    "warning": "#8a5a00",
    "danger": "#b42318",
    "border": "#dde3ea",
}


def configure_windows_rendering():
    if not HAS_GUI or sys.platform != "win32":
        return

    try:
        from ctypes import windll
    except Exception:
        return

    try:
        windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        try:
            windll.user32.SetProcessDPIAware()
        except Exception:
            pass


def _use_base_theme(style):
    for theme_name in ("vista", "xpnative", "clam"):
        try:
            style.theme_use(theme_name)
            return theme_name
        except Exception:
            continue
    return None


def apply_app_theme(root):
    if not HAS_GUI:
        return

    style = ttk.Style(root)
    _use_base_theme(style)

    root.configure(background=APP_COLORS["bg"])
    try:
        root.option_add("*Font", "{Segoe UI} 10")
        root.option_add("*tearOff", False)
    except Exception:
        pass

    style.configure(".", font=("Segoe UI", 10), foreground=APP_COLORS["text"])
    style.configure("TFrame", background=APP_COLORS["surface"])
    style.configure("App.TFrame", background=APP_COLORS["bg"])
    style.configure("Surface.TFrame", background=APP_COLORS["surface"])
    style.configure("Sidebar.TFrame", background=APP_COLORS["sidebar"])
    style.configure("Toolbar.TFrame", background=APP_COLORS["bg"])

    style.configure(
        "Card.TLabelframe",
        background=APP_COLORS["surface"],
        bordercolor=APP_COLORS["border"],
        relief="solid",
        borderwidth=1,
        padding=12,
    )
    style.configure(
        "Card.TLabelframe.Label",
        background=APP_COLORS["surface"],
        foreground=APP_COLORS["text"],
        font=("Segoe UI Semibold", 10),
    )

    style.configure(
        "Title.TLabel",
        background=APP_COLORS["bg"],
        foreground=APP_COLORS["text"],
        font=("Segoe UI Semibold", 20),
    )
    style.configure(
        "PageTitle.TLabel",
        background=APP_COLORS["surface"],
        foreground=APP_COLORS["text"],
        font=("Segoe UI Semibold", 18),
    )
    style.configure(
        "SectionTitle.TLabel",
        background=APP_COLORS["surface"],
        foreground=APP_COLORS["text"],
        font=("Segoe UI Semibold", 12),
    )
    style.configure(
        "Subtle.TLabel",
        background=APP_COLORS["bg"],
        foreground=APP_COLORS["text_muted"],
        font=("Segoe UI", 9),
    )
    style.configure(
        "SurfaceSubtle.TLabel",
        background=APP_COLORS["surface"],
        foreground=APP_COLORS["text_muted"],
        font=("Segoe UI", 9),
    )
    style.configure(
        "SidebarTitle.TLabel",
        background=APP_COLORS["sidebar"],
        foreground=APP_COLORS["text"],
        font=("Segoe UI Semibold", 16),
    )
    style.configure(
        "SidebarSubtle.TLabel",
        background=APP_COLORS["sidebar"],
        foreground=APP_COLORS["text_muted"],
        font=("Segoe UI", 9),
    )
    style.configure(
        "SidebarGroup.TLabel",
        background=APP_COLORS["sidebar"],
        foreground=APP_COLORS["text_muted"],
        font=("Segoe UI Semibold", 9),
    )

    style.configure(
        "Nav.TButton",
        background=APP_COLORS["sidebar"],
        foreground=APP_COLORS["text"],
        borderwidth=0,
        relief="flat",
        focusthickness=0,
        padding=(12, 9),
        anchor="w",
        font=("Segoe UI", 10),
    )
    style.map(
        "Nav.TButton",
        background=[("active", APP_COLORS["sidebar_hover"]), ("pressed", APP_COLORS["sidebar_hover"])],
    )
    style.configure(
        "NavSelected.TButton",
        background=APP_COLORS["sidebar_active"],
        foreground=APP_COLORS["accent"],
        borderwidth=0,
        relief="flat",
        focusthickness=0,
        padding=(12, 9),
        anchor="w",
        font=("Segoe UI Semibold", 10),
    )
    style.map(
        "NavSelected.TButton",
        background=[("active", APP_COLORS["sidebar_active"]), ("pressed", APP_COLORS["sidebar_active"])],
        foreground=[("active", APP_COLORS["accent"]), ("pressed", APP_COLORS["accent"])],
    )

    style.configure(
        "Chip.TButton",
        background=APP_COLORS["surface_alt"],
        foreground=APP_COLORS["text"],
        borderwidth=0,
        relief="flat",
        focusthickness=0,
        padding=(10, 6),
        font=("Segoe UI", 9),
    )
    style.map(
        "Chip.TButton",
        background=[("active", APP_COLORS["surface_alt_2"]), ("pressed", APP_COLORS["surface_alt_2"])],
    )
    style.configure(
        "ChipSelected.TButton",
        background=APP_COLORS["accent_soft"],
        foreground=APP_COLORS["accent"],
        borderwidth=0,
        relief="flat",
        focusthickness=0,
        padding=(10, 6),
        font=("Segoe UI Semibold", 9),
    )
    style.map(
        "ChipSelected.TButton",
        background=[("active", APP_COLORS["accent_soft"]), ("pressed", APP_COLORS["accent_soft"])],
    )

    style.configure(
        "Primary.TButton",
        background=APP_COLORS["accent"],
        foreground="#ffffff",
        borderwidth=0,
        relief="flat",
        focusthickness=0,
        padding=(12, 8),
        font=("Segoe UI Semibold", 10),
    )
    style.map(
        "Primary.TButton",
        background=[("active", APP_COLORS["accent_hover"]), ("pressed", APP_COLORS["accent_pressed"])],
    )
    style.configure(
        "Ghost.TButton",
        background=APP_COLORS["surface_alt"],
        foreground=APP_COLORS["text"],
        borderwidth=0,
        relief="flat",
        focusthickness=0,
        padding=(10, 8),
        font=("Segoe UI", 10),
    )
    style.map(
        "Ghost.TButton",
        background=[("active", APP_COLORS["surface_alt_2"]), ("pressed", APP_COLORS["surface_alt_2"])],
    )

    style.configure(
        "Status.TLabel",
        background=APP_COLORS["surface_alt"],
        foreground=APP_COLORS["text"],
        padding=(10, 6),
        font=("Segoe UI Semibold", 9),
    )
    style.configure(
        "StatusOk.TLabel",
        background="#e8f5e9",
        foreground=APP_COLORS["success"],
        padding=(10, 6),
        font=("Segoe UI Semibold", 9),
    )
    style.configure(
        "StatusWarn.TLabel",
        background="#fff4e5",
        foreground=APP_COLORS["warning"],
        padding=(10, 6),
        font=("Segoe UI Semibold", 9),
    )
    style.configure(
        "StatusDanger.TLabel",
        background="#fdecec",
        foreground=APP_COLORS["danger"],
        padding=(10, 6),
        font=("Segoe UI Semibold", 9),
    )

    style.configure(
        "TLabel",
        background=APP_COLORS["surface"],
        foreground=APP_COLORS["text"],
    )
    style.configure(
        "TCheckbutton",
        background=APP_COLORS["surface"],
        foreground=APP_COLORS["text"],
    )
    style.configure(
        "TRadiobutton",
        background=APP_COLORS["surface"],
        foreground=APP_COLORS["text"],
    )
    style.configure(
        "TCombobox",
        arrowsize=14,
        padding=6,
        fieldbackground=APP_COLORS["surface"],
    )
    style.configure(
        "TEntry",
        fieldbackground=APP_COLORS["surface"],
        padding=6,
    )
    style.configure("Horizontal.TScale", background=APP_COLORS["surface"])
    style.configure("TProgressbar", background=APP_COLORS["accent"], troughcolor=APP_COLORS["surface_alt"])
    style.configure("TSeparator", background=APP_COLORS["border"])
