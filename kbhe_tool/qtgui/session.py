from __future__ import annotations

from .common import HAS_GUI, QObject, Signal


class AppSession(QObject):
    statusChanged = Signal(str, str)
    selectedKeyChanged = Signal(int)
    snapshotChanged = Signal(dict)
    connectionChanged = Signal(bool)
    liveSettingsChanged = Signal(bool, int)

    def __init__(self, device):
        if not HAS_GUI:
            raise RuntimeError("PySide6 is not available")
        super().__init__()
        self.device = device
        self.selected_key = 0
        self.connected = True
        self.snapshot = {}
        self.live_enabled = True
        self.live_interval_ms = 100
        self._last_refresh_error: str | None = None

    def set_status(self, message: str, level: str = "info") -> None:
        self.statusChanged.emit(message, level)

    def set_selected_key(self, key_index: int) -> None:
        key_index = max(0, min(5, int(key_index)))
        if key_index == self.selected_key:
            return
        self.selected_key = key_index
        self.selectedKeyChanged.emit(key_index)

    def set_live_enabled(self, enabled: bool) -> None:
        enabled = bool(enabled)
        if enabled == self.live_enabled:
            return
        self.live_enabled = enabled
        self.liveSettingsChanged.emit(self.live_enabled, self.live_interval_ms)

    def set_live_interval_ms(self, interval_ms: int) -> None:
        interval_ms = max(20, min(2000, int(interval_ms)))
        if interval_ms == self.live_interval_ms:
            return
        self.live_interval_ms = interval_ms
        self.liveSettingsChanged.emit(self.live_enabled, self.live_interval_ms)

    def set_live_settings(self, enabled: bool, interval_ms: int) -> None:
        enabled = bool(enabled)
        interval_ms = max(20, min(2000, int(interval_ms)))
        if enabled == self.live_enabled and interval_ms == self.live_interval_ms:
            return
        self.live_enabled = enabled
        self.live_interval_ms = interval_ms
        self.liveSettingsChanged.emit(self.live_enabled, self.live_interval_ms)

    def _set_connection(self, connected: bool) -> None:
        connected = bool(connected)
        if connected == self.connected:
            return
        self.connected = connected
        self.connectionChanged.emit(connected)

    def _collect_snapshot(self) -> dict:
        snapshot = {
            "firmware_version": self.device.get_firmware_version(),
            "options": self.device.get_options() or {},
            "led_enabled": self.device.led_get_enabled(),
            "brightness": self.device.led_get_brightness(),
            "nkro_enabled": self.device.get_nkro_enabled(),
        }

        # Treat a full set of empty responses as a failed transport.
        if (
            snapshot["firmware_version"] is None
            and not snapshot["options"]
            and snapshot["led_enabled"] is None
            and snapshot["brightness"] is None
            and snapshot["nkro_enabled"] is None
        ):
            raise RuntimeError("no response from device")

        return snapshot

    def _try_reconnect(self) -> None:
        if hasattr(self.device, "reconnect"):
            self.device.reconnect(logger=None)
            return
        if hasattr(self.device, "disconnect"):
            self.device.disconnect()
        self.device.connect(logger=None)

    def refresh_snapshot(self) -> dict:
        snapshot = {}
        try:
            snapshot = self._collect_snapshot()
        except Exception as exc:
            # One reconnect attempt to recover from unplug/replug without restart.
            try:
                self._try_reconnect()
                snapshot = self._collect_snapshot()
            except Exception as reconnect_exc:
                self._set_connection(False)
                message = f"Device refresh failed: {reconnect_exc}"
                if message != self._last_refresh_error:
                    self._last_refresh_error = message
                    self.set_status(message, "danger")
                return snapshot

        self._set_connection(True)
        self._last_refresh_error = None
        self.snapshot = snapshot
        self.snapshotChanged.emit(snapshot)
        return snapshot
