from __future__ import annotations

import pathlib

from PySide6.QtCore import QSignalBlocker, Qt
from PySide6.QtWidgets import (
    QCheckBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QVBoxLayout,
    QWidget,
)

from ...protocol import KEY_COUNT
from ..widgets import (
    PageScaffold,
    SectionCard,
    SubCard,
    StatusChip,
    make_danger_button,
    make_primary_button,
    make_secondary_button,
)


def _clamp(value: int) -> int:
    return max(0, min(255, int(value)))


class DevicePage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self._build_state()
        self._build_ui()
        self.reload()

    # ------------------------------------------------------------------
    # State init
    # ------------------------------------------------------------------

    def _build_state(self) -> None:
        owner = self.controller
        if owner is not None and hasattr(owner, "pixels"):
            self.pixels = [list(pixel[:3]) for pixel in getattr(owner, "pixels")]
        else:
            self.pixels = [[0, 0, 0] for _ in range(KEY_COUNT)]
            if owner is not None:
                try:
                    setattr(owner, "pixels", self.pixels)
                except Exception:
                    pass
        if len(self.pixels) < KEY_COUNT:
            self.pixels.extend([[0, 0, 0] for _ in range(KEY_COUNT - len(self.pixels))])
        self.pixels = self.pixels[:KEY_COUNT]

        if owner is not None and hasattr(owner, "brightness"):
            self.brightness = int(getattr(owner, "brightness"))
        else:
            self.brightness = 50
            if owner is not None:
                try:
                    setattr(owner, "brightness", self.brightness)
                except Exception:
                    pass

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Device",
            "Inspect boot-time defaults, persist live LED state, and handle recovery actions from one place.",
        )
        root.addWidget(scaffold, 1)

        scaffold.add_card(self._build_info_card())
        scaffold.add_card(self._build_interfaces_card())

        # Actions row: save / reset / file
        actions_row = QHBoxLayout()
        actions_row.setSpacing(14)
        actions_row.addWidget(self._build_save_card(), 1)
        actions_row.addWidget(self._build_reset_card(), 1)
        actions_row.addWidget(self._build_file_card(), 1)
        scaffold.content_layout.addLayout(actions_row)

        self.status_chip = StatusChip("Device page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

    def _build_info_card(self) -> SectionCard:
        card = SectionCard("Device Summary")
        self.firmware_label = QLabel("Firmware Version: Loading…")
        self.firmware_label.setObjectName("Muted")
        self.brightness_label = QLabel("Brightness: --")
        self.brightness_label.setObjectName("Muted")
        self.led_count_label = QLabel("Lit pixels: --")
        self.led_count_label.setObjectName("Muted")
        card.body_layout.addWidget(self.firmware_label)
        card.body_layout.addWidget(self.brightness_label)
        card.body_layout.addWidget(self.led_count_label)
        return card

    def _build_interfaces_card(self) -> SectionCard:
        card = SectionCard(
            "HID Interfaces",
            "Keyboard HID, Gamepad HID, and NKRO are saved immediately. "
            "LED matrix enable stays live until you explicitly save it.",
        )

        self.keyboard_enabled = QCheckBox("Keyboard HID (sends keypresses)")
        self.keyboard_enabled.stateChanged.connect(self.on_keyboard_enabled_change)
        card.body_layout.addWidget(self.keyboard_enabled)

        self.gamepad_enabled = QCheckBox("Gamepad HID (analog axes)")
        self.gamepad_enabled.stateChanged.connect(self.on_gamepad_enabled_change)
        card.body_layout.addWidget(self.gamepad_enabled)

        self.led_enabled = QCheckBox("LED Matrix (WS2812)")
        self.led_enabled.stateChanged.connect(self.on_led_enabled_change)
        card.body_layout.addWidget(self.led_enabled)

        # NKRO sub-section
        nkro_sub = SubCard()
        nkro_title = QLabel("Keyboard Mode")
        nkro_title.setObjectName("CardTitle")
        nkro_sub.layout.addWidget(nkro_title)
        nkro_desc = QLabel("NKRO uses an independent HID interface for unlimited simultaneous keys.")
        nkro_desc.setObjectName("Muted")
        nkro_desc.setWordWrap(True)
        nkro_sub.layout.addWidget(nkro_desc)
        self.nkro_enabled = QCheckBox("Enable NKRO mode")
        self.nkro_enabled.stateChanged.connect(self.on_nkro_enabled_change)
        nkro_sub.layout.addWidget(self.nkro_enabled)
        card.body_layout.addWidget(nkro_sub)

        return card

    def _build_save_card(self) -> SectionCard:
        card = SectionCard(
            "Save Settings",
            "Persists the LED matrix state stored in RAM: LED enable, brightness, "
            "and the current 82-LED pattern.",
        )
        card.body_layout.addWidget(
            make_primary_button("Save All Settings to Flash", self.save_to_device)
        )
        return card

    def _build_reset_card(self) -> SectionCard:
        card = SectionCard(
            "Factory Reset",
            "Restores the device to factory defaults and clears the current "
            "lighting configuration.",
        )
        card.body_layout.addWidget(make_danger_button("Factory Reset", self.factory_reset))
        return card

    def _build_file_card(self) -> SectionCard:
        card = SectionCard(
            "Lighting File",
            "Import or export the LED pattern in the current .led file format.",
        )
        row = QHBoxLayout()
        row.setSpacing(8)
        row.addWidget(make_secondary_button("Export", self.export_to_file))
        row.addWidget(make_secondary_button("Import", self.import_from_file))
        row.addStretch(1)
        card.body_layout.addLayout(row)
        return card

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _update_status(self, message: str, kind: str = "info") -> None:
        level_map = {"success": "ok", "warning": "warn", "error": "bad", "info": "info"}
        self.status_chip.set_text_and_level(message, level_map.get(kind, "neutral"))
        if self.controller is not None and hasattr(self.controller, "set_status"):
            try:
                self.controller.set_status(message, kind)
            except TypeError:
                self.controller.set_status(message)
            except Exception:
                pass

    def _sync_shared(self) -> None:
        if self.controller is None:
            return
        for name, value in (("pixels", self.pixels), ("brightness", self.brightness)):
            try:
                setattr(self.controller, name, value)
            except Exception:
                pass

    def _serialize_pixels(self) -> list[int]:
        payload = []
        for pixel in self.pixels[:KEY_COUNT]:
            payload.extend(_clamp(ch) for ch in pixel[:3])
        return payload[: KEY_COUNT * 3]

    def _load_pixels(self, payload: list[int]) -> bool:
        expected_size = KEY_COUNT * 3
        if len(payload) < expected_size:
            return False
        for i in range(KEY_COUNT):
            base = i * 3
            self.pixels[i] = [payload[base], payload[base + 1], payload[base + 2]]
        self._sync_shared()
        return True

    def _set_check(self, widget: QCheckBox, checked: bool) -> None:
        with QSignalBlocker(widget):
            widget.setChecked(bool(checked))

    # ------------------------------------------------------------------
    # Data loading
    # ------------------------------------------------------------------

    def reload(self) -> None:
        errors = []
        try:
            version = self.device.get_firmware_version()
            self.firmware_label.setText(
                f"Firmware Version: {version if version else 'Unknown'}"
            )
        except Exception as exc:
            errors.append(f"firmware version: {exc}")

        try:
            brightness = self.device.led_get_brightness()
            if brightness is not None:
                self.brightness = int(brightness)
                self._sync_shared()
                self.brightness_label.setText(f"Brightness: {self.brightness}")
        except Exception as exc:
            errors.append(f"brightness: {exc}")

        try:
            led_enabled = self.device.led_get_enabled()
            if led_enabled is not None:
                self._set_check(self.led_enabled, led_enabled)
        except Exception as exc:
            errors.append(f"LED enabled: {exc}")

        try:
            options = self.device.get_options() or {}
            if "keyboard_enabled" in options:
                self._set_check(self.keyboard_enabled, options["keyboard_enabled"])
            if "gamepad_enabled" in options:
                self._set_check(self.gamepad_enabled, options["gamepad_enabled"])
        except Exception as exc:
            errors.append(f"options: {exc}")

        try:
            nkro_enabled = self.device.get_nkro_enabled()
            if nkro_enabled is not None:
                self._set_check(self.nkro_enabled, nkro_enabled)
        except Exception as exc:
            errors.append(f"NKRO: {exc}")

        try:
            pixels = self.device.led_download_all()
            expected_size = KEY_COUNT * 3
            if pixels and len(pixels) >= expected_size:
                self._load_pixels(pixels[:expected_size])
                lit = sum(1 for p in self.pixels if any(ch for ch in p[:3]))
                self.led_count_label.setText(f"Lit pixels: {lit} / {KEY_COUNT}")
            elif pixels:
                errors.append(f"pixel payload too short ({len(pixels)} bytes)")
        except Exception as exc:
            errors.append(f"pixels: {exc}")

        if errors:
            self._update_status(
                "Device loaded with warnings: " + "; ".join(errors), "warning"
            )
        else:
            self._update_status("Device state loaded from firmware.", "success")

    load_from_device = reload
    refresh_from_device = reload

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def save_to_device(self) -> None:
        try:
            if not self.device.led_upload_all(self._serialize_pixels()):
                raise RuntimeError("device rejected the LED payload")
            if not self.device.save_settings():
                raise RuntimeError("device rejected the save request")
        except Exception as exc:
            self._update_status(f"Save failed: {exc}", "error")
            QMessageBox.critical(self, "Save failed", f"Could not save device settings:\n{exc}")
            return
        self._update_status("All settings saved to flash.", "success")
        QMessageBox.information(self, "Saved", "Settings saved to flash successfully.")
        self.reload()

    def export_to_file(self) -> None:
        filename, _ = QFileDialog.getSaveFileName(
            self,
            "Export LED Pattern",
            str(pathlib.Path.home() / "kbhe.led"),
            "LED Pattern (*.led);;All Files (*.*)",
        )
        if not filename:
            return
        try:
            payload = bytes([self.brightness]) + bytes(self._serialize_pixels())
            pathlib.Path(filename).write_bytes(payload)
        except Exception as exc:
            self._update_status(f"Export failed: {exc}", "error")
            QMessageBox.critical(self, "Export failed", f"Could not export the LED pattern:\n{exc}")
            return
        self._update_status(f"Exported LED pattern to {filename}.", "success")

    def import_from_file(self) -> None:
        filename, _ = QFileDialog.getOpenFileName(
            self,
            "Import LED Pattern",
            str(pathlib.Path.home()),
            "LED Pattern (*.led);;All Files (*.*)",
        )
        if not filename:
            return
        try:
            data = pathlib.Path(filename).read_bytes()
        except Exception as exc:
            self._update_status(f"Import failed: {exc}", "error")
            QMessageBox.critical(self, "Import failed", f"Could not read the LED pattern:\n{exc}")
            return
        expected_new = 1 + KEY_COUNT * 3
        expected_old = 193
        if len(data) not in (expected_new, expected_old):
            self._update_status("Import failed: invalid file format.", "error")
            QMessageBox.warning(
                self,
                "Import failed",
                f"Invalid LED pattern file.\nExpected {expected_new} bytes (82 LEDs) or {expected_old} bytes (legacy 64 LEDs).",
            )
            return

        brightness = _clamp(data[0])
        payload = list(data[1:])
        if len(data) == expected_old:
            payload.extend([0] * ((KEY_COUNT * 3) - len(payload)))
        payload = payload[: KEY_COUNT * 3]
        try:
            self.brightness = brightness
            self._sync_shared()
            self.brightness_label.setText(f"Brightness: {brightness}")
            if not self.device.led_set_brightness(brightness):
                raise RuntimeError("device rejected the brightness update")
            if not self._load_pixels(payload):
                raise RuntimeError("invalid LED payload")
            if not self.device.led_upload_all(payload):
                raise RuntimeError("device rejected the LED payload")
        except Exception as exc:
            self._update_status(f"Import failed: {exc}", "error")
            QMessageBox.critical(self, "Import failed", f"Could not import the LED pattern:\n{exc}")
            return

        self._update_status(f"Imported LED pattern from {filename}.", "success")
        QMessageBox.information(
            self,
            "Imported",
            "LED pattern imported and sent live.\n\nUse Save Settings if you want to keep it after reboot.",
        )

    # ------------------------------------------------------------------
    # Checkbox handlers
    # ------------------------------------------------------------------

    def on_led_enabled_change(self, state: int) -> None:
        _ = state
        enabled = self.led_enabled.isChecked()
        try:
            ok = self.device.led_set_enabled(enabled)
        except Exception as exc:
            self._set_check(self.led_enabled, not enabled)
            self._update_status(f"Failed to update LED enable state: {exc}", "error")
            return
        if not ok:
            self._set_check(self.led_enabled, not enabled)
            self._update_status("Device rejected the LED enable state.", "error")
            return
        self._update_status(
            f"LED matrix {'enabled' if enabled else 'disabled'} live only. "
            "Use Save Settings to keep it after reboot.",
            "success",
        )

    def on_keyboard_enabled_change(self, state: int) -> None:
        _ = state
        enabled = self.keyboard_enabled.isChecked()
        try:
            ok = self.device.set_keyboard_enabled(enabled)
        except Exception as exc:
            self._set_check(self.keyboard_enabled, not enabled)
            self._update_status(f"Failed to update keyboard HID: {exc}", "error")
            return
        if not ok:
            self._set_check(self.keyboard_enabled, not enabled)
            self._update_status("Device rejected the keyboard HID state.", "error")
            return
        self._update_status(
            f"Keyboard HID {'enabled' if enabled else 'disabled'} and saved immediately.", "success"
        )

    def on_gamepad_enabled_change(self, state: int) -> None:
        _ = state
        enabled = self.gamepad_enabled.isChecked()
        try:
            ok = self.device.set_gamepad_enabled(enabled)
        except Exception as exc:
            self._set_check(self.gamepad_enabled, not enabled)
            self._update_status(f"Failed to update gamepad HID: {exc}", "error")
            return
        if not ok:
            self._set_check(self.gamepad_enabled, not enabled)
            self._update_status("Device rejected the gamepad HID state.", "error")
            return
        self._update_status(
            f"Gamepad HID {'enabled' if enabled else 'disabled'} and saved immediately.", "success"
        )

    def on_nkro_enabled_change(self, state: int) -> None:
        _ = state
        enabled = self.nkro_enabled.isChecked()
        try:
            ok = self.device.set_nkro_enabled(enabled)
        except Exception as exc:
            self._set_check(self.nkro_enabled, not enabled)
            self._update_status(f"Failed to update NKRO mode: {exc}", "error")
            return
        if not ok:
            self._set_check(self.nkro_enabled, not enabled)
            self._update_status("Device rejected the NKRO mode state.", "error")
            return
        self._update_status(
            f"NKRO mode {'enabled' if enabled else 'disabled'} and saved immediately. "
            "USB re-enumeration may still be required.",
            "success",
        )

    def factory_reset(self) -> None:
        confirm = QMessageBox.question(
            self,
            "Factory Reset",
            "Reset all settings to factory defaults?\n\n"
            "This clears LED patterns and resets all interface options.",
        )
        if confirm != QMessageBox.Yes:
            return
        try:
            ok = self.device.factory_reset()
        except Exception as exc:
            self._update_status(f"Factory reset failed: {exc}", "error")
            QMessageBox.critical(self, "Factory reset failed", f"Factory reset failed:\n{exc}")
            return
        if not ok:
            self._update_status("Factory reset failed.", "error")
            QMessageBox.critical(
                self, "Factory reset failed", "The device rejected the factory reset request."
            )
            return
        self._update_status("Factory reset completed. Reloading settings…", "success")
        QMessageBox.information(self, "Factory reset", "Factory reset complete.\n\nReloading settings…")
        self.reload()

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
