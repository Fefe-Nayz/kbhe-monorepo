from __future__ import annotations

from .common import HAS_GUI, QObject, Signal


class AppSession(QObject):
    statusChanged = Signal(str, str)
    selectedKeyChanged = Signal(int)
    snapshotChanged = Signal(dict)
    connectionChanged = Signal(bool)

    def __init__(self, device):
        if not HAS_GUI:
            raise RuntimeError("PySide6 is not available")
        super().__init__()
        self.device = device
        self.selected_key = 0
        self.connected = True
        self.snapshot = {}

    def set_status(self, message: str, level: str = "info") -> None:
        self.statusChanged.emit(message, level)

    def set_selected_key(self, key_index: int) -> None:
        key_index = max(0, min(5, int(key_index)))
        if key_index == self.selected_key:
            return
        self.selected_key = key_index
        self.selectedKeyChanged.emit(key_index)

    def _set_connection(self, connected: bool) -> None:
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
                self.set_status(f"Device refresh failed: {reconnect_exc}", "danger")
                return snapshot

        self._set_connection(True)
        self.snapshot = snapshot
        self.snapshotChanged.emit(snapshot)
        return snapshot
