from .common import HAS_GUI, tk, ttk, time, queue, threading
from .lighting_page import LightingPageMixin
from .effects_page import EffectsPageMixin
from .keyboard_page import KeyboardPageMixin
from .gamepad_page import GamepadPageMixin
from .calibration_page import CalibrationPageMixin
from .device_page import DevicePageMixin
from .debug_page import DebugPageMixin
from .graph_page import GraphPageMixin
from .firmware_page import FirmwarePageMixin


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

        super().__init__()

        self.device = device
        self.title("KBHE Keyboard Configurator")
        self.geometry("1180x940")
        self.minsize(1080, 820)
        self.resizable(True, True)

        # Current color
        self.current_color = [255, 0, 0]  # Default: Red

        # Pixel data (8x8 RGB)
        self.pixels = [[0, 0, 0] for _ in range(64)]

        # Live update flag
        self.live_sensor_update = False
        self.sensor_update_job = None

        # Async sensor data queue
        self.sensor_queue = queue.Queue()
        self.sensor_thread = None
        self.sensor_thread_running = False
        self.gamepad_viz_job = None
        self.graph_job = None

        # Timing tracking for performance display
        self.last_update_time = time.time()
        self.update_count = 0

        self.active_tab = None
        self.tabs = {}
        self.firmware_busy = False
        self.firmware_log_queue = queue.Queue()
        self.firmware_log_job = None
        self.firmware_thread = None

        # GUI elements
        self.create_widgets()

        # Load current state from device across all pages
        self.refresh_from_device()

        # Handle window close
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def on_close(self):
        """Handle window close - stop sensor thread."""
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
        """Create all GUI elements."""

        self.status_var = tk.StringVar(
            value="Ready. Use the keyboard, lighting, gamepad, and firmware pages to configure the device."
        )

        header = ttk.Frame(self, padding=(10, 10, 10, 4))
        header.pack(fill=tk.X)

        title_frame = ttk.Frame(header)
        title_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Label(
            title_frame,
            text="KBHE Keyboard Configurator",
            font=("Segoe UI", 16, "bold"),
        ).pack(anchor=tk.W)
        ttk.Label(
            title_frame,
            text="Structured like the main configurator flow: keyboard first, then device subsystems and maintenance.",
            foreground="gray",
        ).pack(anchor=tk.W)

        actions = ttk.Frame(header)
        actions.pack(side=tk.RIGHT)
        ttk.Button(actions, text="Refresh", command=self.refresh_from_device).pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(actions, text="Save to Flash", command=self.save_to_device).pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(
            actions,
            text="Firmware",
            command=lambda: self.select_tab("Firmware"),
        ).pack(side=tk.LEFT, padx=4)

        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=8, pady=6)

        tab_specs = [
            ("Keyboard", self.create_key_settings_widgets),
            ("Lighting", self.create_led_widgets),
            ("Effects", self.create_led_effects_widgets),
            ("Gamepad", self.create_gamepad_settings_widgets),
            ("Calibration", self.create_calibration_widgets),
            ("Device", self.create_settings_widgets),
            ("Debug / Sensors", self.create_debug_widgets),
            ("Live Graph", self.create_graph_widgets),
            ("Firmware", self.create_firmware_widgets),
        ]

        for tab_name, builder in tab_specs:
            tab = ttk.Frame(self.notebook, padding="10")
            self.tabs[tab_name] = tab
            self.notebook.add(tab, text=tab_name)
            builder(tab)

        self.notebook.bind("<<NotebookTabChanged>>", self.on_tab_changed)

        status_frame = ttk.Frame(self)
        status_frame.pack(fill=tk.X, padx=8, pady=(0, 8))

        status_label = ttk.Label(status_frame, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W)
        status_label.pack(fill=tk.X)

        self.after_idle(self.on_tab_changed)

    def refresh_from_device(self):
        """Reload all device-backed views."""
        self.load_from_device()
        if hasattr(self, "load_led_effect_settings"):
            self.load_led_effect_settings()
        if hasattr(self, "load_selected_key_settings"):
            self.load_selected_key_settings()
        if hasattr(self, "load_gamepad_settings"):
            self.load_gamepad_settings()
        if hasattr(self, "load_calibration"):
            self.load_calibration()
        if hasattr(self, "load_key_curve"):
            self.load_key_curve()
        if hasattr(self, "load_filter_settings"):
            self.load_filter_settings()
        self.status_var.set("🔄 Device state refreshed")

    def select_tab(self, tab_name):
        """Switch to a named notebook tab."""
        tab = self.tabs.get(tab_name)
        if tab is not None:
            self.notebook.select(tab)

    def on_tab_changed(self, event=None):
        """Pause heavy live tasks when their page is hidden."""
        del event
        self.active_tab = self.notebook.tab(self.notebook.select(), "text")

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

__all__ = ['HAS_GUI', 'KBHEConfiguratorApp', 'LEDMatrixEditor']
