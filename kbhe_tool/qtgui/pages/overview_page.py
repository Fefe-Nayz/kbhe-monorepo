from __future__ import annotations

from PySide6.QtWidgets import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QVBoxLayout,
    QWidget,
)

from kbhe_tool.key_layout import key_display_name
from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
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
        try:
            self.session.snapshotChanged.connect(self.reload)
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
            "Current device status at a glance. Jump into any configuration screen from here.",
        )
        root.addWidget(scaffold, 1)

        # ── Status tiles (2×2) ────────────────────────────────────────
        tiles_grid = QGridLayout()
        tiles_grid.setHorizontalSpacing(14)
        tiles_grid.setVerticalSpacing(14)

        self.connection_tile = self._make_tile("Connection", "●")
        self.firmware_tile = self._make_tile("Firmware", "⬡")
        self.interfaces_tile = self._make_tile("Interfaces", "⌨")
        self.lighting_tile = self._make_tile("Lighting", "◈")

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

    def _make_tile(self, title: str, icon: str = "") -> dict:
        """A prominent status tile styled like a SectionCard."""
        frame = QFrame()
        frame.setObjectName("SectionCard")

        layout = QVBoxLayout(frame)
        layout.setContentsMargins(20, 18, 20, 18)
        layout.setSpacing(8)

        # Header: label + status chip
        header = QHBoxLayout()
        header.setSpacing(8)
        lbl = QLabel(f"{icon}  {title.upper()}" if icon else title.upper())
        lbl.setObjectName("SidebarGroupLabel")
        header.addWidget(lbl, 1)
        chip = StatusChip("—", "neutral")
        header.addWidget(chip)
        layout.addLayout(header)

        # Large value
        value = QLabel("—")
        value.setObjectName("CardTitle")
        value.setStyleSheet("font-size: 17pt; font-weight: 700; margin-top: 2px;")
        layout.addWidget(value)

        # Detail line
        detail = QLabel("")
        detail.setObjectName("Muted")
        detail.setWordWrap(True)
        layout.addWidget(detail)

        return {"frame": frame, "value": value, "detail": detail, "chip": chip}

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
        ("Gamepad", "gamepad", "Routing, response curve, stick preview, key mapping"),
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

        self.focus_key_value = QLabel(key_display_name(0))
        self.focus_key_value.setObjectName("CardTitle")
        self.focus_key_value.setStyleSheet("font-size: 28pt; font-weight: 700; padding: 8px 0;")
        card.body_layout.addWidget(self.focus_key_value)

        hint = QLabel(
            "Pick a key in the top bar, then jump straight to the screen where you want to edit it."
        )
        hint.setObjectName("Muted")
        hint.setWordWrap(True)
        card.body_layout.addWidget(hint)

        card.body_layout.addSpacing(4)

        for title, page_id in [
            ("Open Keyboard Tuning", "keyboard"),
            ("Open Calibration", "calibration"),
            ("Open Gamepad Mapping", "gamepad"),
        ]:
            card.body_layout.addWidget(
                make_secondary_button(title, lambda _=False, pid=page_id: self._open_page(pid))
            )

        card.body_layout.addStretch(1)
        return card

    def _build_persistence_card(self) -> SectionCard:
        card = SectionCard("Persistence Model")
        for text in [
            "Saved immediately: Keyboard HID, Gamepad HID, NKRO.",
            "Live until Save to Flash: LED enable, brightness, and the current 82-LED frame.",
            "Other tuning screens edit live RAM-backed settings; persist them with Save to Flash once the behavior feels correct.",
        ]:
            lbl = QLabel(text)
            lbl.setObjectName("Muted")
            lbl.setWordWrap(True)
            card.body_layout.addWidget(lbl)
        card.body_layout.addStretch(1)
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
        card.body_layout.addStretch(1)
        return card

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _open_page(self, page_id: str) -> None:
        if self.controller is not None and hasattr(self.controller, "show_page"):
            self.controller.show_page(page_id)

    def _set_tile(self, tile: dict, value: str, detail: str, level: str = "neutral") -> None:
        tile["value"].setText(value)
        tile["detail"].setText(detail)
        tile["chip"].set_text_and_level(value, level)

    # ------------------------------------------------------------------
    # Data
    # ------------------------------------------------------------------

    def reload(self, *_args) -> None:
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
            "ok" if connected else "bad",
        )
        self._set_tile(
            self.firmware_tile,
            str(firmware),
            "Application firmware reported by the device.",
            "info" if firmware != "Unknown" else "neutral",
        )
        self._set_tile(
            self.interfaces_tile,
            f"KBD {'On' if options.get('keyboard_enabled') else 'Off'}  •  PAD {'On' if options.get('gamepad_enabled') else 'Off'}",
            f"NKRO {'On' if nkro_enabled else 'Off'}",
            "ok" if options.get("keyboard_enabled") else "neutral",
        )
        self._set_tile(
            self.lighting_tile,
            f"LED {'On' if led_enabled else 'Off'}",
            f"Brightness {brightness if brightness is not None else '--'}",
            "ok" if led_enabled else "neutral",
        )

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_selected_key_changed(self, key_index: int) -> None:
        self.focus_key_value.setText(key_display_name(int(key_index)))

    def on_page_activated(self) -> None:
        try:
            self.session.refresh_snapshot()
        except Exception:
            pass
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
