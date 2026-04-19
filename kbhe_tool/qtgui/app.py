from __future__ import annotations

import sys
import time

from .common import (
    HAS_GUI,
    QApplication,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QStackedWidget,
    QStatusBar,
    QTimer,
    QVBoxLayout,
    QWidget,
    Qt,
)
from .pages import (
    AppSettingsPage,
    CalibrationPage,
    DebugPage,
    DevicePage,
    EffectsPage,
    FirmwarePage,
    GamepadPage,
    GraphPage,
    KeyboardPage,
    LightingPage,
    OverviewPage,
    RawADCPage,
    RotaryEncoderPage,
    TravelPage,
)
from .session import AppSession
from .app_prefs import AppPreferences
from .theme import apply_app_style, current_theme_mode
from .widgets import StatusPill, make_primary_button, make_secondary_button
from ..windows_volume import (
    get_last_render_spectrum_source,
    get_default_render_spectrum_levels,
    get_default_render_volume_level,
)


# ---------------------------------------------------------------------------
# Sidebar navigation structure
# Each entry: (page_id, label, group_label_before_this_item)
# group_label_before_this_item creates a section header above the button.
# ---------------------------------------------------------------------------

_NAV: list[tuple[str, str, str | None]] = [
    ("overview",     "Overview",         "Workspace"),
    ("settings",     "App Settings",     None),
    ("keyboard",     "Keyboard",         "Configure"),
    ("calibration",  "Calibration",      None),
    ("travel",       "Travel",           None),
    ("gamepad",      "Gamepad",          None),
    ("rotary",       "Rotary Encoder",   None),
    ("lighting",     "Lighting",         "Lighting"),
    ("effects",      "Effects",          None),
    ("device",       "Device",           "Inspect"),
    ("raw_adc",      "82 Raw ADC",       None),
    ("debug",        "Debug / Sensors",  None),
    ("graph",        "Live Graph",       None),
    ("firmware",     "Firmware",         "Maintain"),
]


class KBHEQtMainWindow(QMainWindow):
    def __init__(self, device):
        if not HAS_GUI:
            raise RuntimeError("PySide6 is not available on this system")

        super().__init__()
        self.session = AppSession(device)
        self.app_prefs = AppPreferences()
        self.active_page_id: str | None = None
        self.theme_mode = current_theme_mode()
        self.nav_buttons: dict[str, QPushButton] = {}
        self.pages: dict[str, QWidget] = {}
        self._theme_btns: dict[str, QPushButton] = {}
        self.live_timer = QTimer(self)
        self.live_timer.timeout.connect(self._on_live_tick)
        self.volume_timer = QTimer(self)
        self.volume_timer.setInterval(25)
        self.volume_timer.timeout.connect(self._on_volume_tick)
        self._last_host_volume_level: int | None = None
        self._last_host_volume_push_monotonic = 0.0
        self._last_audio_spectrum: list[int] | None = None
        self._last_audio_spectrum_push_monotonic = 0.0
        self._last_audio_spectrum_source = "none"

        self.setWindowTitle("KBHE Configurator")
        self.resize(1440, 900)
        self.setMinimumSize(1180, 720)

        self._build_ui()
        self._connect_signals()
        self._on_selected_key_changed(self.session.selected_key)
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)
        self.session.refresh_snapshot()
        self._apply_pending_led_restore_if_needed()
        self.show_page("overview")
        self.volume_timer.start()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        shell = QFrame()
        shell.setObjectName("ShellRoot")
        self.setCentralWidget(shell)

        shell_layout = QHBoxLayout(shell)
        shell_layout.setContentsMargins(0, 0, 0, 0)
        shell_layout.setSpacing(0)

        # Left: sidebar (fixed width)
        shell_layout.addWidget(self._build_sidebar(), 0)

        # Right: action bar + page stack
        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(0)
        shell_layout.addWidget(right, 1)

        right_layout.addWidget(self._build_action_bar(), 0)

        # Page stack
        self.page_stack = QStackedWidget()
        right_layout.addWidget(self.page_stack, 1)

        for page_id, page in self._create_pages().items():
            self.pages[page_id] = page
            self.page_stack.addWidget(page)

        # Status bar
        status = QStatusBar()
        status.setSizeGripEnabled(False)
        self.setStatusBar(status)
        self.status_pill = StatusPill("Ready")
        status.addPermanentWidget(self.status_pill, 1)

    def _build_sidebar(self) -> QFrame:
        sidebar = QFrame()
        sidebar.setObjectName("Sidebar")
        sidebar.setFixedWidth(220)

        layout = QVBoxLayout(sidebar)
        layout.setContentsMargins(14, 20, 14, 16)
        layout.setSpacing(2)

        # ── Brand ─────────────────────────────────────────
        brand_row = QHBoxLayout()
        brand_row.setSpacing(8)

        title = QLabel("KBHE")
        title.setObjectName("SidebarTitle")
        brand_row.addWidget(title)

        self.connection_dot = QLabel("●")
        self.connection_dot.setObjectName("ConnectionDot")
        self.connection_dot.setProperty("connected", "true")
        self.connection_dot.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        brand_row.addWidget(self.connection_dot, 1)

        layout.addLayout(brand_row)

        self.fw_label = QLabel("—")
        self.fw_label.setObjectName("SidebarVersion")
        layout.addWidget(self.fw_label)

        layout.addSpacing(16)

        # ── Navigation ────────────────────────────────────
        for page_id, label, group in _NAV:
            if group is not None:
                grp_lbl = QLabel(group.upper())
                grp_lbl.setObjectName("SidebarGroupLabel")
                layout.addSpacing(10)
                layout.addWidget(grp_lbl)
                layout.addSpacing(2)

            btn = QPushButton(label)
            btn.setObjectName("NavButton")
            btn.setProperty("active", "false")
            btn.clicked.connect(lambda _c=False, pid=page_id: self.show_page(pid))
            layout.addWidget(btn)
            self.nav_buttons[page_id] = btn

        layout.addStretch(1)

        # ── Footer card ───────────────────────────────────
        footer = QFrame()
        footer.setObjectName("SidebarFooter")
        footer_layout = QVBoxLayout(footer)
        footer_layout.setContentsMargins(12, 10, 12, 10)
        footer_layout.setSpacing(6)

        reload_btn = make_secondary_button("Reload All", self._refresh_all)
        footer_layout.addWidget(reload_btn)

        layout.addWidget(footer)
        return sidebar

    def _build_action_bar(self) -> QFrame:
        """Thin bar between sidebar and page content with key selector + actions."""
        bar = QFrame()
        bar.setObjectName("ActionBar")

        outer = QHBoxLayout(bar)
        outer.setContentsMargins(20, 10, 16, 10)
        outer.setSpacing(16)

        # Page title (updated on navigation)
        self.page_title_label = QLabel("Overview")
        self.page_title_label.setObjectName("ActionBarPageTitle")
        outer.addWidget(self.page_title_label, 0)

        outer.addStretch(1)

        focus_wrap = QVBoxLayout()
        focus_wrap.setContentsMargins(0, 0, 0, 0)
        focus_wrap.setSpacing(2)
        key_lbl = QLabel("FOCUSED KEY")
        key_lbl.setObjectName("SectionEyebrow")
        focus_wrap.addWidget(key_lbl, 0)
        focus_row = QHBoxLayout()
        focus_row.setContentsMargins(0, 0, 0, 0)
        focus_row.setSpacing(8)
        self.prev_key_btn = make_secondary_button("◀", self._select_previous_key)
        self.prev_key_btn.setFixedWidth(32)
        self.next_key_btn = make_secondary_button("▶", self._select_next_key)
        self.next_key_btn.setFixedWidth(32)
        self.focus_key_chip = StatusPill("K01")
        self.focus_key_chip.set_level("info")
        focus_row.addWidget(self.prev_key_btn, 0)
        focus_row.addWidget(self.focus_key_chip, 0)
        focus_row.addWidget(self.next_key_btn, 0)
        focus_wrap.addLayout(focus_row)
        outer.addLayout(focus_wrap, 0)

        # Actions
        outer.addWidget(make_secondary_button("Refresh", self._refresh_active), 0)
        outer.addWidget(make_primary_button("Sync Now", self._save_to_flash), 0)

        return bar

    def _create_pages(self) -> dict[str, QWidget]:
        s = self.session
        d = s.device
        return {
            "overview":    OverviewPage(s, controller=self),
            "settings":    AppSettingsPage(s, controller=self),
            "keyboard":    KeyboardPage(s),
            "calibration": CalibrationPage(s),
            "travel":      TravelPage(s),
            "gamepad":     GamepadPage(s),
            "rotary":      RotaryEncoderPage(d, controller=self),
            "lighting":    LightingPage(d, controller=self),
            "effects":     EffectsPage(d, controller=self),
            "device":      DevicePage(d, controller=self),
            "raw_adc":     RawADCPage(s),
            "debug":       DebugPage(s),
            "graph":       GraphPage(s),
            "firmware":    FirmwarePage(d, controller=self),
        }

    # ------------------------------------------------------------------
    # Signal connections
    # ------------------------------------------------------------------

    def _connect_signals(self) -> None:
        self.session.statusChanged.connect(self._on_status_changed)
        self.session.snapshotChanged.connect(self._on_snapshot_changed)
        self.session.connectionChanged.connect(self._on_connection_changed)
        self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
        self.session.selectedKeyChanged.connect(self._on_selected_key_changed)

    def _on_status_changed(self, message: str, level: str) -> None:
        self.status_pill.setText(message)
        self.status_pill.set_level(level)

    def _on_snapshot_changed(self, snapshot: dict) -> None:
        fw = snapshot.get("firmware_version") or "—"
        self.fw_label.setText(f"FW {fw}")

        options = snapshot.get("options") or {}
        kbd = "On" if options.get("keyboard_enabled") else "Off"
        pad = "On" if options.get("gamepad_enabled") else "Off"
        nkro = "On" if snapshot.get("nkro_enabled") else "Off"
        brightness = snapshot.get("brightness")

        self.status_pill.setText(
            f"KBD {kbd}  ·  PAD {pad}  ·  NKRO {nkro}"
            + (f"  ·  Brightness {brightness}" if brightness is not None else "")
        )

    def _on_connection_changed(self, connected: bool) -> None:
        self.connection_dot.setProperty("connected", "true" if connected else "false")
        self.connection_dot.style().unpolish(self.connection_dot)
        self.connection_dot.style().polish(self.connection_dot)
        if not connected:
            self._last_host_volume_level = None
            self._last_host_volume_push_monotonic = 0.0
            self._last_audio_spectrum = None
            self._last_audio_spectrum_push_monotonic = 0.0
            self._last_audio_spectrum_source = "none"

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.live_timer.start(max(20, int(interval_ms)))
        else:
            self.live_timer.stop()

    def _on_selected_key_changed(self, key_index: int) -> None:
        self.focus_key_chip.setText(f"K{int(key_index) + 1:02d}")

    def _on_live_tick(self) -> None:
        self.session.refresh_snapshot()
        if not self.session.connected:
            return
        if self.active_page_id and self.active_page_id in self.pages:
            page = self.pages[self.active_page_id]
            if hasattr(page, "on_live_tick"):
                try:
                    page.on_live_tick()
                except Exception:
                    pass

    def _on_volume_tick(self) -> None:
        if not self.session.connected:
            return

        now = time.monotonic()
        level = get_default_render_volume_level()
        if level is not None:
            should_push = (
                self._last_host_volume_level is None
                or self._last_host_volume_level != level
                or (now - self._last_host_volume_push_monotonic) >= 0.75
            )

            if should_push:
                try:
                    if self.session.device.led_set_volume_overlay(level):
                        self._last_host_volume_level = level
                        self._last_host_volume_push_monotonic = now
                except Exception:
                    pass

        spectrum = get_default_render_spectrum_levels(16)
        if spectrum is not None:
            self._last_audio_spectrum_source = get_last_render_spectrum_source()
            impact = min(255, int((max(spectrum) if spectrum else 0) * 1.6))
            should_push_spectrum = (
                self._last_audio_spectrum is None
                or self._last_audio_spectrum != spectrum
                or (now - self._last_audio_spectrum_push_monotonic) >= 0.04
            )

            if should_push_spectrum:
                try:
                    if self.session.device.led_set_audio_spectrum(spectrum, impact):
                        self._last_audio_spectrum = list(spectrum)
                        self._last_audio_spectrum_push_monotonic = now
                except Exception:
                    pass
        elif self._last_audio_spectrum is not None and (
            now - self._last_audio_spectrum_push_monotonic
        ) >= 0.30:
            try:
                if self.session.device.led_clear_audio_spectrum():
                    self._last_audio_spectrum = None
                    self._last_audio_spectrum_push_monotonic = now
                    self._last_audio_spectrum_source = "none"
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Navigation
    # ------------------------------------------------------------------

    _PAGE_TITLES: dict[str, str] = {
        "overview":    "Overview",
        "settings":    "App Settings",
        "keyboard":    "Keyboard",
        "calibration": "Calibration",
        "travel":      "Travel",
        "gamepad":     "Gamepad",
        "lighting":    "Lighting",
        "effects":     "Effects",
        "device":      "Device",
        "raw_adc":     "82 Raw ADC",
        "debug":       "Debug / Sensors",
        "graph":       "Live Graph",
        "firmware":    "Firmware",
    }

    def show_page(self, page_id: str) -> None:
        if page_id not in self.pages:
            return

        # Deactivate previous page
        if self.active_page_id and self.active_page_id in self.pages:
            prev = self.pages[self.active_page_id]
            if hasattr(prev, "on_page_deactivated"):
                prev.on_page_deactivated()

        self.active_page_id = page_id
        self.page_stack.setCurrentWidget(self.pages[page_id])

        # Update sidebar highlight
        for pid, btn in self.nav_buttons.items():
            active = pid == page_id
            btn.setProperty("active", "true" if active else "false")
            btn.style().unpolish(btn)
            btn.style().polish(btn)

        # Update action bar title
        self.page_title_label.setText(self._PAGE_TITLES.get(page_id, page_id.title()))

        # Activate new page
        page = self.pages[page_id]
        if hasattr(page, "on_page_activated"):
            page.on_page_activated()
        if hasattr(page, "reload"):
            page.reload()

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def _refresh_active(self) -> None:
        self.session.refresh_snapshot()
        if not self.session.connected:
            return
        if self.active_page_id and self.active_page_id in self.pages:
            page = self.pages[self.active_page_id]
            if hasattr(page, "reload"):
                page.reload()
        self.session.set_status("Page refreshed from device.", "ok")

    def _refresh_all(self) -> None:
        self.session.refresh_snapshot()

    def _select_previous_key(self) -> None:
        selected = int(getattr(self.session, "selected_key", 0))
        self.session.set_selected_key(max(0, selected - 1))

    def _select_next_key(self) -> None:
        selected = int(getattr(self.session, "selected_key", 0))
        self.session.set_selected_key(min(81, selected + 1))
        if not self.session.connected:
            return
        for page in self.pages.values():
            try:
                if hasattr(page, "reload"):
                    page.reload()
            except Exception:
                pass
        self.session.set_status("All pages refreshed from device.", "ok")

    def _save_to_flash(self) -> None:
        try:
            if not self.session.device.save_settings():
                raise RuntimeError("device rejected save")
        except Exception as exc:
            self.session.set_status(f"Immediate flash sync failed: {exc}", "error")
            return
        self.session.set_status("Settings flushed to flash immediately.", "success")
        self.session.refresh_snapshot()

    def set_status(self, message: str, level: str = "info") -> None:
        self.session.set_status(message, level)

    def get_close_effect_enabled(self) -> bool:
        return bool(self.app_prefs.close_effect_enabled)

    def set_close_effect_enabled(self, enabled: bool) -> None:
        self.app_prefs.set_close_effect_enabled(bool(enabled))

    def get_close_effect_mode(self) -> int:
        return int(self.app_prefs.close_effect_mode)

    def set_close_effect_mode(self, mode: int) -> None:
        self.app_prefs.set_close_effect_mode(int(mode))

    def get_restore_previous_effect_on_startup(self) -> bool:
        return bool(self.app_prefs.restore_previous_on_startup)

    def set_restore_previous_effect_on_startup(self, enabled: bool) -> None:
        self.app_prefs.set_restore_previous_on_startup(bool(enabled))

    def _apply_pending_led_restore_if_needed(self) -> None:
        if not self.app_prefs.restore_previous_on_startup:
            return
        pending = self.app_prefs.get_pending_restore()
        if pending is None or not self.session.connected:
            return

        mode, params = pending
        try:
            if params is not None:
                self.session.device.set_led_effect_params(mode, params)
            if not self.session.device.set_led_effect(mode):
                raise RuntimeError("device rejected LED effect restore")
            self.app_prefs.clear_pending_restore()
            self.session.set_status("Restored the previous LED effect from the last app close.", "info")
            self.session.refresh_snapshot()
        except Exception as exc:
            self.session.set_status(f"Failed to restore previous LED effect: {exc}", "warning")

    def _apply_close_effect_before_exit(self) -> None:
        if not self.app_prefs.close_effect_enabled or not self.session.connected:
            return

        target_mode = int(self.app_prefs.close_effect_mode)
        try:
            previous_mode = self.session.device.get_led_effect()
            should_store_restore = (
                self.app_prefs.restore_previous_on_startup
                and previous_mode is not None
                and int(previous_mode) != target_mode
            )

            previous_params = None
            if should_store_restore:
                previous_params = self.session.device.get_led_effect_params(previous_mode)

            try:
                self.session.device.led_clear_volume_overlay()
            except Exception:
                pass
            try:
                self.session.device.led_clear_audio_spectrum()
            except Exception:
                pass

            if not self.session.device.set_led_effect(target_mode):
                return

            if should_store_restore:
                self.app_prefs.set_pending_restore(int(previous_mode), previous_params)
            else:
                self.app_prefs.clear_pending_restore()
        except Exception:
            pass

    # ------------------------------------------------------------------
    # Theme switching
    # ------------------------------------------------------------------

    def _set_theme(self, mode: str) -> None:
        self.theme_mode = mode
        apply_app_style(QApplication.instance(), mode)
        for m, btn in self._theme_btns.items():
            btn.setProperty("active", "true" if m == mode else "false")
            btn.style().unpolish(btn)
            btn.style().polish(btn)
        for page in self.pages.values():
            if hasattr(page, "apply_theme"):
                page.apply_theme()
        self.repaint()

    # ------------------------------------------------------------------
    # Window lifecycle
    # ------------------------------------------------------------------

    def closeEvent(self, event) -> None:
        self.live_timer.stop()
        self.volume_timer.stop()
        self._apply_close_effect_before_exit()
        if self.session.connected:
            try:
                self.session.device.led_clear_volume_overlay()
            except Exception:
                pass
            try:
                self.session.device.led_clear_audio_spectrum()
            except Exception:
                pass
        if self.active_page_id and self.active_page_id in self.pages:
            page = self.pages[self.active_page_id]
            if hasattr(page, "on_page_deactivated"):
                try:
                    page.on_page_deactivated()
                except Exception:
                    pass
        super().closeEvent(event)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def launch_gui(device) -> int:
    if not HAS_GUI:
        raise RuntimeError("PySide6 is not available")

    app = QApplication.instance()
    owns_app = app is None
    if app is None:
        app = QApplication(sys.argv)

    apply_app_style(app, "system")

    window = KBHEQtMainWindow(device)
    window.show()

    return app.exec() if owns_app else 0
