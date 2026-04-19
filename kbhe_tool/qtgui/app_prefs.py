from __future__ import annotations

import json
import os
import pathlib

from ..protocol import LEDEffect, LED_EFFECT_NAMES, LED_EFFECT_PARAM_COUNT


def _default_mode() -> int:
    fallback = int(LEDEffect.SOLID_COLOR)
    return fallback if fallback in {int(k) for k in LED_EFFECT_NAMES.keys()} else 0


def _prefs_path() -> pathlib.Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        base_dir = pathlib.Path(appdata)
    else:
        base_dir = pathlib.Path.home() / ".kbhe"
    return base_dir / "KBHE" / "configurator_prefs.json"


class AppPreferences:
    def __init__(self, path: pathlib.Path | None = None):
        self.path = path or _prefs_path()
        self.close_effect_enabled = False
        self.close_effect_mode = _default_mode()
        self.restore_previous_on_startup = True
        self.pending_restore_mode: int | None = None
        self.pending_restore_params: list[int] | None = None
        self.load()

    def load(self) -> None:
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except Exception:
            raw = {}

        self.close_effect_enabled = bool(raw.get("close_effect_enabled", False))

        mode = int(raw.get("close_effect_mode", _default_mode()))
        valid_modes = {int(k) for k in LED_EFFECT_NAMES.keys()}
        self.close_effect_mode = mode if mode in valid_modes else _default_mode()

        self.restore_previous_on_startup = bool(raw.get("restore_previous_on_startup", True))

        pending_mode = raw.get("pending_restore_mode")
        pending_params = raw.get("pending_restore_params")
        if isinstance(pending_mode, int):
            self.pending_restore_mode = int(pending_mode)
        else:
            self.pending_restore_mode = None

        if isinstance(pending_params, list):
            sanitized = [max(0, min(255, int(v))) for v in pending_params[:LED_EFFECT_PARAM_COUNT]]
            while len(sanitized) < LED_EFFECT_PARAM_COUNT:
                sanitized.append(0)
            self.pending_restore_params = sanitized
        else:
            self.pending_restore_params = None

    def save(self) -> None:
        payload = {
            "close_effect_enabled": bool(self.close_effect_enabled),
            "close_effect_mode": int(self.close_effect_mode),
            "restore_previous_on_startup": bool(self.restore_previous_on_startup),
            "pending_restore_mode": self.pending_restore_mode,
            "pending_restore_params": self.pending_restore_params,
        }
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        except Exception:
            # Preferences are best effort and should never crash the app.
            pass

    def set_close_effect_enabled(self, enabled: bool) -> None:
        self.close_effect_enabled = bool(enabled)
        self.save()

    def set_close_effect_mode(self, mode: int) -> None:
        mode = int(mode)
        valid_modes = {int(k) for k in LED_EFFECT_NAMES.keys()}
        self.close_effect_mode = mode if mode in valid_modes else _default_mode()
        self.save()

    def set_restore_previous_on_startup(self, enabled: bool) -> None:
        self.restore_previous_on_startup = bool(enabled)
        if not self.restore_previous_on_startup:
            self.clear_pending_restore(save=False)
        self.save()

    def set_pending_restore(self, mode: int, params: list[int] | None) -> None:
        self.pending_restore_mode = int(mode)
        if params is None:
            self.pending_restore_params = None
        else:
            sanitized = [max(0, min(255, int(v))) for v in list(params)[:LED_EFFECT_PARAM_COUNT]]
            while len(sanitized) < LED_EFFECT_PARAM_COUNT:
                sanitized.append(0)
            self.pending_restore_params = sanitized
        self.save()

    def clear_pending_restore(self, save: bool = True) -> None:
        self.pending_restore_mode = None
        self.pending_restore_params = None
        if save:
            self.save()

    def get_pending_restore(self) -> tuple[int, list[int] | None] | None:
        if self.pending_restore_mode is None:
            return None
        if self.pending_restore_params is None:
            return int(self.pending_restore_mode), None
        return int(self.pending_restore_mode), list(self.pending_restore_params)
