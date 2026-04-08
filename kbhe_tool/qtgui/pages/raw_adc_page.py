from __future__ import annotations

import time

from ..common import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QVBoxLayout,
    QWidget,
    Qt,
)
from ..widgets import (
    PageScaffold,
    SectionCard,
    StatusChip,
    make_secondary_button,
)
from ...protocol import KEY_COUNT


def _format_key_label(index: int) -> str:
    return f"K{index + 1:02d}"


class _SensorTile(QFrame):
    def __init__(self, key_index: int, parent=None):
        super().__init__(parent)
        self.setObjectName("SubCard")

        root = QVBoxLayout(self)
        root.setContentsMargins(12, 10, 12, 10)
        root.setSpacing(6)

        top = QHBoxLayout()
        top.setSpacing(8)
        root.addLayout(top)

        self.name_label = QLabel(_format_key_label(key_index))
        self.name_label.setObjectName("Muted")
        top.addWidget(self.name_label)

        top.addStretch(1)

        self.value_label = QLabel("----")
        self.value_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        top.addWidget(self.value_label)

        self.bar = QProgressBar()
        self.bar.setRange(0, 4095)
        self.bar.setValue(0)
        self.bar.setTextVisible(False)
        self.bar.setFixedHeight(10)
        root.addWidget(self.bar)

    def set_value(self, value: int) -> None:
        value = max(0, min(4095, int(value)))
        self.bar.setValue(value)
        self.value_label.setText(str(value))


class RawADCPage(QWidget):
    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session
        self.device = session.device
        self._page_active = False
        self._last_update_monotonic: float | None = None

        try:
            self.session.liveSettingsChanged.connect(self._on_live_settings_changed)
        except Exception:
            pass

        self._build_ui()
        self._on_live_settings_changed(self.session.live_enabled, self.session.live_interval_ms)

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        scaffold = PageScaffold(
            "82 Raw ADC",
            "Vue brute de tous les capteurs Hall. Les valeurs affichées ici sont "
            "les ADC raw directement lus par le firmware, sans LUT ni conversion distance.",
        )
        root.addWidget(scaffold, 1)

        summary = SectionCard(
            "Capture Summary",
            "La page suit le live global et ne poll que lorsqu'elle est visible.",
        )
        scaffold.content_layout.addWidget(summary)

        top = QHBoxLayout()
        top.setSpacing(10)
        summary.body_layout.addLayout(top)

        live_label = QLabel("Live")
        live_label.setObjectName("Muted")
        top.addWidget(live_label)
        self.live_chip = StatusChip("OFF", "neutral")
        top.addWidget(self.live_chip)

        updated_label = QLabel("Last update")
        updated_label.setObjectName("Muted")
        top.addWidget(updated_label)
        self.updated_chip = StatusChip("Never", "neutral")
        top.addWidget(self.updated_chip)

        top.addStretch(1)
        top.addWidget(make_secondary_button("Refresh Raw ADC", self.reload))

        self.stats_label = QLabel("Min --  ·  Max --  ·  Avg --  ·  82 channels")
        self.stats_label.setObjectName("Muted")
        summary.body_layout.addWidget(self.stats_label)

        sensor_card = SectionCard(
            "All 82 Sensors",
            "Chaque barre représente une lecture raw 12-bit sur une touche.",
        )
        scaffold.content_layout.addWidget(sensor_card, 1)

        grid_host = QWidget()
        grid = QGridLayout(grid_host)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(10)
        sensor_card.body_layout.addWidget(grid_host)

        self.sensor_tiles: list[_SensorTile] = []
        columns = 4
        for index in range(KEY_COUNT):
            tile = _SensorTile(index)
            grid.addWidget(tile, index // columns, index % columns)
            self.sensor_tiles.append(tile)

        self.status_chip = StatusChip("ADC raw page ready.", "neutral")
        scaffold.content_layout.addWidget(self.status_chip)
        scaffold.add_stretch()

    def _set_status(self, message: str, level: str = "info") -> None:
        level_map = {"success": "ok", "warning": "warn", "error": "bad", "info": "info"}
        self.status_chip.set_text_and_level(message, level_map.get(level, "neutral"))
        try:
            self.session.set_status(
                message,
                {"success": "ok", "warning": "warn", "error": "danger"}.get(level, "info"),
            )
        except Exception:
            pass

    def _on_live_settings_changed(self, enabled: bool, interval_ms: int) -> None:
        if enabled:
            self.live_chip.set_text_and_level(f"ON @ {int(interval_ms)} ms", "info")
        else:
            self.live_chip.set_text_and_level("OFF", "neutral")

    def _update_summary(self, values: list[int]) -> None:
        if not values:
            self.stats_label.setText("Min --  ·  Max --  ·  Avg --  ·  82 channels")
            return

        min_value = min(values)
        max_value = max(values)
        avg_value = sum(values) / len(values)
        self.stats_label.setText(
            f"Min {min_value}  ·  Max {max_value}  ·  Avg {avg_value:.0f}  ·  {len(values)} channels"
        )

    def _poll_once(self) -> None:
        values = self.device.get_all_raw_adc_values()
        if not values:
            raise RuntimeError("firmware did not return the 82 raw ADC values")

        for index, value in enumerate(values[: len(self.sensor_tiles)]):
            self.sensor_tiles[index].set_value(value)

        self._update_summary(values)
        self._last_update_monotonic = time.monotonic()
        self.updated_chip.set_text_and_level("Just now", "ok")
        self._set_status("82 raw ADC values refreshed.", "success")

    def reload(self) -> None:
        try:
            self._poll_once()
        except Exception as exc:
            self.updated_chip.set_text_and_level("Failed", "bad")
            self._set_status(f"Raw ADC refresh failed: {exc}", "error")

    def on_page_activated(self) -> None:
        self._page_active = True

    def on_page_deactivated(self) -> None:
        self._page_active = False

    def on_live_tick(self) -> None:
        if not self._page_active or not self.session.live_enabled:
            return

        try:
            self._poll_once()
        except Exception as exc:
            self.updated_chip.set_text_and_level("Failed", "bad")
            self._set_status(f"Raw ADC live update failed: {exc}", "error")
            return

        if self._last_update_monotonic is not None:
            age_ms = (time.monotonic() - self._last_update_monotonic) * 1000.0
            if age_ms >= 1.0:
                self.updated_chip.set_text_and_level(f"{age_ms:.0f} ms ago", "info")
