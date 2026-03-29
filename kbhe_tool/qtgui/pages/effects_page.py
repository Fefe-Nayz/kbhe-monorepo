from __future__ import annotations

from PySide6.QtCore import QSignalBlocker, Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QButtonGroup,
    QColorDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QRadioButton,
    QScrollArea,
    QSlider,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from ..widgets import (
    PageScaffold,
    SectionCard,
    SubCard,
    StatusChip,
    make_secondary_button,
)

EFFECT_GROUPS = [
    (
        "Software Control",
        [
            (0, "Matrix (Software)"),
            (4, "Solid Color"),
            (14, "Third-Party Live"),
        ],
    ),
    (
        "Ambient Motion",
        [
            (1, "Rainbow Wave"),
            (2, "Breathing"),
            (3, "Static Rainbow"),
            (5, "Plasma"),
            (6, "Fire"),
            (7, "Ocean Waves"),
            (8, "Matrix Rain"),
            (9, "Sparkle"),
            (10, "Breathing Rainbow"),
            (11, "Spiral"),
            (12, "Color Cycle"),
        ],
    ),
    (
        "Reactive",
        [
            (13, "Reactive (Key Press)"),
            (15, "Sensor Distance"),
        ],
    ),
]

EFFECT_METADATA: dict[int, tuple[str, str]] = {
    0: ("Matrix (Software)", "No animation; uses the editable matrix pattern from the Lighting tab."),
    1: ("Rainbow Wave", "Animated rainbow sweep across the matrix."),
    2: ("Breathing", "Smooth in/out pulsing with the selected color."),
    3: ("Static Rainbow", "Rainbow colors stay visible without motion."),
    4: ("Solid Color", "A single steady color fill."),
    5: ("Plasma", "Fluid plasma-style motion effect."),
    6: ("Fire", "Warm animated fire simulation."),
    7: ("Ocean Waves", "Cool wave-like motion with layered color changes."),
    8: ("Matrix Rain", "Green matrix-style falling animation."),
    9: ("Sparkle", "Random sparkles across the matrix."),
    10: ("Breathing Rainbow", "Breathing animation that cycles through rainbow colors."),
    11: ("Spiral", "Spinning spiral motion pattern."),
    12: ("Color Cycle", "Continuous cycling through the selected effect color palette."),
    13: ("Reactive (Key Press)", "Responds to key presses using the selected effect color."),
    14: ("Third-Party Live", "Matrix is read-only in this app; live frame is displayed from device state."),
    15: ("Sensor Distance", "Each key LED color changes live according to its sensor travel distance."),
}

_QUICK_COLORS = [
    ("#ff3b30", "Red"),
    ("#34c759", "Green"),
    ("#0a84ff", "Blue"),
    ("#ffffff", "White"),
    ("#ffd60a", "Yellow"),
    ("#ff2d55", "Magenta"),
    ("#32ade6", "Cyan"),
    ("#ff9500", "Orange"),
]

DEFAULT_EFFECT_COLOR = [0, 255, 0]


def _clamp(value: int) -> int:
    return max(0, min(255, int(value)))


def _rgb_to_hex(rgb) -> str:
    r, g, b = (_clamp(ch) for ch in rgb[:3])
    return f"#{r:02x}{g:02x}{b:02x}"


class EffectsPage(QWidget):
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
        if owner is not None and hasattr(owner, "effect_color"):
            self.effect_color = list(getattr(owner, "effect_color"))
        else:
            self.effect_color = DEFAULT_EFFECT_COLOR[:]
            if owner is not None:
                try:
                    setattr(owner, "effect_color", self.effect_color)
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
            "Effects",
            "Tune the lighting engine live: select a mode, shape the tempo, "
            "and set the color used by the supported effects.",
        )
        root.addWidget(scaffold, 1)

        # Active mode summary (shown below page subtitle)
        self.effect_summary = QLabel()
        self.effect_summary.setObjectName("Muted")
        self.effect_summary.setWordWrap(True)
        scaffold.content_layout.addWidget(self.effect_summary)

        # Two-column: mode list on left, controls on right
        columns = QHBoxLayout()
        columns.setSpacing(14)
        scaffold.content_layout.addLayout(columns)

        columns.addWidget(self._build_mode_card(), 2)
        columns.addLayout(self._build_right_column(), 1)

        # Status
        self.status_chip = StatusChip("Effects page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)

        scaffold.add_stretch()

    def _build_mode_card(self) -> SectionCard:
        card = SectionCard(
            "Effect Mode",
            "Choose a mode. The device applies the change immediately.",
        )

        # Scrollable mode list
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

        scroll_content = QWidget()
        scroll_layout = QVBoxLayout(scroll_content)
        scroll_layout.setContentsMargins(0, 0, 0, 0)
        scroll_layout.setSpacing(10)
        scroll.setWidget(scroll_content)

        self.effect_mode_buttons: dict[int, QRadioButton] = {}
        self._mode_group = QButtonGroup(self)
        self._mode_group.setExclusive(True)
        for group_name, modes in EFFECT_GROUPS:
            sub = SubCard()
            grp_lbl = QLabel(group_name)
            grp_lbl.setObjectName("CardTitle")
            sub.layout.addWidget(grp_lbl)
            for value, label in modes:
                btn = QRadioButton(label)
                btn.toggled.connect(
                    lambda checked, m=value: self._on_mode_toggled(checked, m)
                )
                sub.layout.addWidget(btn)
                self.effect_mode_buttons[value] = btn
                self._mode_group.addButton(btn)
            scroll_layout.addWidget(sub)

        scroll_layout.addStretch(1)
        card.body_layout.addWidget(scroll, 1)
        return card

    def _build_right_column(self) -> QVBoxLayout:
        col = QVBoxLayout()
        col.setSpacing(14)
        col.addWidget(self._build_speed_card())
        col.addWidget(self._build_fps_card())
        col.addWidget(self._build_color_card())
        col.addStretch(1)
        return col

    def _build_speed_card(self) -> SectionCard:
        card = SectionCard(
            "Effect Speed",
            "Controls how quickly animated effects advance.",
        )
        self.effect_speed_slider = QSlider(Qt.Horizontal)
        self.effect_speed_slider.setRange(1, 255)
        self.effect_speed_slider.valueChanged.connect(self.on_effect_speed_change)
        card.body_layout.addWidget(self.effect_speed_slider)

        self.effect_speed_value = QLabel("128")
        self.effect_speed_value.setAlignment(Qt.AlignCenter)
        self.effect_speed_value.setObjectName("CardTitle")
        card.body_layout.addWidget(self.effect_speed_value)
        return card

    def _build_fps_card(self) -> SectionCard:
        card = SectionCard(
            "FPS Limit",
            "Caps the frame rate when the firmware supports throttling.",
        )
        self.fps_slider = QSlider(Qt.Horizontal)
        self.fps_slider.setRange(0, 120)
        self.fps_slider.valueChanged.connect(self.on_fps_limit_change)
        card.body_layout.addWidget(self.fps_slider)

        self.fps_value = QLabel("60 FPS")
        self.fps_value.setAlignment(Qt.AlignCenter)
        self.fps_value.setObjectName("CardTitle")
        card.body_layout.addWidget(self.fps_value)
        return card

    def _build_color_card(self) -> SectionCard:
        card = SectionCard(
            "Effect Color",
            "Used by Breathing, Color Cycle, and Reactive modes.",
        )

        self.effect_color_preview = QFrame()
        self.effect_color_preview.setFixedHeight(48)
        self.effect_color_preview.setStyleSheet(
            "border-radius: 12px; border: 1px solid palette(mid); background: #34c759;"
        )
        card.body_layout.addWidget(self.effect_color_preview)

        self.effect_color_label = QLabel("RGB(0, 255, 0)")
        self.effect_color_label.setObjectName("Muted")
        card.body_layout.addWidget(self.effect_color_label)

        card.body_layout.addWidget(make_secondary_button("Choose Color…", self.pick_effect_color))

        # Quick swatches
        swatch_host = QWidget()
        swatch_grid = QGridLayout(swatch_host)
        swatch_grid.setContentsMargins(0, 0, 0, 0)
        swatch_grid.setHorizontalSpacing(6)
        swatch_grid.setVerticalSpacing(6)
        for i, (hex_color, label) in enumerate(_QUICK_COLORS):
            swatch = QToolButton()
            swatch.setText(label)
            swatch.setToolTip(hex_color)
            swatch.setCursor(Qt.PointingHandCursor)
            swatch.setFixedSize(76, 34)
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
            swatch.clicked.connect(lambda _=False, h=hex_color: self.set_effect_color_hex(h))
            swatch_grid.addWidget(swatch, i // 4, i % 4)
        card.body_layout.addWidget(swatch_host)

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
        try:
            setattr(self.controller, "effect_color", self.effect_color)
        except Exception:
            pass

    def _effect_summary_text(self, mode: int) -> str:
        name, description = EFFECT_METADATA.get(
            mode,
            (f"Mode {mode}", "This mode is supported by the device but not described in the UI."),
        )
        return f"{name}: {description}"

    def _set_effect_color(self, rgb: list[int]) -> None:
        self.effect_color = [_clamp(ch) for ch in rgb[:3]]
        self._sync_shared()
        hex_color = _rgb_to_hex(self.effect_color)
        self.effect_color_preview.setStyleSheet(
            f"border-radius: 12px; border: 1px solid palette(mid); background: {hex_color};"
        )
        r, g, b = self.effect_color
        self.effect_color_label.setText(f"RGB({r}, {g}, {b})")

    def _on_mode_toggled(self, checked: bool, mode: int) -> None:
        if not checked or self._loading:
            return
        self.on_effect_mode_change(mode)

    # ------------------------------------------------------------------
    # Event handlers
    # ------------------------------------------------------------------

    def on_effect_mode_change(self, mode: int | None = None) -> None:
        if mode is None:
            for value, btn in self.effect_mode_buttons.items():
                if btn.isChecked():
                    mode = value
                    break
        if mode is None:
            return
        try:
            ok = self.device.set_led_effect(int(mode))
        except Exception as exc:
            self._update_status(f"Failed to set effect mode: {exc}", "error")
            if int(mode) in self.effect_mode_buttons:
                with QSignalBlocker(self.effect_mode_buttons[int(mode)]):
                    self.effect_mode_buttons[int(mode)].setChecked(False)
            self.reload()
            return
        if not ok:
            self._update_status("Device rejected the effect mode.", "error")
            self.reload()
            return
        self._update_status(
            f"Effect mode set to {EFFECT_METADATA.get(int(mode), (f'Mode {mode}', ''))[0]} live.",
            "success",
        )
        self.effect_summary.setText(self._effect_summary_text(int(mode)))

    def on_effect_speed_change(self, value: int) -> None:
        speed = _clamp(value)
        self.effect_speed_value.setText(str(speed))
        try:
            ok = self.device.set_led_effect_speed(speed)
        except Exception as exc:
            self._update_status(f"Failed to set effect speed: {exc}", "error")
            return
        if not ok:
            self._update_status("Device rejected the effect speed.", "error")
            return
        self._update_status(f"Effect speed set to {speed} live.", "success")

    def on_fps_limit_change(self, value: int) -> None:
        fps = _clamp(value)
        self.fps_value.setText("Unlimited" if fps == 0 else f"{fps} FPS")
        try:
            ok = self.device.set_led_fps_limit(fps)
        except Exception as exc:
            self._update_status(f"Failed to set FPS limit: {exc}", "error")
            return
        if not ok:
            self._update_status("Device rejected the FPS limit.", "error")
            return
        self._update_status(
            "FPS limit set to Unlimited live." if fps == 0 else f"FPS limit set to {fps} live.",
            "success",
        )

    def pick_effect_color(self) -> None:
        color = QColorDialog.getColor(QColor(*self.effect_color[:3]), self, "Choose Effect Color")
        if not color.isValid():
            return
        self._set_effect_color([color.red(), color.green(), color.blue()])
        try:
            ok = self.device.set_led_effect_color(color.red(), color.green(), color.blue())
        except Exception as exc:
            self._update_status(f"Failed to set effect color: {exc}", "error")
            return
        if not ok:
            self._update_status("Device rejected the effect color.", "error")
            return
        self._update_status(
            f"Effect color set to RGB({color.red()}, {color.green()}, {color.blue()}) live.",
            "success",
        )

    def set_effect_color_hex(self, hex_color: str) -> None:
        color = QColor(hex_color)
        if not color.isValid():
            self._update_status(f"Invalid effect color value: {hex_color}", "error")
            return
        self._set_effect_color([color.red(), color.green(), color.blue()])
        try:
            ok = self.device.set_led_effect_color(color.red(), color.green(), color.blue())
        except Exception as exc:
            self._update_status(f"Failed to set effect color: {exc}", "error")
            return
        if not ok:
            self._update_status("Device rejected the effect color.", "error")
            return
        self._update_status(f"Effect color set to {hex_color.upper()} live.", "success")

    # ------------------------------------------------------------------
    # Data loading
    # ------------------------------------------------------------------

    def reload(self) -> None:
        errors = []
        self._loading = True
        try:
            mode = self.device.get_led_effect()
            if mode is not None and mode in self.effect_mode_buttons:
                with QSignalBlocker(self.effect_mode_buttons[mode]):
                    self.effect_mode_buttons[mode].setChecked(True)
                self.effect_summary.setText(self._effect_summary_text(int(mode)))
        except Exception as exc:
            errors.append(f"mode: {exc}")

        try:
            speed = self.device.get_led_effect_speed()
            if speed is not None:
                with QSignalBlocker(self.effect_speed_slider):
                    self.effect_speed_slider.setValue(int(speed))
                self.effect_speed_value.setText(str(int(speed)))
        except Exception as exc:
            errors.append(f"speed: {exc}")

        try:
            fps = self.device.get_led_fps_limit()
            if fps is not None:
                with QSignalBlocker(self.fps_slider):
                    self.fps_slider.setValue(int(fps))
                self.fps_value.setText("Unlimited" if int(fps) == 0 else f"{int(fps)} FPS")
        except Exception as exc:
            errors.append(f"fps: {exc}")
        finally:
            self._loading = False

        if errors:
            self._update_status("Effects loaded with warnings: " + "; ".join(errors), "warning")
        else:
            self._update_status("Effect settings loaded from device.", "success")

    load_led_effect_settings = reload
    refresh_from_device = reload

    # ------------------------------------------------------------------
    # Page lifecycle
    # ------------------------------------------------------------------

    def on_page_activated(self) -> None:
        self.reload()

    def on_page_deactivated(self) -> None:
        pass
