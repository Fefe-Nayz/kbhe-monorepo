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

    def _build_live_card(self) -> SectionCard:
        card = SectionCard(
            "Global Live Updates",
            "One shared live toggle and interval for auto-refresh and all live preview pages.",
        )

        self.live_toggle = QCheckBox("Enable global live polling")
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
            "Applies to periodic status refresh and live previews (travel, graph, debug, gamepad, curve tracking)."
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
        finally:
            self._loading = False

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
