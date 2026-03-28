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
    QPushButton,
    QSlider,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from ..widgets import (
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
    ("#a259ff", "Purple"),
    ("#64d2ff", "Sky"),
    ("#5e5ce6", "Indigo"),
]


def _clamp(value: int) -> int:
    return max(0, min(255, int(value)))


def _rgb_to_hex(rgb) -> str:
    r, g, b = (_clamp(ch) for ch in rgb[:3])
    return f"#{r:02x}{g:02x}{b:02x}"


class LightingPage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self._loading = False
        self._build_state()
        self._build_ui()
        self.reload()

    # ------------------------------------------------------------------
    # State init
    # ------------------------------------------------------------------

    def _build_state(self) -> None:
        owner = self.controller
        if owner is not None and hasattr(owner, "pixels"):
            self.pixels = getattr(owner, "pixels")
        else:
            self.pixels = [[0, 0, 0] for _ in range(64)]
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

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Lighting",
            "Paint the matrix live, preview colors, and persist the current "
            "lighting state when you are ready.",
        )
        root.addWidget(scaffold, 1)

        # Two-column layout: matrix on left, controls on right
        columns = QHBoxLayout()
        columns.setSpacing(14)
        scaffold.content_layout.addLayout(columns)

        columns.addWidget(self._build_matrix_card(), 2)
        columns.addLayout(self._build_right_column(), 1)

        # Status chip
        self.status_chip = StatusChip("Lighting page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

        self._refresh_matrix()

    def _build_matrix_card(self) -> SectionCard:
        card = SectionCard(
            "LED Matrix (8×8)",
            "Click a cell to paint it with the selected color.",
        )

        _cell, _gap, _n = 30, 4, 8
        _grid_px = _n * _cell + (_n - 1) * _gap  # 268 px

        grid_host = QWidget()
        grid_host.setFixedSize(_grid_px, _grid_px)
        self.matrix_grid = QGridLayout(grid_host)
        self.matrix_grid.setContentsMargins(0, 0, 0, 0)
        self.matrix_grid.setHorizontalSpacing(_gap)
        self.matrix_grid.setVerticalSpacing(_gap)

        self.matrix_buttons: list[list[QPushButton]] = []
        for y in range(8):
            row_btns = []
            for x in range(8):
                index = y * 8 + x
                btn = QPushButton()
                btn.setObjectName("MatrixButton")
                btn.setCursor(Qt.PointingHandCursor)
                btn.setToolTip(f"LED {index + 1}")
                btn.clicked.connect(lambda _=False, i=index: self.on_led_click(i))
                self.matrix_grid.addWidget(btn, y, x)
                row_btns.append(btn)
            self.matrix_buttons.append(row_btns)

        card.body_layout.addWidget(grid_host)
        card.body_layout.addStretch(1)
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

        card.body_layout.addWidget(
            make_secondary_button("Choose Color…", self.pick_color)
        )

        # Quick-color swatches
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
            swatch.setFixedHeight(34)
            light_text = hex_color in ("#ffffff", "#ffd60a", "#64d2ff")
            swatch.setStyleSheet(
                f"QToolButton {{"
                f"  background: {hex_color};"
                f"  color: {'#000000' if light_text else '#ffffff'};"
                f"  border: 1px solid rgba(0,0,0,0.15);"
                f"  border-radius: 8px;"
                f"  font-weight: 600;"
                f"}}"
                f"QToolButton:hover {{ border-color: palette(highlight); }}"
            )
            swatch.clicked.connect(lambda _=False, h=hex_color: self.set_color_hex(h))
            swatch_grid.addWidget(swatch, i // 4, i % 4)

        card.body_layout.addWidget(swatch_host)
        return card

    def _build_brightness_card(self) -> SectionCard:
        card = SectionCard(
            "Brightness",
            "Brightness is applied immediately to the keyboard.",
        )

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
        card = SectionCard(
            "Live Actions",
            "These actions update the keyboard immediately.",
        )
        card.body_layout.addWidget(make_primary_button("Fill with Current Color", self.fill_color))
        card.body_layout.addWidget(make_danger_button("Clear Matrix", self.clear_all))
        card.body_layout.addWidget(make_secondary_button("Run Rainbow Test", self.rainbow_test))
        card.body_layout.addWidget(make_secondary_button("Reload from Device", self.reload))
        return card

    def _build_persistence_card(self) -> SectionCard:
        card = SectionCard(
            "Persistence",
            "Save to flash when you want the current live state to survive power cycles.",
        )
        card.body_layout.addWidget(make_primary_button("Save to Flash", self.save_to_flash))

        file_row = QHBoxLayout()
        file_row.setSpacing(8)
        file_row.addWidget(make_secondary_button("Export", self.export_to_file))
        file_row.addWidget(make_secondary_button("Import", self.import_from_file))
        file_row.addStretch(1)
        card.body_layout.addLayout(file_row)

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
        for name, value in (
            ("pixels", self.pixels),
            ("current_color", self.current_color),
            ("brightness", self.brightness),
        ):
            try:
                setattr(self.controller, name, value)
            except Exception:
                pass

    def _refresh_matrix(self) -> None:
        for index, pixel in enumerate(self.pixels[:64]):
            y, x = divmod(index, 8)
            bg = _rgb_to_hex(pixel)
            self.matrix_buttons[y][x].setStyleSheet(
                f"QPushButton#MatrixButton {{"
                f"  background: {bg};"
                f"  border-radius: 8px;"
                f"  border: 1px solid rgba(0,0,0,0.12);"
                f"  min-width: 30px; min-height: 30px;"
                f"  max-width: 30px; max-height: 30px;"
                f"  padding: 0px;"
                f"}}"
                f"QPushButton#MatrixButton:hover {{ border-color: palette(highlight); }}"
            )

    def _set_current_color(self, rgb: list[int]) -> None:
        self.current_color = [_clamp(ch) for ch in rgb[:3]]
        self._sync_shared()
        hex_color = _rgb_to_hex(self.current_color)
        self.color_preview.setStyleSheet(
            f"border-radius: 12px; border: 1px solid palette(mid); background: {hex_color};"
        )
        r, g, b = self.current_color
        self.color_label.setText(f"RGB({r}, {g}, {b})")

    # ------------------------------------------------------------------
    # Event handlers
    # ------------------------------------------------------------------

    def on_led_click(self, index: int) -> None:
        if index < 0 or index >= len(self.pixels):
            return
        rgb = self.current_color[:3]
        previous = self.pixels[index][:3]
        self.pixels[index] = rgb[:]
        self._refresh_matrix()
        self._sync_shared()
        try:
            ok = self.device.led_set_pixel(index, rgb[0], rgb[1], rgb[2])
        except Exception as exc:
            self.pixels[index] = previous
            self._refresh_matrix()
            self._update_status(f"Failed to update LED {index + 1}: {exc}", "error")
            return
        if not ok:
            self.pixels[index] = previous
            self._refresh_matrix()
            self._update_status(f"Device rejected LED {index + 1}.", "error")
            return
        self._update_status(
            f"LED {index + 1} set to RGB({rgb[0]}, {rgb[1]}, {rgb[2]}) live.", "success"
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
        color = QColorDialog.getColor(QColor(*self.current_color[:3]), self, "Choose LED Color")
        if not color.isValid():
            return
        self._set_current_color([color.red(), color.green(), color.blue()])
        self._update_status(
            f"Paint color set to RGB({color.red()}, {color.green()}, {color.blue()}).", "success"
        )

    def set_color_hex(self, hex_color: str) -> None:
        qcolor = QColor(hex_color)
        if not qcolor.isValid():
            self._update_status(f"Invalid color value: {hex_color}", "error")
            return
        self._set_current_color([qcolor.red(), qcolor.green(), qcolor.blue()])
        self._update_status(f"Paint color set to {hex_color.upper()}.", "success")

    def clear_all(self) -> None:
        previous = [p[:] for p in self.pixels]
        for i in range(64):
            self.pixels[i] = [0, 0, 0]
        self._refresh_matrix()
        self._sync_shared()
        try:
            ok = self.device.led_clear()
        except Exception as exc:
            self.pixels[:] = previous
            self._refresh_matrix()
            self._update_status(f"Failed to clear matrix: {exc}", "error")
            return
        if not ok:
            self.pixels[:] = previous
            self._refresh_matrix()
            self._update_status("Device rejected clear request.", "error")
            return
        self._update_status("Matrix cleared live.", "success")

    def fill_color(self) -> None:
        rgb = self.current_color[:3]
        previous = [p[:] for p in self.pixels]
        for i in range(64):
            self.pixels[i] = rgb[:]
        self._refresh_matrix()
        self._sync_shared()
        try:
            ok = self.device.led_fill(rgb[0], rgb[1], rgb[2])
        except Exception as exc:
            self.pixels[:] = previous
            self._refresh_matrix()
            self._update_status(f"Failed to fill matrix: {exc}", "error")
            return
        if not ok:
            self.pixels[:] = previous
            self._refresh_matrix()
            self._update_status("Device rejected fill request.", "error")
            return
        self._update_status(
            f"Matrix filled with RGB({rgb[0]}, {rgb[1]}, {rgb[2]}) live.", "success"
        )

    def rainbow_test(self) -> None:
        try:
            ok = self.device.led_test_rainbow()
        except Exception as exc:
            self._update_status(f"Rainbow test failed: {exc}", "error")
            return
        if not ok:
            self._update_status("Device rejected rainbow test.", "error")
            return
        self._update_status("Rainbow test pattern shown.", "success")

    # ------------------------------------------------------------------
    # Data loading / persistence
    # ------------------------------------------------------------------

    def reload(self) -> None:
        errors = []
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
            if pixels and len(pixels) >= 192:
                for i in range(64):
                    base = i * 3
                    self.pixels[i] = [pixels[base], pixels[base + 1], pixels[base + 2]]
                self._sync_shared()
                self._refresh_matrix()
            elif pixels:
                errors.append(f"pixel payload too short ({len(pixels)} bytes)")
        except Exception as exc:
            errors.append(f"pixels: {exc}")

        if errors:
            self._update_status(
                "Lighting state loaded with warnings: " + "; ".join(errors), "warning"
            )
        else:
            self._update_status("Lighting state loaded from device.", "success")

    load_from_device = reload
    refresh_from_device = reload

    def save_to_flash(self) -> None:
        pixel_data = []
        for pixel in self.pixels[:64]:
            pixel_data.extend(int(ch) for ch in pixel[:3])
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
        if len(data) < 193:
            self._update_status("Import failed: invalid file format.", "error")
            QMessageBox.warning(
                self, "Import failed", "Invalid LED pattern file.\nExpected 193 bytes."
            )
            return

        brightness = _clamp(data[0])
        for i in range(64):
            base = 1 + i * 3
            self.pixels[i] = [data[base], data[base + 1], data[base + 2]]
        self._sync_shared()
        with QSignalBlocker(self.brightness_slider):
            self.brightness_slider.setValue(brightness)
        self.brightness = brightness
        self.brightness_value.setText(str(brightness))
        self._refresh_matrix()
        try:
            self.device.led_set_brightness(brightness)
            if not self.device.led_upload_all(list(data[1:193])):
                raise RuntimeError("device rejected the LED payload")
        except Exception as exc:
            self._update_status(f"Import failed: {exc}", "error")
            QMessageBox.critical(self, "Import failed", f"Could not import the LED pattern:\n{exc}")
            return
        self._update_status(f"Imported lighting pattern from {filename}.", "success")
        QMessageBox.information(self, "Imported", "Lighting pattern imported and sent live.")

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass

    def _serialize_pixels(self) -> list[int]:
        payload = []
        for pixel in self.pixels[:64]:
            channels = list(pixel[:3]) + [0, 0, 0]
            payload.extend(_clamp(ch) for ch in channels[:3])
        return payload[:192]
