from __future__ import annotations

import pathlib
import re
import time

from PySide6.QtCore import QThread, Signal
from PySide6.QtWidgets import (
    QDoubleSpinBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from kbhe_tool.firmware import perform_firmware_update, reconnect_device
from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
    make_primary_button,
    make_secondary_button,
)


# ---------------------------------------------------------------------------
# Flash worker
# ---------------------------------------------------------------------------

class FirmwareFlashWorker(QThread):
    log = Signal(str)
    completed = Signal()
    failed = Signal(str)

    def __init__(self, device, firmware_path, firmware_version, timeout_s, retries, parent=None):
        super().__init__(parent)
        self.device = device
        self.firmware_path = firmware_path
        self.firmware_version = firmware_version
        self.timeout_s = timeout_s
        self.retries = retries

    def run(self) -> None:
        try:
            perform_firmware_update(
                self.device,
                self.firmware_path,
                firmware_version=self.firmware_version,
                timeout_s=self.timeout_s,
                retries=self.retries,
                reconnect_after=False,
                logger=self.log.emit,
            )
            reconnect_device(self.device, timeout_s=self.timeout_s, logger=self.log.emit)
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.completed.emit()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _format_bytes(size: int) -> str:
    if size < 1024:
        return f"{size} B"
    if size < 1024 * 1024:
        return f"{size / 1024:.1f} KB"
    return f"{size / (1024 * 1024):.1f} MB"


def _format_timestamp(timestamp: float) -> str:
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(timestamp))


# ---------------------------------------------------------------------------
# FirmwarePage
# ---------------------------------------------------------------------------

class FirmwarePage(QWidget):
    def __init__(self, device, controller=None, parent=None):
        super().__init__(parent)
        self.device = device
        self.controller = controller
        self.worker = None
        self._busy = False
        self._build_ui()
        self.reload()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Firmware Update",
            "Flash the application slot with the custom updater. "
            "The bootloader stays resident and the page keeps the updater flow visible.",
        )
        root.addWidget(scaffold, 1)

        scaffold.add_card(self._build_selection_card())
        scaffold.add_card(self._build_options_card())
        scaffold.add_card(self._build_controls_card())
        scaffold.add_card(self._build_log_card())

        self.status_chip = StatusChip("Firmware page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

    def _build_selection_card(self) -> SectionCard:
        card = SectionCard(
            "Image Selection",
            "Select a .bin file to flash, or use the default build output shortcut.",
        )

        self.selection_summary = QLabel("No firmware image selected")
        self.selection_summary.setObjectName("CardTitle")
        self.selection_summary.setWordWrap(True)
        card.body_layout.addWidget(self.selection_summary)

        self.selection_details = QLabel(
            "Use Browse or the default build shortcut to pick a .bin file."
        )
        self.selection_details.setObjectName("Muted")
        self.selection_details.setWordWrap(True)
        card.body_layout.addWidget(self.selection_details)

        path_row = QHBoxLayout()
        path_row.setSpacing(8)
        self.firmware_path_edit = QLineEdit()
        self.firmware_path_edit.textChanged.connect(self._update_selection_summary)
        path_row.addWidget(self.firmware_path_edit, 1)
        self.browse_button = make_secondary_button("Browse…", self.browse_firmware_file)
        path_row.addWidget(self.browse_button)
        self.default_button = make_secondary_button("Use Default Build", self.use_default_firmware)
        path_row.addWidget(self.default_button)
        card.body_layout.addLayout(path_row)

        return card

    def _build_options_card(self) -> SectionCard:
        card = SectionCard(
            "Flash Options",
            "Leave firmware version blank to auto-read from Core/Src/settings.c.",
        )

        form_row = QHBoxLayout()
        form_row.setSpacing(10)

        fw_lbl = QLabel("FW Version:")
        fw_lbl.setObjectName("Muted")
        form_row.addWidget(fw_lbl)

        self.firmware_version_edit = QLineEdit()
        self.firmware_version_edit.setPlaceholderText("Auto-detect")
        form_row.addWidget(self.firmware_version_edit, 1)

        timeout_lbl = QLabel("Timeout (s):")
        timeout_lbl.setObjectName("Muted")
        form_row.addWidget(timeout_lbl)

        self.timeout_spin = QDoubleSpinBox()
        self.timeout_spin.setRange(1.0, 30.0)
        self.timeout_spin.setSingleStep(0.5)
        self.timeout_spin.setValue(5.0)
        form_row.addWidget(self.timeout_spin)

        retries_lbl = QLabel("Retries:")
        retries_lbl.setObjectName("Muted")
        form_row.addWidget(retries_lbl)

        self.retries_spin = QSpinBox()
        self.retries_spin.setRange(1, 20)
        self.retries_spin.setValue(5)
        form_row.addWidget(self.retries_spin)

        card.body_layout.addLayout(form_row)
        return card

    def _build_controls_card(self) -> SectionCard:
        card = SectionCard(
            "Updater Controls",
            "The flash button is disabled while an update is running.",
        )

        self.flash_button = make_primary_button("Flash Firmware", self.start_firmware_flash)
        card.body_layout.addWidget(self.flash_button)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(8)
        btn_row.addWidget(make_secondary_button("Clear Log", self.clear_firmware_log))
        btn_row.addStretch(1)
        card.body_layout.addLayout(btn_row)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setTextVisible(True)
        self.progress_bar.setFormat("Flashing: %p%")
        self.progress_bar.hide()
        card.body_layout.addWidget(self.progress_bar)

        return card

    def _build_log_card(self) -> SectionCard:
        card = SectionCard(
            "Updater Log",
            "Live output from the updater and reconnect flow appears here.",
        )
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(2000)
        self.log_view.setPlaceholderText("Firmware log output will appear here.")
        self.log_view.setMinimumHeight(200)
        card.body_layout.addWidget(self.log_view, 1)
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

    def _default_build_path(self) -> pathlib.Path:
        return pathlib.Path(__file__).resolve().parents[3] / "build" / "Release" / "kbhe.bin"

    def _update_selection_summary(self) -> None:
        path_text = self.firmware_path_edit.text().strip().strip('"')
        if not path_text:
            self.selection_summary.setText("No firmware image selected")
            self.selection_details.setText(
                "Use Browse or the default build shortcut to pick a .bin file."
            )
            return

        path = pathlib.Path(path_text)
        if not path.exists():
            self.selection_summary.setText("Selected firmware file is missing")
            self.selection_details.setText(str(path))
            return

        try:
            stat = path.stat()
        except Exception:
            self.selection_summary.setText(f"{path.name} selected")
            self.selection_details.setText(str(path))
            return

        self.selection_summary.setText(f"{path.name} ready to flash")
        self.selection_details.setText(
            f"{path} · {_format_bytes(stat.st_size)} · modified {_format_timestamp(stat.st_mtime)}"
        )

    def _set_busy(self, busy: bool) -> None:
        self._busy = busy
        for widget in (
            self.flash_button,
            self.browse_button,
            self.default_button,
            self.firmware_path_edit,
            self.firmware_version_edit,
            self.timeout_spin,
            self.retries_spin,
        ):
            widget.setEnabled(not busy)
        if busy:
            self.progress_bar.setRange(0, 100)
            self.progress_bar.setValue(0)
            self.progress_bar.setFormat("Starting…")
            self.progress_bar.show()
        else:
            self.progress_bar.hide()

    def _append_log(self, text: str) -> None:
        if not text:
            return
        self.log_view.appendPlainText(text)
        self.log_view.verticalScrollBar().setValue(
            self.log_view.verticalScrollBar().maximum()
        )
        m = re.search(r"Flashing:\s*\d+/(\d+)\s*bytes\s*\((\d+)%\)", text)
        if m:
            total = int(m.group(1))
            pct = int(m.group(2))
            self.progress_bar.setRange(0, 100)
            self.progress_bar.setValue(pct)
            self.progress_bar.setFormat(f"Flashing: {pct}%  ({total // 1024} KB total)")

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def reload(self) -> None:
        default_path = self._default_build_path()
        if not self.firmware_path_edit.text().strip() and default_path.exists():
            self.firmware_path_edit.setText(str(default_path))
        self.default_button.setEnabled(default_path.exists() and not self._busy)
        self._update_selection_summary()
        if self._busy:
            self._update_status("Firmware flash in progress.", "warning")
        else:
            self._update_status("Firmware page ready.", "info")

    load_from_device = reload
    refresh_from_device = reload

    def browse_firmware_file(self) -> None:
        default_parent = self._default_build_path().parent
        start_dir = str(default_parent) if default_parent.exists() else str(pathlib.Path.home())
        filename, _ = QFileDialog.getOpenFileName(
            self,
            "Select Firmware Image",
            start_dir,
            "Firmware binaries (*.bin);;All Files (*.*)",
        )
        if not filename:
            return
        self.firmware_path_edit.setText(filename)
        self._update_selection_summary()
        self._update_status(f"Selected firmware image: {filename}", "success")

    def use_default_firmware(self) -> None:
        default_path = self._default_build_path()
        if not default_path.exists():
            self._update_status("Default build output not found.", "error")
            QMessageBox.warning(
                self,
                "Missing firmware",
                "Default build output not found.\n\nBuild kbhe.bin first or browse to a different file.",
            )
            return
        self.firmware_path_edit.setText(str(default_path))
        self._update_selection_summary()
        self._update_status(f"Selected default build output: {default_path}", "success")

    def clear_firmware_log(self) -> None:
        self.log_view.clear()

    def start_firmware_flash(self) -> None:
        if self._busy:
            self._update_status("Firmware flash already in progress.", "warning")
            return

        firmware_path_text = self.firmware_path_edit.text().strip().strip('"')
        if not firmware_path_text:
            QMessageBox.warning(self, "Missing firmware", "Select a firmware .bin file first.")
            return

        firmware_path = pathlib.Path(firmware_path_text)
        if not firmware_path.exists():
            QMessageBox.warning(self, "Missing firmware", f"File does not exist:\n{firmware_path}")
            self._update_status(f"Missing firmware image: {firmware_path}", "error")
            return
        if firmware_path.stat().st_size <= 0:
            QMessageBox.warning(self, "Missing firmware", f"File is empty:\n{firmware_path}")
            self._update_status(f"Firmware image is empty: {firmware_path}", "error")
            return

        version_text = self.firmware_version_edit.text().strip()
        try:
            firmware_version = int(version_text, 0) if version_text else None
            timeout_s = float(self.timeout_spin.value())
            retries = int(self.retries_spin.value())
        except ValueError as exc:
            QMessageBox.warning(self, "Invalid firmware options", str(exc))
            return

        label = version_text if version_text else "auto-detect from Core/Src/settings.c"
        confirm = QMessageBox.question(
            self,
            "Confirm firmware flash",
            f"Flash the selected firmware image?\n\n"
            f"File: {firmware_path}\n"
            f"Version: {label}\n"
            f"Timeout: {timeout_s:.1f}s\n"
            f"Retries: {retries}\n\n"
            "Only the application slot will be updated.",
        )
        if confirm != QMessageBox.Yes:
            return

        self._set_busy(True)
        self._append_log("-" * 72)
        self._append_log(f"Starting flash for {firmware_path}")
        self._append_log(f"Version: {label}")
        self._append_log(f"Timeout: {timeout_s:.1f}s | Retries: {retries}")
        self._update_status("Flashing firmware...", "warning")

        self.worker = FirmwareFlashWorker(
            self.device,
            str(firmware_path),
            firmware_version,
            timeout_s,
            retries,
            parent=self,
        )
        self.worker.log.connect(self._append_log)
        self.worker.completed.connect(self._flash_completed)
        self.worker.failed.connect(self._flash_failed)
        self.worker.finished.connect(self.worker.deleteLater)
        self.worker.start()

    def _flash_completed(self) -> None:
        self._append_log("Firmware update completed successfully.")
        self._set_busy(False)
        self._update_status("Firmware update complete.", "success")
        if self.controller is not None and hasattr(self.controller, "refresh_from_device"):
            try:
                self.controller.refresh_from_device()
            except Exception:
                pass
        QMessageBox.information(self, "Firmware update", "Firmware update completed successfully.")
        self.reload()

    def _flash_failed(self, error_message: str) -> None:
        self._append_log(f"Firmware update failed: {error_message}")
        self._set_busy(False)
        self._update_status(f"Firmware update failed: {error_message}", "error")
        QMessageBox.critical(self, "Firmware update failed", error_message)

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
