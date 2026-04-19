from __future__ import annotations

from PySide6.QtCore import QSignalBlocker
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ..widgets import PageScaffold, SectionCard, StatusChip
from ..common import LED_EFFECT_NAMES


class AppSettingsPage(QWidget):
    def __init__(self, session, controller=None, parent=None):
        super().__init__(parent)
        self.session = session
        self.controller = controller
        self._loading = False
        self._build_ui()
        self._connect_signals()
        self.reload()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "App Settings",
            "Configure global UI behavior and shared live polling controls used across pages.",
        )
        root.addWidget(scaffold, 1)

        scaffold.add_card(self._build_theme_card())
        scaffold.add_card(self._build_live_card())
        scaffold.add_card(self._build_shutdown_effect_card())

        self.status_chip = StatusChip("App settings ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _build_theme_card(self) -> SectionCard:
        card = SectionCard("Appearance", "Choose the global theme for the configurator.")

        row = QHBoxLayout()
        row.setSpacing(8)
        lbl = QLabel("Theme")
        lbl.setObjectName("Muted")
        row.addWidget(lbl)

        self.theme_combo = QComboBox()
        self.theme_combo.addItem("Auto", "system")
        self.theme_combo.addItem("Light", "light")
        self.theme_combo.addItem("Dark", "dark")
        self.theme_combo.currentIndexChanged.connect(self._on_theme_changed)
        row.addWidget(self.theme_combo, 1)

        card.body_layout.addLayout(row)
        return card

    def _build_shutdown_effect_card(self) -> SectionCard:
        card = SectionCard(
            "Shutdown LED Behavior",
            "Optionally force a specific LED effect when the app closes, then restore the previous one on next launch.",
        )

        self.close_effect_toggle = QCheckBox("Apply selected LED effect on app close")
        self.close_effect_toggle.toggled.connect(self._on_close_effect_toggled)
        card.body_layout.addWidget(self.close_effect_toggle)

        row = QHBoxLayout()
        row.setSpacing(8)
        lbl = QLabel("Effect on close")
        lbl.setObjectName("Muted")
        row.addWidget(lbl)

        self.close_effect_combo = QComboBox()
        for effect_id, effect_name in sorted(
            ((int(k), str(v)) for k, v in LED_EFFECT_NAMES.items()),
            key=lambda item: item[0],
        ):
            self.close_effect_combo.addItem(f"{effect_id:02d} - {effect_name}", effect_id)
        self.close_effect_combo.currentIndexChanged.connect(self._on_close_effect_mode_changed)
        row.addWidget(self.close_effect_combo, 1)

        card.body_layout.addLayout(row)

        self.restore_previous_effect_toggle = QCheckBox(
            "Restore previous LED effect automatically on next startup"
        )
        self.restore_previous_effect_toggle.toggled.connect(self._on_restore_previous_toggled)
        card.body_layout.addWidget(self.restore_previous_effect_toggle)

        note = QLabel(
            "When restore is enabled, the app stores the currently active effect before applying the shutdown effect."
        )
        note.setObjectName("Muted")
        note.setWordWrap(True)
        card.body_layout.addWidget(note)

        return card

    def _build_live_card(self) -> SectionCard:
        card = SectionCard(
            "Global Live Updates",
            "One shared live toggle and interval for auto-refresh, live previews, and firmware performance diagnostics.",
        )

        self.live_toggle = QCheckBox("Enable global live polling and perf diagnostics")
        self.live_toggle.toggled.connect(self._on_live_toggled)
        card.body_layout.addWidget(self.live_toggle)

        row = QHBoxLayout()
        row.setSpacing(8)
        lbl = QLabel("Refresh interval (ms)")
        lbl.setObjectName("Muted")
        row.addWidget(lbl)

        self.interval_spin = QSpinBox()
        self.interval_spin.setRange(20, 2000)
        self.interval_spin.setSingleStep(10)
        self.interval_spin.valueChanged.connect(self._on_interval_changed)
        row.addWidget(self.interval_spin)
        row.addStretch(1)

        card.body_layout.addLayout(row)

        note = QLabel(
            "Applies to periodic status refresh and live previews. It also re-enables the firmware perf diagnostics session while pages are polling."
        )
        note.setObjectName("Muted")
        note.setWordWrap(True)
        card.body_layout.addWidget(note)

        return card

    def _connect_signals(self) -> None:
        try:
            self.session.liveSettingsChanged.connect(self._on_session_live_settings_changed)
        except Exception:
            pass

    def _set_status(self, message: str, level: str = "info") -> None:
        level_map = {"success": "ok", "warning": "warn", "error": "bad", "info": "info"}
        self.status_chip.set_text_and_level(message, level_map.get(level, "neutral"))
        try:
            self.session.set_status(message, level)
        except Exception:
            pass

    def _on_theme_changed(self, _index: int) -> None:
        if self._loading:
            return
        mode = self.theme_combo.currentData()
        if self.controller is not None and hasattr(self.controller, "_set_theme"):
            try:
                self.controller._set_theme(mode)
                self._set_status(f"Theme set to {self.theme_combo.currentText()}.", "success")
            except Exception as exc:
                self._set_status(f"Failed to apply theme: {exc}", "error")

    def _on_live_toggled(self, checked: bool) -> None:
        if self._loading:
            return
        self.session.set_live_enabled(bool(checked))
        self._set_status(
            "Global live polling enabled." if checked else "Global live polling disabled.",
            "success",
        )

    def _on_interval_changed(self, value: int) -> None:
        if self._loading:
            return
        self.session.set_live_interval_ms(int(value))
        self._set_status(f"Global refresh interval set to {int(value)} ms.", "success")

    def _on_close_effect_toggled(self, checked: bool) -> None:
        if self._loading:
            return
        if self.controller is None or not hasattr(self.controller, "set_close_effect_enabled"):
            return
        try:
            self.controller.set_close_effect_enabled(bool(checked))
            self._sync_close_effect_controls()
            self._set_status(
                "Shutdown LED override enabled." if checked else "Shutdown LED override disabled.",
                "success",
            )
        except Exception as exc:
            self._set_status(f"Failed to update shutdown LED override: {exc}", "error")

    def _on_close_effect_mode_changed(self, _index: int) -> None:
        if self._loading:
            return
        if self.controller is None or not hasattr(self.controller, "set_close_effect_mode"):
            return
        mode = self.close_effect_combo.currentData()
        if mode is None:
            return
        try:
            self.controller.set_close_effect_mode(int(mode))
            self._set_status(
                f"Shutdown LED effect set to {self.close_effect_combo.currentText()}.",
                "success",
            )
        except Exception as exc:
            self._set_status(f"Failed to update shutdown effect: {exc}", "error")

    def _on_restore_previous_toggled(self, checked: bool) -> None:
        if self._loading:
            return
        if self.controller is None or not hasattr(self.controller, "set_restore_previous_effect_on_startup"):
            return
        try:
            self.controller.set_restore_previous_effect_on_startup(bool(checked))
            self._set_status(
                "Startup restore of previous effect enabled."
                if checked
                else "Startup restore of previous effect disabled.",
                "success",
            )
        except Exception as exc:
            self._set_status(f"Failed to update startup restore option: {exc}", "error")

    def _sync_close_effect_controls(self) -> None:
        enabled = bool(self.close_effect_toggle.isChecked())
        self.close_effect_combo.setEnabled(enabled)
        self.restore_previous_effect_toggle.setEnabled(enabled)

    def _on_session_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        self._loading = True
        try:
            with QSignalBlocker(self.live_toggle):
                self.live_toggle.setChecked(bool(enabled))
            with QSignalBlocker(self.interval_spin):
                self.interval_spin.setValue(int(interval_ms))
        finally:
            self._loading = False

    def reload(self) -> None:
        self._loading = True
        try:
            if self.controller is not None and hasattr(self.controller, "theme_mode"):
                mode = getattr(self.controller, "theme_mode")
                idx = max(0, self.theme_combo.findData(mode))
                with QSignalBlocker(self.theme_combo):
                    self.theme_combo.setCurrentIndex(idx)

            with QSignalBlocker(self.live_toggle):
                self.live_toggle.setChecked(bool(getattr(self.session, "live_enabled", True)))
            with QSignalBlocker(self.interval_spin):
                self.interval_spin.setValue(int(getattr(self.session, "live_interval_ms", 100)))

            close_enabled = False
            close_mode = self.close_effect_combo.currentData()
            restore_previous = True
            if self.controller is not None:
                if hasattr(self.controller, "get_close_effect_enabled"):
                    close_enabled = bool(self.controller.get_close_effect_enabled())
                if hasattr(self.controller, "get_close_effect_mode"):
                    close_mode = int(self.controller.get_close_effect_mode())
                if hasattr(self.controller, "get_restore_previous_effect_on_startup"):
                    restore_previous = bool(self.controller.get_restore_previous_effect_on_startup())

            with QSignalBlocker(self.close_effect_toggle):
                self.close_effect_toggle.setChecked(close_enabled)

            combo_index = self.close_effect_combo.findData(int(close_mode))
            if combo_index < 0:
                combo_index = 0
            with QSignalBlocker(self.close_effect_combo):
                self.close_effect_combo.setCurrentIndex(combo_index)

            with QSignalBlocker(self.restore_previous_effect_toggle):
                self.restore_previous_effect_toggle.setChecked(restore_previous)

            self._sync_close_effect_controls()
        finally:
            self._loading = False

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
