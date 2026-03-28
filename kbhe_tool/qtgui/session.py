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

    def refresh_snapshot(self) -> dict:
        snapshot = {}
        try:
            snapshot["firmware_version"] = self.device.get_firmware_version()
            snapshot["options"] = self.device.get_options() or {}
            snapshot["led_enabled"] = self.device.led_get_enabled()
            snapshot["brightness"] = self.device.led_get_brightness()
            snapshot["nkro_enabled"] = self.device.get_nkro_enabled()
            self.connected = True
            self.connectionChanged.emit(True)
        except Exception as exc:
            self.connected = False
            self.connectionChanged.emit(False)
            self.set_status(f"Device refresh failed: {exc}", "danger")
        else:
            self.snapshot = snapshot
            self.snapshotChanged.emit(snapshot)
        return snapshot
