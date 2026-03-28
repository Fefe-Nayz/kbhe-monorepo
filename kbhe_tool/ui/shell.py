from .common import HAS_GUI, queue, threading, time, tk, ttk
from .lighting_page import LightingPageMixin
from .effects_page import EffectsPageMixin
from .keyboard_page import KeyboardPageMixin
from .gamepad_page import GamepadPageMixin
from .calibration_page import CalibrationPageMixin
from .device_page import DevicePageMixin
from .debug_page import DebugPageMixin
from .graph_page import GraphPageMixin
from .firmware_page import FirmwarePageMixin
from .theme import APP_COLORS, apply_app_theme, configure_windows_rendering
from .widgets import KeyStrip, make_status_chip


PAGE_GROUPS = [
    (
        "Configure",
        [
            ("Keyboard", "Per-key actuation, SOCD and rapid trigger", "⌨"),
            ("Calibration", "Zero points and analog response curves", ""),
            ("Gamepad", "Analog shaping and per-key mappings", ""),
        ],
    ),
    (
        "Lighting",
        [
            ("Lighting", "Matrix painting, brightness and patterns", ""),
            ("Effects", "Animated effects tuning and diagnostics", ""),
        ],
    ),
    (
        "Inspect",
        [
            ("Device", "Output modes, options and persistence", ""),
            ("Debug / Sensors", "Live sensor inspection and filters", ""),
            ("Live Graph", "Continuous ADC and travel plotting", ""),
        ],
    ),
    (
        "Maintenance",
        [
            ("Firmware", "Updater, logs and recovery tools", ""),
        ],
    ),
]


class KBHEConfiguratorApp(
    FirmwarePageMixin,
    GraphPageMixin,
    DebugPageMixin,
    DevicePageMixin,
    CalibrationPageMixin,
    GamepadPageMixin,
    KeyboardPageMixin,
    EffectsPageMixin,
    LightingPageMixin,
    tk.Tk if HAS_GUI else object,
):
    """KBHE keyboard configurator shell."""

    def __init__(self, device):
        if not HAS_GUI:
            raise RuntimeError("tkinter is not available on this system")

        configure_windows_rendering()
        super().__init__()

        self.device = device
        self.title("KBHE Keyboard Configurator")
        self.geometry("1360x960")
        self.minsize(1220, 820)
        self.resizable(True, True)
        apply_app_theme(self)

        self.current_color = [255, 0, 0]
        self.pixels = [[0, 0, 0] for _ in range(64)]

        self.selected_key_var = tk.IntVar(value=0)
        self.live_sensor_update = False
        self.sensor_update_job = None
        self.sensor_queue = queue.Queue()
        self.sensor_thread = None
        self.sensor_thread_running = False
        self.gamepad_viz_job = None
        self.graph_job = None

        self.last_update_time = time.time()
        self.update_count = 0

        self.active_tab = None
        self.tabs = {}
        self.nav_buttons = {}
        self.page_hosts = {}

        self.firmware_busy = False
        self.firmware_log_queue = queue.Queue()
        self.firmware_log_job = None
        self.firmware_thread = None

        self.create_widgets()
        self.after_idle(self.refresh_from_device)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def on_close(self):
        self._stop_sensor_updates()
        self._stop_gamepad_viz()
        self._stop_graph_updates()
        if self.firmware_log_job:
            self.after_cancel(self.firmware_log_job)
            self.firmware_log_job = None
        if self.sensor_thread and self.sensor_thread.is_alive():
            self.sensor_thread.join(timeout=0.5)
        self.destroy()

    def create_widgets(self):
        self.status_var = tk.StringVar(
            value="Ready. Configure the keyboard from the left navigation, then save when you want the current live state persisted."
        )
        self.connection_var = tk.StringVar(value="Connected")

        root = ttk.Frame(self, style="App.TFrame")
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(1, weight=1)
        root.rowconfigure(0, weight=1)

        self._create_sidebar(root)
        self._create_main_surface(root)

        self.select_tab("Keyboard")

    def _create_sidebar(self, parent):
        sidebar = ttk.Frame(parent, style="Sidebar.TFrame", padding=(16, 18))
        sidebar.grid(row=0, column=0, sticky="nsew")
        parent.columnconfigure(0, minsize=250)

        ttk.Label(sidebar, text="KBHE", style="SidebarTitle.TLabel").pack(anchor=tk.W)
        ttk.Label(
            sidebar,
            text="Clear per-page tooling, shared key focus, and direct access to what matters most.",
            style="SidebarSubtle.TLabel",
            wraplength=210,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(6, 16))

        for group_name, pages in PAGE_GROUPS:
            ttk.Label(sidebar, text=group_name.upper(), style="SidebarGroup.TLabel").pack(
                anchor=tk.W, pady=(14, 6)
            )
            for page_name, page_desc, icon in pages:
                button = ttk.Button(
                    sidebar,
                    text=page_name,
                    command=lambda name=page_name: self.select_tab(name),
                    style="Nav.TButton",
                )
                button.pack(fill=tk.X, pady=2)
                self.nav_buttons[page_name] = button

                desc = ttk.Label(
                    sidebar,
                    text=page_desc,
                    style="SidebarSubtle.TLabel",
                    wraplength=210,
                    justify=tk.LEFT,
                )
                desc.pack(anchor=tk.W, padx=12, pady=(0, 4))

        footer = ttk.Frame(sidebar, style="Sidebar.TFrame")
        footer.pack(fill=tk.X, side=tk.BOTTOM, pady=(18, 0))
        ttk.Label(footer, textvariable=self.connection_var, style="SidebarSubtle.TLabel").pack(anchor=tk.W)
        ttk.Label(
            footer,
            text="Use Save to Flash once the live state matches what you want after reboot.",
            style="SidebarSubtle.TLabel",
            wraplength=210,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(4, 0))

    def _create_main_surface(self, parent):
        surface = ttk.Frame(parent, style="App.TFrame")
        surface.grid(row=0, column=1, sticky="nsew")
        parent.rowconfigure(0, weight=1)

        header = ttk.Frame(surface, style="Toolbar.TFrame", padding=(20, 18, 20, 10))
        header.pack(fill=tk.X)

        title_col = ttk.Frame(header, style="Toolbar.TFrame")
        title_col.pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Label(title_col, text="Keyboard Configurator", style="Title.TLabel").pack(anchor=tk.W)
        ttk.Label(
            title_col,
            text="A cleaner Windows-style layout with page ownership, shared key focus, and lighter live interactions.",
            style="Subtle.TLabel",
            wraplength=780,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(4, 0))

        action_col = ttk.Frame(header, style="Toolbar.TFrame")
        action_col.pack(side=tk.RIGHT)
        ttk.Button(action_col, text="Refresh", command=self.refresh_from_device, style="Ghost.TButton").pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(action_col, text="Save to Flash", command=self.save_to_device, style="Primary.TButton").pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(action_col, text="Firmware", command=lambda: self.select_tab("Firmware"), style="Ghost.TButton").pack(
            side=tk.LEFT, padx=4
        )

        info_row = ttk.Frame(surface, style="App.TFrame", padding=(20, 0, 20, 10))
        info_row.pack(fill=tk.X)

        self.key_strip = KeyStrip(info_row, self.selected_key_var, self.on_selected_key_changed)
        self.key_strip.pack(side=tk.LEFT, anchor=tk.W)

        status_col = ttk.Frame(info_row, style="App.TFrame")
        status_col.pack(side=tk.RIGHT, fill=tk.X, expand=True)
        ttk.Label(status_col, text="Session Status", style="Subtle.TLabel").pack(anchor=tk.E)
        self.status_chip = make_status_chip(status_col, self.status_var)
        self.status_chip.pack_configure(anchor=tk.E, pady=(4, 0))

        content = ttk.Frame(surface, style="App.TFrame")
        content.pack(fill=tk.BOTH, expand=True, padx=20, pady=(0, 12))
        content.rowconfigure(0, weight=1)
        content.columnconfigure(0, weight=1)

        self.page_container = content

        builders = {
            "Keyboard": self.create_key_settings_widgets,
            "Calibration": self.create_calibration_widgets,
            "Gamepad": self.create_gamepad_settings_widgets,
            "Lighting": self.create_led_widgets,
            "Effects": self.create_led_effects_widgets,
            "Device": self.create_settings_widgets,
            "Debug / Sensors": self.create_debug_widgets,
            "Live Graph": self.create_graph_widgets,
            "Firmware": self.create_firmware_widgets,
        }

        for page_name, builder in builders.items():
            host = ttk.Frame(content, style="Surface.TFrame")
            host.grid(row=0, column=0, sticky="nsew")
            self.tabs[page_name] = host
            self.page_hosts[page_name] = host
            builder(host)

    def _set_connection_state(self, connected):
        self.connection_var.set("Connected" if connected else "Disconnected")

    def refresh_from_device(self):
        self.load_from_device()
        if hasattr(self, "load_led_effect_settings"):
            self.load_led_effect_settings()
        if hasattr(self, "load_selected_key_settings"):
            self.load_selected_key_settings(int(self.selected_key_var.get()))
        if hasattr(self, "load_gamepad_settings"):
            self.load_gamepad_settings()
        if hasattr(self, "load_calibration"):
            self.load_calibration()
        if hasattr(self, "load_key_curve"):
            self.load_key_curve(int(self.selected_key_var.get()))
        if hasattr(self, "load_filter_settings"):
            self.load_filter_settings()

        self._set_connection_state(True)
        self.status_var.set("Device state refreshed from firmware and shared key-dependent views synchronized.")

    def on_selected_key_changed(self, key_index):
        if hasattr(self, "load_selected_key_settings"):
            self.load_selected_key_settings(key_index)
        if hasattr(self, "load_key_curve"):
            self.load_key_curve(key_index)
        self.status_var.set(f"Focused Key {key_index + 1}. Keyboard and curve editors are now aligned.")

    def select_tab(self, tab_name):
        if tab_name not in self.tabs:
            return

        self.active_tab = tab_name
        self.tabs[tab_name].tkraise()

        for page_name, button in self.nav_buttons.items():
            button.configure(style="NavSelected.TButton" if page_name == tab_name else "Nav.TButton")

        self.on_tab_changed()

    def on_tab_changed(self, event=None):
        del event

        if self.active_tab == "Debug / Sensors":
            if getattr(self, "live_update_var", None) and self.live_update_var.get():
                self._start_sensor_updates()
        else:
            self._stop_sensor_updates(preserve_toggle=True)

        if self.active_tab == "Live Graph":
            if getattr(self, "graph_live_var", None) and self.graph_live_var.get():
                self._start_graph_updates()
        else:
            self._stop_graph_updates(preserve_toggle=True)

        if self.active_tab == "Gamepad":
            if getattr(self, "gamepad_viz_enabled", None) and self.gamepad_viz_enabled.get():
                self._start_gamepad_viz()
        else:
            self._stop_gamepad_viz(preserve_toggle=True)

        if self.key_strip:
            self.key_strip.refresh()

    def _start_sensor_updates(self):
        if self.live_sensor_update:
            return
        self.live_sensor_update = True
        self.last_update_time = time.time()
        self.update_count = 0
        self.sensor_thread_running = True
        self.sensor_thread = threading.Thread(target=self._sensor_reader_thread, daemon=True)
        self.sensor_thread.start()
        self._process_sensor_queue()

    def _stop_sensor_updates(self, preserve_toggle=False):
        self.live_sensor_update = False
        self.sensor_thread_running = False
        if self.sensor_update_job:
            self.after_cancel(self.sensor_update_job)
            self.sensor_update_job = None
        if not preserve_toggle and hasattr(self, "live_update_var"):
            self.live_update_var.set(False)

    def _start_graph_updates(self):
        if self.graph_job is None:
            self.update_graph()

    def _stop_graph_updates(self, preserve_toggle=False):
        if self.graph_job:
            self.after_cancel(self.graph_job)
            self.graph_job = None
        if not preserve_toggle and hasattr(self, "graph_live_var"):
            self.graph_live_var.set(False)

    def _start_gamepad_viz(self):
        if self.gamepad_viz_job is None:
            self.update_gamepad_viz()

    def _stop_gamepad_viz(self, preserve_toggle=False):
        if self.gamepad_viz_job:
            self.after_cancel(self.gamepad_viz_job)
            self.gamepad_viz_job = None
        if not preserve_toggle and hasattr(self, "gamepad_viz_enabled"):
            self.gamepad_viz_enabled.set(False)


LEDMatrixEditor = KBHEConfiguratorApp

__all__ = ["HAS_GUI", "KBHEConfiguratorApp", "LEDMatrixEditor"]
