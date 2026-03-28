from __future__ import annotations

from PySide6.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QVBoxLayout,
    QWidget,
)

from ..widgets import (
    PageScaffold,
    SectionCard,
    SubCard,
    make_secondary_button,
)


class OverviewPage(QWidget):
    def __init__(self, session, controller=None, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self.controller = controller
        self._build_ui()
        self.on_selected_key_changed(getattr(self.session, "selected_key", 0))
        try:
            self.session.selectedKeyChanged.connect(self.on_selected_key_changed)
        except Exception:
            pass
        self.reload()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "Overview",
            "Verify device state, understand what persists automatically, "
            "then jump directly into keys, calibration, lighting, or firmware maintenance.",
        )
        root.addWidget(scaffold, 1)

        # ── Status tiles ──────────────────────────────────────────────
        tiles_grid = QGridLayout()
        tiles_grid.setHorizontalSpacing(14)
        tiles_grid.setVerticalSpacing(14)

        self.connection_tile = self._make_tile("Connection")
        self.firmware_tile = self._make_tile("Firmware")
        self.interfaces_tile = self._make_tile("Interfaces")
        self.lighting_tile = self._make_tile("Lighting")
        tiles_grid.addWidget(self.connection_tile["frame"], 0, 0)
        tiles_grid.addWidget(self.firmware_tile["frame"], 0, 1)
        tiles_grid.addWidget(self.interfaces_tile["frame"], 1, 0)
        tiles_grid.addWidget(self.lighting_tile["frame"], 1, 1)
        scaffold.content_layout.addLayout(tiles_grid)

        # ── Middle row: quick actions + focused key ───────────────────
        middle_row = QHBoxLayout()
        middle_row.setSpacing(14)
        scaffold.content_layout.addLayout(middle_row)

        middle_row.addWidget(self._build_actions_card(), 2)
        middle_row.addWidget(self._build_focus_card(), 1)

        # ── Bottom row: persistence + workflow ────────────────────────
        bottom_row = QHBoxLayout()
        bottom_row.setSpacing(14)
        scaffold.content_layout.addLayout(bottom_row)

        bottom_row.addWidget(self._build_persistence_card(), 1)
        bottom_row.addWidget(self._build_workflow_card(), 1)

        scaffold.add_stretch()

    def _make_tile(self, title: str) -> dict:
        sub = SubCard()

        lbl = QLabel(title)
        lbl.setObjectName("Muted")
        sub.layout.addWidget(lbl)

        value = QLabel("--")
        value.setObjectName("CardTitle")
        value.setStyleSheet("font-size: 14pt; font-weight: 700;")
        sub.layout.addWidget(value)

        detail = QLabel("")
        detail.setObjectName("Muted")
        detail.setWordWrap(True)
        sub.layout.addWidget(detail)

        return {"frame": sub, "value": value, "detail": detail}

    def _build_actions_card(self) -> SectionCard:
        card = SectionCard(
            "Jump Into a Task",
            "Structured like a configurator flow instead of a flat utility menu.",
        )
        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(10)
        card.body_layout.addLayout(grid)

        action_specs = [
            ("Tune Keys", "keyboard", "Actuation, release, remap, rapid trigger"),
            ("Calibrate", "calibration", "Zero points and analog curve editing"),
            ("Gamepad", "gamepad", "Deadzone, response curve, key mapping"),
            ("Lighting", "lighting", "Matrix painting and brightness"),
            ("Effects", "effects", "Live animation mode and timing"),
            ("Firmware", "firmware", "Flash app image and inspect updater logs"),
        ]
        for i, (title, page_id, subtitle) in enumerate(action_specs):
            btn = make_secondary_button(
                f"{title}\n{subtitle}",
                lambda _=False, pid=page_id: self._open_page(pid),
            )
            btn.setMinimumHeight(64)
            btn.setStyleSheet(btn.styleSheet() + " text-align: left; padding: 8px 14px;")
            grid.addWidget(btn, i // 2, i % 2)

        return card

    def _build_focus_card(self) -> SectionCard:
        card = SectionCard(
            "Focused Key",
            "The selected key follows you across Keyboard, Calibration, and Gamepad.",
        )

        self.focus_key_value = QLabel("Key 1")
        self.focus_key_value.setObjectName("CardTitle")
        self.focus_key_value.setStyleSheet("font-size: 20pt; font-weight: 700;")
        card.body_layout.addWidget(self.focus_key_value)

        hint = QLabel(
            "Pick a key in the top bar, then jump straight to the screen where you want to edit it."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        card.body_layout.addWidget(hint)

        for title, page_id in [
            ("Open Keyboard Tuning", "keyboard"),
            ("Open Calibration", "calibration"),
            ("Open Gamepad Mapping", "gamepad"),
        ]:
            card.body_layout.addWidget(
                make_secondary_button(title, lambda _=False, pid=page_id: self._open_page(pid))
            )

        return card

    def _build_persistence_card(self) -> SectionCard:
        card = SectionCard("Persistence Model")
        for text in [
            "Saved immediately: Keyboard HID, Gamepad HID, NKRO.",
            "Live until Save to Flash: LED matrix enable, brightness, and the current 8×8 LED pattern.",
            "Other tuning screens edit live RAM-backed settings; persist them with Save to Flash once the behavior feels correct.",
        ]:
            lbl = QLabel(text)
            lbl.setObjectName("Muted")
            lbl.setWordWrap(True)
            card.body_layout.addWidget(lbl)
        return card

    def _build_workflow_card(self) -> SectionCard:
        card = SectionCard("Recommended Workflow")
        for step in [
            "1. Tune the per-key behavior in Keyboard.",
            "2. Adjust baselines or curves in Calibration if travel feels off.",
            "3. Map analog/gamepad behavior only after the key behavior is stable.",
            "4. Finalize lighting last, then Save to Flash.",
        ]:
            lbl = QLabel(step)
            lbl.setObjectName("Muted")
            lbl.setWordWrap(True)
            card.body_layout.addWidget(lbl)
        return card

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _open_page(self, page_id: str) -> None:
        if self.controller is not None and hasattr(self.controller, "show_page"):
            self.controller.show_page(page_id)

    def _set_tile(self, tile: dict, value: str, detail: str) -> None:
        tile["value"].setText(value)
        tile["detail"].setText(detail)

    # ------------------------------------------------------------------
    # Data
    # ------------------------------------------------------------------

    def reload(self) -> None:
        snapshot = self.session.snapshot or {}
        options = snapshot.get("options") or {}
        connected = bool(getattr(self.session, "connected", True))
        firmware = snapshot.get("firmware_version") or "Unknown"
        brightness = snapshot.get("brightness")
        led_enabled = snapshot.get("led_enabled")
        nkro_enabled = snapshot.get("nkro_enabled")

        self._set_tile(
            self.connection_tile,
            "Connected" if connected else "Disconnected",
            "Raw HID session is active." if connected else "Reconnect the keyboard before editing settings.",
        )
        self._set_tile(
            self.firmware_tile,
            str(firmware),
            "Application firmware reported by the device.",
        )
        self._set_tile(
            self.interfaces_tile,
            f"KBD {'On' if options.get('keyboard_enabled') else 'Off'}  •  PAD {'On' if options.get('gamepad_enabled') else 'Off'}",
            f"NKRO {'On' if nkro_enabled else 'Off'}",
        )
        self._set_tile(
            self.lighting_tile,
            f"LED {'On' if led_enabled else 'Off'}",
            f"Brightness {brightness if brightness is not None else '--'}",
        )

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_selected_key_changed(self, key_index: int) -> None:
        self.focus_key_value.setText(f"Key {int(key_index) + 1}")

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
