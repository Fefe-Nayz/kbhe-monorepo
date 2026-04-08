from __future__ import annotations

import pathlib

from PySide6.QtCore import QSignalBlocker, Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QColorDialog,
    QFileDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QSlider,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from ...key_layout import key_display_name, key_short_label
from ...protocol import KEY_COUNT
from ..widgets import (
    KeyboardLayoutWidget,
    PageScaffold,
    SectionCard,
    StatusChip,
    make_danger_button,
    make_primary_button,
    make_secondary_button,
)

QUICK_COLORS = [
    ("#ff3b30", "Red"),
    ("#34c759", "Green"),
    ("#0a84ff", "Blue"),
    ("#ffffff", "White"),
    ("#ffd60a", "Yellow"),
    ("#ff2d55", "Magenta"),
    ("#32ade6", "Cyan"),
    ("#000000", "Black"),
    ("#ff9500", "Orange"),
    ("#a855f7", "Purple"),
    ("#64d2ff", "Sky"),
    ("#5e5ce6", "Indigo"),
]

LED_EFFECT_MATRIX_SOFTWARE = 0
LED_EFFECT_THIRD_PARTY = 14


def _clamp(value: int) -> int:
    return max(0, min(255, int(value)))


def _rgb_to_hex(rgb) -> str:
    r, g, b = (_clamp(ch) for ch in rgb[:3])
    return f"#{r:02x}{g:02x}{b:02x}"


def _hex_text_color(hex_color: str) -> str:
    color = QColor(hex_color)
    luminance = (
        0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF()
    )
    return "#0f172a" if luminance >= 0.62 else "#f8fafc"


class LightingPage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self.session = getattr(controller, "session", None)
        self._page_active = False
        self._current_effect_mode = LED_EFFECT_MATRIX_SOFTWARE
        self._matrix_editable = True
        self._last_live_sync_error = None
        self._layout_edit_widgets: list[QWidget] = []
        self._build_state()
        self._build_ui()
        self.reload()

        if self.session is not None and hasattr(self.session, "selectedKeyChanged"):
            try:
                self.session.selectedKeyChanged.connect(self._on_selected_key_changed)
            except Exception:
                pass
        self._on_selected_key_changed(getattr(self.session, "selected_key", 0))

    def _build_state(self) -> None:
        owner = self.controller
        target_led_count = KEY_COUNT

        if owner is not None and hasattr(owner, "pixels"):
            existing = [list(pixel[:3]) for pixel in getattr(owner, "pixels")]
            if len(existing) < target_led_count:
                existing.extend([[0, 0, 0] for _ in range(target_led_count - len(existing))])
            self.pixels = existing[:target_led_count]
        else:
            self.pixels = [[0, 0, 0] for _ in range(target_led_count)]
        if owner is not None:
            try:
                setattr(owner, "pixels", self.pixels)
            except Exception:
                pass

        if owner is not None and hasattr(owner, "current_color"):
            self.current_color = list(getattr(owner, "current_color"))
        else:
            self.current_color = [255, 0, 0]
        if owner is not None:
            try:
                setattr(owner, "current_color", self.current_color)
            except Exception:
                pass

        if owner is not None and hasattr(owner, "brightness"):
            self.brightness = int(getattr(owner, "brightness"))
        else:
            self.brightness = 50
        if owner is not None:
            try:
                setattr(owner, "brightness", self.brightness)
            except Exception:
                pass

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Lighting",
            "Éditeur LED physique 82 touches. Le layout suit le vrai clavier au lieu d’un faux 8×8.",
        )
        root.addWidget(scaffold, 1)

        columns = QHBoxLayout()
        columns.setSpacing(14)
        columns.setAlignment(Qt.AlignTop)
        scaffold.content_layout.addLayout(columns)

        columns.addWidget(self._build_layout_card(), 2, Qt.AlignTop)
        columns.addLayout(self._build_right_column(), 1)

        self.status_chip = StatusChip("Lighting page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

        self._refresh_layout()

    def _build_layout_card(self) -> SectionCard:
        card = SectionCard(
            "Keyboard LED Layout",
            "Clique une touche pour la peindre avec la couleur courante. En mode effet actif, la vue devient read-only et reflète la frame live.",
        )
        self.matrix_mode_chip = StatusChip("Matrix mode (editable)", "ok")

        info_row = QHBoxLayout()
        info_row.setSpacing(8)
        info_row.addWidget(self.matrix_mode_chip)
        self.selected_led_chip = StatusChip("K01", "info")
        self.selected_led_rgb_chip = StatusChip("RGB 0,0,0", "neutral")
        info_row.addWidget(self.selected_led_chip)
        info_row.addWidget(self.selected_led_rgb_chip)
        info_row.addStretch(1)
        card.body_layout.addLayout(info_row)

        self.layout_view = KeyboardLayoutWidget(self.session, unit=34)
        self.layout_view.keyClicked.connect(self.on_led_click)
        card.body_layout.addWidget(self.layout_view, 0, Qt.AlignTop | Qt.AlignLeft)

        hint = QLabel("Le layout suit l’ordre physique des touches. Clique une touche pour la peindre avec la couleur courante.")
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        card.body_layout.addWidget(hint)
        return card

    def _build_right_column(self) -> QVBoxLayout:
        col = QVBoxLayout()
        col.setSpacing(14)
        col.addWidget(self._build_color_card())
        col.addWidget(self._build_brightness_card())
        col.addWidget(self._build_actions_card())
        col.addWidget(self._build_persistence_card())
        col.addStretch(1)
        return col

    def _build_color_card(self) -> SectionCard:
        card = SectionCard("Paint Color")

        self.color_preview = QFrame()
        self.color_preview.setFixedHeight(48)
        self.color_preview.setStyleSheet(
            "border-radius: 12px; border: 1px solid palette(mid); background: #ff3b30;"
        )
        card.body_layout.addWidget(self.color_preview)

        self.color_label = QLabel("RGB(255, 0, 0)")
        self.color_label.setObjectName("Muted")
        card.body_layout.addWidget(self.color_label)

        self.choose_color_btn = make_secondary_button("Choose Color…", self.pick_color)
        card.body_layout.addWidget(self.choose_color_btn)
        self._register_layout_edit_widget(self.choose_color_btn)

        swatch_host = QWidget()
        swatch_grid = QGridLayout(swatch_host)
        swatch_grid.setContentsMargins(0, 0, 0, 0)
        swatch_grid.setHorizontalSpacing(6)
        swatch_grid.setVerticalSpacing(6)

        for i, (hex_color, label) in enumerate(QUICK_COLORS):
            swatch = QToolButton()
            swatch.setText(label)
            swatch.setToolTip(hex_color)
            swatch.setCursor(Qt.PointingHandCursor)
            swatch.setFixedSize(76, 34)
            swatch.setStyleSheet(
                f"QToolButton {{"
                f"  background: {hex_color};"
                f"  color: {_hex_text_color(hex_color)};"
                f"  border: 1px solid rgba(0,0,0,0.15);"
                f"  border-radius: 8px;"
                f"  font-weight: 600;"
                f"}}"
                "QToolButton:hover { border-color: palette(highlight); }"
            )
            swatch.clicked.connect(lambda _=False, h=hex_color: self.set_color_hex(h))
            self._register_layout_edit_widget(swatch)
            swatch_grid.addWidget(swatch, i // 4, i % 4)

        card.body_layout.addWidget(swatch_host)
        return card

    def _build_brightness_card(self) -> SectionCard:
        card = SectionCard("Brightness", "Appliquée immédiatement au clavier.")
        self.brightness_slider = QSlider(Qt.Horizontal)
        self.brightness_slider.setRange(0, 255)
        self.brightness_slider.valueChanged.connect(self.on_brightness_change)
        card.body_layout.addWidget(self.brightness_slider)

        self.brightness_value = QLabel("50")
        self.brightness_value.setAlignment(Qt.AlignCenter)
        self.brightness_value.setObjectName("CardTitle")
        card.body_layout.addWidget(self.brightness_value)
        return card

    def _build_actions_card(self) -> SectionCard:
        card = SectionCard("Live Actions", "Ces actions modifient immédiatement la frame live.")
        self.fill_btn = make_primary_button("Fill with Current Color", self.fill_color)
        self.clear_btn = make_danger_button("Clear All LEDs", self.clear_all)
        self.rainbow_btn = make_secondary_button("Run Rainbow Test", self.rainbow_test)
        self.reload_btn = make_secondary_button("Reload from Device", self.reload)
        card.body_layout.addWidget(self.fill_btn)
        card.body_layout.addWidget(self.clear_btn)
        card.body_layout.addWidget(self.rainbow_btn)
        card.body_layout.addWidget(self.reload_btn)
        self._register_layout_edit_widget(self.fill_btn)
        self._register_layout_edit_widget(self.clear_btn)
        self._register_layout_edit_widget(self.rainbow_btn)
        return card

    def _build_persistence_card(self) -> SectionCard:
        card = SectionCard(
            "Persistence",
            "Sauvegarde l’état LED courant dans le flash. Le format `.led` exporte maintenant 82 RGB + brightness.",
        )
        self.save_to_flash_btn = make_primary_button("Save to Flash", self.save_to_flash)
        card.body_layout.addWidget(self.save_to_flash_btn)
        self._register_layout_edit_widget(self.save_to_flash_btn)

        row = QHBoxLayout()
        row.setSpacing(8)
        self.export_btn = make_secondary_button("Export", self.export_to_file)
        self.import_btn = make_secondary_button("Import", self.import_from_file)
        row.addWidget(self.export_btn)
        row.addWidget(self.import_btn)
        row.addStretch(1)
        card.body_layout.addLayout(row)
        self._register_layout_edit_widget(self.import_btn)
        return card

    def _register_layout_edit_widget(self, widget: QWidget) -> None:
        self._layout_edit_widgets.append(widget)

    @staticmethod
    def _is_matrix_edit_mode(effect_mode: int | None) -> bool:
        return int(effect_mode) == LED_EFFECT_MATRIX_SOFTWARE if effect_mode is not None else False

    def _set_layout_editable(self, editable: bool, effect_mode: int | None = None) -> None:
        self._matrix_editable = bool(editable)
        self.layout_view.set_interactive(self._matrix_editable)
        for widget in self._layout_edit_widgets:
            widget.setEnabled(self._matrix_editable)

        mode = int(effect_mode) if effect_mode is not None else self._current_effect_mode
        if self._matrix_editable:
            self.matrix_mode_chip.set_text_and_level("Matrix mode (editable)", "ok")
        elif mode == LED_EFFECT_THIRD_PARTY:
            self.matrix_mode_chip.set_text_and_level("Third-party mode (read-only live)", "info")
        else:
            self.matrix_mode_chip.set_text_and_level("Effect mode (read-only live)", "info")

    def _sync_effect_mode_from_device(self) -> int | None:
        try:
            mode = self.device.get_led_effect()
        except Exception as exc:
            message = f"Failed to read effect mode: {exc}"
            if message != self._last_live_sync_error:
                self._last_live_sync_error = message
                self._update_status(message, "warning")
            return None

        if mode is None:
            return None

        self._current_effect_mode = int(mode)
        self._set_layout_editable(
            self._is_matrix_edit_mode(self._current_effect_mode),
            self._current_effect_mode,
        )
        return self._current_effect_mode

    def _deny_edit_if_read_only(self) -> bool:
        # Re-sync the effect mode first so matrix edits immediately follow
        # changes made from the Effects page or another controller surface.
        self._sync_effect_mode_from_device()
        if self._matrix_editable:
            return False
        if self._current_effect_mode == LED_EFFECT_THIRD_PARTY:
            self._update_status(
                "LED editing is disabled in Third-party mode. Showing live LED state only.",
                "warning",
            )
        else:
            self._update_status(
                "LED editing is disabled while an effect is active. Switch to Matrix mode to edit.",
                "warning",
            )
        return True

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
        for name, value in (
            ("pixels", self.pixels),
            ("current_color", self.current_color),
            ("brightness", self.brightness),
        ):
            try:
                setattr(self.controller, name, value)
            except Exception:
                pass

    def _global_live_enabled(self) -> bool:
        if self.session is None:
            return False
        return bool(getattr(self.session, "live_enabled", False))

    def _set_current_color(self, rgb: list[int]) -> None:
        self.current_color = [_clamp(ch) for ch in rgb[:3]]
        self._sync_shared()
        hex_color = _rgb_to_hex(self.current_color)
        self.color_preview.setStyleSheet(
            f"border-radius: 12px; border: 1px solid palette(mid); background: {hex_color};"
        )
        r, g, b = self.current_color
        self.color_label.setText(f"RGB({r}, {g}, {b})")

    def _refresh_layout(self) -> None:
        for index, pixel in enumerate(self.pixels):
            rgb = [_clamp(ch) for ch in pixel[:3]]
            hex_color = _rgb_to_hex(rgb)
            self.layout_view.set_key_state(
                index,
                title=key_short_label(index),
                subtitle="",
                fill=hex_color,
                tooltip=(
                    f"{key_display_name(index)}\n"
                    f"RGB({rgb[0]}, {rgb[1]}, {rgb[2]})"
                ),
            )
        self._on_selected_key_changed(getattr(self.session, "selected_key", 0))
        self.layout_view.refresh()

    def _flatten_pixels(self) -> list[int]:
        flat: list[int] = []
        for pixel in self.pixels:
            flat.extend(_clamp(ch) for ch in pixel[:3])
        return flat

    def _refresh_live_effect_frame(self) -> None:
        try:
            effect_mode = self._sync_effect_mode_from_device()
            if effect_mode is None or self._is_matrix_edit_mode(effect_mode):
                self._last_live_sync_error = None
                return

            pixels = self.device.led_download_all()
            expected = KEY_COUNT * 3
            if not pixels or len(pixels) < expected:
                return

            for index in range(KEY_COUNT):
                base = index * 3
                self.pixels[index] = [pixels[base], pixels[base + 1], pixels[base + 2]]
            self._sync_shared()
            self._refresh_layout()
            self._last_live_sync_error = None
        except Exception as exc:
            message = str(exc)
            if message != self._last_live_sync_error:
                self._last_live_sync_error = message
                self._update_status(f"Live LED sync failed: {exc}", "warning")

    def _on_selected_key_changed(self, key_index: int) -> None:
        key_index = max(0, min(KEY_COUNT - 1, int(key_index)))
        rgb = self.pixels[key_index]
        self.selected_led_chip.set_text_and_level(key_display_name(key_index), "info")
        self.selected_led_rgb_chip.set_text_and_level(f"RGB {rgb[0]},{rgb[1]},{rgb[2]}", "neutral")

    def on_led_click(self, index: int) -> None:
        if self._deny_edit_if_read_only():
            return
        if index < 0 or index >= len(self.pixels):
            return

        rgb = self.current_color[:3]
        previous = self.pixels[index][:3]
        self.pixels[index] = rgb[:]
        self._refresh_layout()
        self._sync_shared()

        try:
            ok = self.device.led_set_pixel(index, rgb[0], rgb[1], rgb[2])
        except Exception as exc:
            self.pixels[index] = previous
            self._refresh_layout()
            self._update_status(f"Failed to update LED {index + 1}: {exc}", "error")
            return
        if not ok:
            self.pixels[index] = previous
            self._refresh_layout()
            self._update_status(f"Device rejected LED {index + 1}.", "error")
            return
        self._refresh_layout()
        self._update_status(
            f"{key_display_name(index)} set to RGB({rgb[0]}, {rgb[1]}, {rgb[2]}) live.",
            "success",
        )

    def on_brightness_change(self, value: int) -> None:
        brightness = _clamp(value)
        self.brightness = brightness
        self._sync_shared()
        self.brightness_value.setText(str(brightness))
        try:
            ok = self.device.led_set_brightness(brightness)
        except Exception as exc:
            self._update_status(f"Failed to set brightness: {exc}", "error")
            return
        if not ok:
            self._update_status("Device rejected brightness change.", "error")
            return
        self._update_status(f"Brightness set to {brightness} live.", "success")

    def pick_color(self) -> None:
        if self._deny_edit_if_read_only():
            return
        color = QColorDialog.getColor(QColor(*self.current_color[:3]), self, "Choose LED Color")
        if not color.isValid():
            return
        self._set_current_color([color.red(), color.green(), color.blue()])
        self._update_status(
            f"Paint color set to RGB({color.red()}, {color.green()}, {color.blue()}).",
            "success",
        )

    def set_color_hex(self, hex_color: str) -> None:
        if self._deny_edit_if_read_only():
            return
        qcolor = QColor(hex_color)
        if not qcolor.isValid():
            self._update_status(f"Invalid color value: {hex_color}", "error")
            return
        self._set_current_color([qcolor.red(), qcolor.green(), qcolor.blue()])
        self._update_status(f"Paint color set to {hex_color.upper()}.", "success")

    def clear_all(self) -> None:
        if self._deny_edit_if_read_only():
            return
        previous = [pixel[:] for pixel in self.pixels]
        for index in range(KEY_COUNT):
            self.pixels[index] = [0, 0, 0]
        self._refresh_layout()
        self._sync_shared()
        try:
            if self._is_matrix_edit_mode(self._current_effect_mode):
                ok = self.device.led_upload_all(self._flatten_pixels())
            else:
                ok = self.device.led_clear()
        except Exception as exc:
            self.pixels[:] = previous
            self._refresh_layout()
            self._update_status(f"Failed to clear LEDs: {exc}", "error")
            return
        if not ok:
            self.pixels[:] = previous
            self._refresh_layout()
            self._update_status("Device rejected clear request.", "error")
            return
        self._refresh_layout()
        self._update_status("All LEDs cleared live.", "success")

    def fill_color(self) -> None:
        if self._deny_edit_if_read_only():
            return
        rgb = self.current_color[:3]
        previous = [pixel[:] for pixel in self.pixels]
        for index in range(KEY_COUNT):
            self.pixels[index] = rgb[:]
        self._refresh_layout()
        self._sync_shared()
        try:
            if self._is_matrix_edit_mode(self._current_effect_mode):
                ok = self.device.led_upload_all(self._flatten_pixels())
            else:
                ok = self.device.led_fill(rgb[0], rgb[1], rgb[2])
        except Exception as exc:
            self.pixels[:] = previous
            self._refresh_layout()
            self._update_status(f"Failed to fill LEDs: {exc}", "error")
            return
        if not ok:
            self.pixels[:] = previous
            self._refresh_layout()
            self._update_status("Device rejected fill request.", "error")
            return
        self._refresh_layout()
        self._update_status(
            f"All LEDs filled with RGB({rgb[0]}, {rgb[1]}, {rgb[2]}) live.",
            "success",
        )

    def rainbow_test(self) -> None:
        if self._deny_edit_if_read_only():
            return
        try:
            ok = self.device.led_test_rainbow()
        except Exception as exc:
            self._update_status(f"Rainbow test failed: {exc}", "error")
            return
        if not ok:
            self._update_status("Device rejected rainbow test.", "error")
            return
        self._update_status("Rainbow test pattern shown.", "success")

    def reload(self) -> None:
        errors = []
        try:
            effect_mode = self.device.get_led_effect()
            if effect_mode is not None:
                self._current_effect_mode = int(effect_mode)
                self._set_layout_editable(
                    self._is_matrix_edit_mode(self._current_effect_mode),
                    self._current_effect_mode,
                )
        except Exception as exc:
            errors.append(f"effect mode: {exc}")

        try:
            brightness = self.device.led_get_brightness()
            if brightness is not None:
                self.brightness = int(brightness)
                self._sync_shared()
                with QSignalBlocker(self.brightness_slider):
                    self.brightness_slider.setValue(self.brightness)
                self.brightness_value.setText(str(self.brightness))
        except Exception as exc:
            errors.append(f"brightness: {exc}")

        try:
            pixels = self.device.led_download_all()
            expected = KEY_COUNT * 3
            if pixels and len(pixels) >= expected:
                for index in range(KEY_COUNT):
                    base = index * 3
                    self.pixels[index] = [pixels[base], pixels[base + 1], pixels[base + 2]]
                self._sync_shared()
                self._refresh_layout()
            elif pixels:
                errors.append(f"pixel payload too short ({len(pixels)} bytes)")
        except Exception as exc:
            errors.append(f"pixels: {exc}")

        if errors:
            self._update_status("Lighting state loaded with warnings: " + "; ".join(errors), "warning")
        else:
            self._update_status("Lighting state loaded from device.", "success")

        self._refresh_live_effect_frame()

    load_from_device = reload
    refresh_from_device = reload

    def save_to_flash(self) -> None:
        if self._deny_edit_if_read_only():
            return
        pixel_data = self._serialize_pixels()
        try:
            if not self.device.led_upload_all(pixel_data):
                self._update_status("Failed to upload LED data.", "error")
                QMessageBox.critical(self, "Save failed", "Failed to upload LED data to the device.")
                return
            if not self.device.save_settings():
                self._update_status("Failed to save lighting settings.", "error")
                QMessageBox.critical(self, "Save failed", "Failed to save settings to flash.")
                return
        except Exception as exc:
            self._update_status(f"Save failed: {exc}", "error")
            QMessageBox.critical(self, "Save failed", f"Could not save lighting settings:\n{exc}")
            return
        self._update_status("Lighting settings saved to flash.", "success")
        QMessageBox.information(self, "Saved", "Lighting settings were saved to flash.")
        self.reload()

    save_to_device = save_to_flash

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
            payload = bytes([_clamp(self.brightness)]) + bytes(self._serialize_pixels())
            pathlib.Path(filename).write_bytes(payload)
        except Exception as exc:
            self._update_status(f"Export failed: {exc}", "error")
            QMessageBox.critical(self, "Export failed", f"Could not export the LED pattern:\n{exc}")
            return
        self._update_status(f"Exported lighting pattern to {filename}.", "success")

    def import_from_file(self) -> None:
        if self._deny_edit_if_read_only():
            return
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

        for index in range(KEY_COUNT):
            base = index * 3
            self.pixels[index] = [payload[base], payload[base + 1], payload[base + 2]]

        self._sync_shared()
        with QSignalBlocker(self.brightness_slider):
            self.brightness_slider.setValue(brightness)
        self.brightness = brightness
        self.brightness_value.setText(str(brightness))
        self._refresh_layout()
        try:
            self.device.led_set_brightness(brightness)
            if not self.device.led_upload_all(payload):
                raise RuntimeError("device rejected the LED payload")
        except Exception as exc:
            self._update_status(f"Import failed: {exc}", "error")
            QMessageBox.critical(self, "Import failed", f"Could not import the LED pattern:\n{exc}")
            return
        self._update_status(f"Imported lighting pattern from {filename}.", "success")
        QMessageBox.information(self, "Imported", "Lighting pattern imported and sent live.")

    def on_page_activated(self) -> None:
        self._page_active = True
        self.reload()

    def on_live_tick(self) -> None:
        if not self._page_active or not self._global_live_enabled():
            return
        self._refresh_live_effect_frame()

    def on_page_deactivated(self) -> None:
        self._page_active = False

    def _serialize_pixels(self) -> list[int]:
        payload: list[int] = []
        for pixel in self.pixels[:KEY_COUNT]:
            payload.extend(_clamp(ch) for ch in pixel[:3])
        return payload[: KEY_COUNT * 3]
