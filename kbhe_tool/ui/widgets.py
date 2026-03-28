from .common import HAS_GUI, tk, ttk
from .scroll import ScrollableFrame
from .theme import APP_COLORS


def create_page_frame(parent, title, description=None, scrollable=True):
    container = ttk.Frame(parent, style="Surface.TFrame")
    container.pack(fill=tk.BOTH, expand=True)

    header = ttk.Frame(container, style="Surface.TFrame")
    header.pack(fill=tk.X, padx=18, pady=(18, 8))

    ttk.Label(header, text=title, style="PageTitle.TLabel").pack(anchor=tk.W)
    if description:
        ttk.Label(
            header,
            text=description,
            style="SurfaceSubtle.TLabel",
            wraplength=920,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(4, 0))

    if scrollable:
        scroll = ScrollableFrame(container)
        scroll.pack(fill=tk.BOTH, expand=True, padx=12, pady=(0, 12))
        return container, scroll.content

    body = ttk.Frame(container, style="Surface.TFrame")
    body.pack(fill=tk.BOTH, expand=True, padx=12, pady=(0, 12))
    return container, body


def create_card(parent, title, description=None, padding=12):
    card = ttk.LabelFrame(parent, text=title, padding=padding, style="Card.TLabelframe")
    card.pack(fill=tk.X, pady=8)
    if description:
        ttk.Label(
            card,
            text=description,
            style="SurfaceSubtle.TLabel",
            wraplength=860,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 8))
    return card


def create_section_row(parent):
    row = ttk.Frame(parent, style="Surface.TFrame")
    row.pack(fill=tk.X, pady=6)
    return row


def create_labeled_value(parent, label, variable, width=18):
    row = create_section_row(parent)
    ttk.Label(row, text=label, width=width).pack(side=tk.LEFT)
    ttk.Label(row, textvariable=variable, style="Status.TLabel").pack(side=tk.LEFT)
    return row


def create_button_row(parent, primary_text=None, primary_command=None, secondary=None):
    row = create_section_row(parent)
    if primary_text and primary_command:
        ttk.Button(row, text=primary_text, command=primary_command, style="Primary.TButton").pack(
            side=tk.LEFT
        )
    for label, command in secondary or []:
        ttk.Button(row, text=label, command=command, style="Ghost.TButton").pack(
            side=tk.LEFT, padx=6
        )
    return row


class KeyStrip(ttk.Frame):
    def __init__(self, parent, variable, on_change):
        super().__init__(parent, style="App.TFrame")
        self._buttons = []
        self._variable = variable
        self._on_change = on_change

        ttk.Label(self, text="Selected Key", style="Subtle.TLabel").pack(anchor=tk.W)

        button_row = ttk.Frame(self, style="App.TFrame")
        button_row.pack(anchor=tk.W, pady=(4, 0))

        for index in range(6):
            button = ttk.Button(
                button_row,
                text=f"Key {index + 1}",
                command=lambda idx=index: self.set_key(idx),
                style="Chip.TButton",
                width=9,
            )
            button.pack(side=tk.LEFT, padx=(0, 6))
            self._buttons.append(button)

        self.refresh()

    def set_key(self, index):
        self._variable.set(index)
        self.refresh()
        if self._on_change:
            self._on_change(index)

    def refresh(self):
        selected = int(self._variable.get())
        for index, button in enumerate(self._buttons):
            button.configure(style="ChipSelected.TButton" if index == selected else "Chip.TButton")


def make_status_chip(parent, textvariable):
    label = ttk.Label(parent, textvariable=textvariable, style="Status.TLabel")
    label.pack(anchor=tk.W)
    return label


def set_status_style(label, level="default"):
    if not HAS_GUI or label is None:
        return

    styles = {
        "default": "Status.TLabel",
        "ok": "StatusOk.TLabel",
        "warn": "StatusWarn.TLabel",
        "danger": "StatusDanger.TLabel",
    }
    label.configure(style=styles.get(level, "Status.TLabel"))


def create_surface_panel(parent):
    panel = tk.Frame(parent, bg=APP_COLORS["surface"], highlightbackground=APP_COLORS["border"], highlightthickness=1)
    panel.pack(fill=tk.BOTH, expand=True)
    return panel
