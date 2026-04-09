from .common import *
from .widgets import create_card, create_page_frame, create_section_row, make_status_chip, set_status_style


class KeyboardPageMixin:
    def create_key_settings_widgets(self, parent):
        """Create the keyboard settings screen."""

        _, body = create_page_frame(
            parent,
            "Keyboard",
            "Per-key actuation, rapid trigger, SOCD pairing and HID keycodes. This screen follows the shared selected key from the app shell.",
        )

        if not hasattr(self, "selected_key_var"):
            self.selected_key_var = tk.IntVar(value=0)

        self.key_focus_var = tk.StringVar(value="")
        self.key_status_var = tk.StringVar(
            value="Load the active key from the device, then apply live changes when you are ready."
        )

        focus_card = create_card(
            body,
            "Active Key",
            "The app shell owns selection. This page mirrors that state and updates the active key when you load from device.",
        )
        row = create_section_row(focus_card)
        ttk.Label(row, text="Editing:", width=20).pack(side=tk.LEFT)
        ttk.Label(row, textvariable=self.key_focus_var, style="SectionTitle.TLabel").pack(side=tk.LEFT)
        self.key_status_label = make_status_chip(focus_card, self.key_status_var)

        def add_slider_row(card, label, attr_name, value_attr_name, default, minimum, maximum, digits):
            row = create_section_row(card)
            ttk.Label(row, text=label, width=20).pack(side=tk.LEFT)

            value_var = tk.DoubleVar(value=default)
            setattr(self, attr_name, value_var)
            label_var = tk.StringVar(value=f"{default:.{digits}f}mm")
            setattr(self, value_attr_name, label_var)

            slider = ttk.Scale(
                row,
                from_=minimum,
                to=maximum,
                variable=value_var,
                orient=tk.HORIZONTAL,
                length=250,
                command=lambda value, target=label_var, precision=digits: target.set(
                    f"{float(value):.{precision}f}mm"
                ),
            )
            slider.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
            ttk.Label(row, textvariable=label_var, width=8).pack(side=tk.LEFT)
            return slider

        keycode_card = create_card(
            body,
            "HID Keycode",
            "Choose the keyboard keycode this switch emits when keyboard output is enabled.",
        )
        keycode_row = create_section_row(keycode_card)
        ttk.Label(keycode_row, text="Keycode:", width=20).pack(side=tk.LEFT)
        self.key_hid_keycode_var = tk.StringVar(value="Q")
        keycode_combo = ttk.Combobox(
            keycode_row,
            textvariable=self.key_hid_keycode_var,
            values=list(HID_KEYCODES.keys()),
            width=18,
            state="readonly",
        )
        keycode_combo.pack(side=tk.LEFT, padx=5)

        fixed_card = create_card(
            body,
            "Fixed Actuation",
            "Used when rapid trigger is disabled. Release point remains independent so you can keep the key from chattering.",
        )
        add_slider_row(fixed_card, "Actuation Point:", "key_actuation_var", "key_actuation_label_var", 2.0, 0.1, 4.0, 1)
        add_slider_row(fixed_card, "Release Point:", "key_release_var", "key_release_label_var", 1.8, 0.1, 4.0, 1)
        ttk.Label(
            fixed_card,
            text="Key activates at the actuation point and releases at the release point.",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W, pady=(6, 0))

        rapid_card = create_card(
            body,
            "Rapid Trigger",
            "Enables rapid re-activation as the key moves back through a narrow sensitivity window.",
        )
        self.key_rapid_enabled_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            rapid_card,
            text="Enable rapid trigger for this key",
            variable=self.key_rapid_enabled_var,
            command=self.on_rapid_trigger_toggle,
        ).pack(anchor=tk.W)

        self.rapid_settings_frame = ttk.Frame(rapid_card, style="Surface.TFrame")
        self.rapid_settings_frame.pack(fill=tk.X, pady=(8, 2))

        add_slider_row(
            self.rapid_settings_frame,
            "Activation Distance:",
            "key_rapid_activation_var",
            "key_rapid_activation_label_var",
            0.5,
            0.1,
            2.0,
            2,
        )
        add_slider_row(
            self.rapid_settings_frame,
            "Press Sensitivity:",
            "key_rapid_press_var",
            "key_rapid_press_label_var",
            0.3,
            0.1,
            1.0,
            2,
        )

        self.key_separate_sensitivity_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            self.rapid_settings_frame,
            text="Use separate press/release sensitivity",
            variable=self.key_separate_sensitivity_var,
            command=self.on_separate_sensitivity_toggle,
        ).pack(anchor=tk.W, pady=(4, 0))

        self.release_sens_frame = ttk.Frame(self.rapid_settings_frame, style="Surface.TFrame")
        add_slider_row(
            self.release_sens_frame,
            "Release Sensitivity:",
            "key_rapid_release_var",
            "key_rapid_release_label_var",
            0.3,
            0.1,
            1.0,
            2,
        )

        ttk.Label(
            rapid_card,
            text="Rapid trigger fires when the key moves by the configured sensitivity after the initial actuation.",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W, pady=(6, 0))

        self.on_rapid_trigger_toggle()

        socd_card = create_card(
            body,
            "SOCD",
            "Set a paired key and choose how simultaneous opposite inputs are resolved.",
        )
        socd_row = create_section_row(socd_card)
        ttk.Label(socd_row, text="Paired Key:", width=20).pack(side=tk.LEFT)
        self.key_socd_var = tk.StringVar(value="None")
        self.key_socd_combo = ttk.Combobox(
            socd_row,
            textvariable=self.key_socd_var,
            values=["None", "Key 1", "Key 2", "Key 3", "Key 4", "Key 5", "Key 6"],
            width=12,
            state="readonly",
        )
        self.key_socd_combo.pack(side=tk.LEFT, padx=5)

        socd_resolution_row = create_section_row(socd_card)
        ttk.Label(socd_resolution_row, text="Resolution:", width=20).pack(side=tk.LEFT)
        self.key_socd_resolution_var = tk.StringVar(value="Last Input Wins")
        self.key_socd_resolution_combo = ttk.Combobox(
            socd_resolution_row,
            textvariable=self.key_socd_resolution_var,
            values=list(SOCD_RESOLUTIONS.keys()),
            width=18,
            state="readonly",
        )
        self.key_socd_resolution_combo.pack(side=tk.LEFT, padx=5)

        self.socd_hint_var = tk.StringVar(value="")
        ttk.Label(
            socd_card,
            textvariable=self.socd_hint_var,
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W, pady=(6, 0))
        self.key_socd_combo.bind("<<ComboboxSelected>>", lambda _event: self._update_socd_hint())
        self.key_socd_resolution_combo.bind("<<ComboboxSelected>>", lambda _event: self._update_socd_hint())
        self._update_socd_hint()

        gamepad_card = create_card(
            body,
            "Gamepad Output",
            "Per-key behavior in gamepad mode stays consistent with the device firmware settings.",
        )
        self.key_disable_kb_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            gamepad_card,
            text="Disable keyboard output when this key is used in gamepad mode",
            variable=self.key_disable_kb_var,
        ).pack(anchor=tk.W)

        actions_card = create_card(body, "Actions", "Load the current device state, apply the active key, or copy it to all keys.")
        button_row = create_section_row(actions_card)
        ttk.Button(button_row, text="Load Settings", command=self.load_selected_key_settings, style="Ghost.TButton").pack(
            side=tk.LEFT
        )
        ttk.Button(button_row, text="Apply Settings", command=self.apply_key_settings, style="Primary.TButton").pack(
            side=tk.LEFT, padx=6
        )
        ttk.Button(button_row, text="Apply to All Keys", command=self.apply_to_all_keys, style="Ghost.TButton").pack(
            side=tk.LEFT
        )

        self.load_selected_key_settings()

    def _selected_key_index(self):
        try:
            if hasattr(self, "selected_key_var"):
                return max(0, min(5, int(self.selected_key_var.get())))
        except Exception:
            pass
        return 0

    def _set_keyboard_status(self, message, level="default"):
        if hasattr(self, "status_var"):
            self.status_var.set(message)
        if hasattr(self, "key_status_var"):
            self.key_status_var.set(message)
        if hasattr(self, "key_status_label"):
            set_status_style(self.key_status_label, level)

    def _sync_key_focus(self, key_idx):
        key_idx = max(0, min(5, int(key_idx)))
        if hasattr(self, "selected_key_var") and int(self.selected_key_var.get()) != key_idx:
            self.selected_key_var.set(key_idx)
        if hasattr(self, "key_focus_var"):
            self.key_focus_var.set(f"Key {key_idx + 1}")
        return key_idx

    def on_rapid_trigger_toggle(self):
        """Toggle rapid trigger settings visibility."""
        if getattr(self, "key_rapid_enabled_var", None) and self.key_rapid_enabled_var.get():
            self.rapid_settings_frame.pack(fill=tk.X, pady=(8, 2))
        else:
            self.rapid_settings_frame.pack_forget()

    def on_separate_sensitivity_toggle(self):
        """Toggle separate release sensitivity visibility."""
        if getattr(self, "key_separate_sensitivity_var", None) and self.key_separate_sensitivity_var.get():
            self.release_sens_frame.pack(fill=tk.X, pady=(6, 0))
        else:
            self.release_sens_frame.pack_forget()

    def _build_key_settings_payload(self):
        keycode = HID_KEYCODES.get(self.key_hid_keycode_var.get(), 0x14)

        socd_str = self.key_socd_var.get()
        if socd_str == "None":
            socd = 255
        else:
            try:
                socd = int(socd_str.split()[-1]) - 1
            except (ValueError, IndexError) as exc:
                raise ValueError("Select a valid SOCD partner or None") from exc

        return {
            "hid_keycode": keycode,
            "actuation_point_mm": self.key_actuation_var.get(),
            "release_point_mm": self.key_release_var.get(),
            "rapid_trigger_enabled": self.key_rapid_enabled_var.get(),
            "rapid_trigger_activation": self.key_rapid_activation_var.get(),
            "rapid_trigger_press": self.key_rapid_press_var.get(),
            "rapid_trigger_release": (
                self.key_rapid_release_var.get()
                if self.key_separate_sensitivity_var.get()
                else self.key_rapid_press_var.get()
            ),
            "socd_pair": socd,
            "socd_resolution": SOCD_RESOLUTIONS.get(
                self.key_socd_resolution_var.get(),
                SOCD_RESOLUTIONS["Last Input Wins"],
            ),
            "disable_kb_on_gamepad": self.key_disable_kb_var.get(),
        }

    def load_selected_key_settings(self, key_idx=None):
        """Load settings for the selected key."""
        if not self.device:
            self._set_keyboard_status("Keyboard settings are unavailable because no device is connected.", "warn")
            return

        if key_idx is None:
            key_idx = self._selected_key_index()
        key_idx = self._sync_key_focus(key_idx)

        try:
            settings = self.device.get_key_settings(key_idx)
            if not settings:
                self._set_keyboard_status(f"No key settings were returned for Key {key_idx + 1}.", "warn")
                return

            keycode = settings.get("hid_keycode", 0x14)
            self.key_hid_keycode_var.set(HID_KEYCODE_NAMES.get(keycode, "Q"))

            actuation_mm = float(settings.get("actuation_point_mm", 2.0))
            release_mm = float(settings.get("release_point_mm", 1.8))
            self.key_actuation_var.set(actuation_mm)
            self.key_release_var.set(release_mm)
            self.key_actuation_label_var.set(f"{actuation_mm:.1f}mm")
            self.key_release_label_var.set(f"{release_mm:.1f}mm")

            rapid_enabled = bool(settings.get("rapid_trigger_enabled", False))
            self.key_rapid_enabled_var.set(rapid_enabled)
            self.on_rapid_trigger_toggle()

            rapid_activation = float(settings.get("rapid_trigger_activation", 0.5))
            rapid_press = float(settings.get("rapid_trigger_press", 0.3))
            rapid_release = float(settings.get("rapid_trigger_release", rapid_press))
            self.key_rapid_activation_var.set(rapid_activation)
            self.key_rapid_press_var.set(rapid_press)
            self.key_rapid_release_var.set(rapid_release)
            self.key_rapid_activation_label_var.set(f"{rapid_activation:.2f}mm")
            self.key_rapid_press_label_var.set(f"{rapid_press:.2f}mm")
            self.key_rapid_release_label_var.set(f"{rapid_release:.2f}mm")

            separate = abs(rapid_press - rapid_release) > 0.0001
            self.key_separate_sensitivity_var.set(separate)
            self.on_separate_sensitivity_toggle()

            socd = settings.get("socd_pair")
            if socd is not None and socd != 255 and 0 <= int(socd) < 6:
                self.key_socd_var.set(f"Key {int(socd) + 1}")
            else:
                self.key_socd_var.set("None")
            self.key_socd_resolution_var.set(
                SOCD_RESOLUTION_NAMES.get(
                    int(settings.get("socd_resolution", 0)),
                    "Last Input Wins",
                )
            )
            self._update_socd_hint()

            self.key_disable_kb_var.set(bool(settings.get("disable_kb_on_gamepad", False)))

            self._set_keyboard_status(f"Loaded settings for Key {key_idx + 1}.", "ok")
        except Exception as exc:
            self._set_keyboard_status(f"Error loading key settings: {exc}", "danger")

    def apply_key_settings(self):
        """Apply settings for the selected key."""
        if not self.device:
            self._set_keyboard_status("Cannot apply keyboard settings because no device is connected.", "warn")
            return

        key_idx = self._selected_key_index()
        try:
            settings = self._build_key_settings_payload()
            success = self.device.set_key_settings_extended(key_idx, settings)
            if success:
                self._set_keyboard_status(f"Applied settings for Key {key_idx + 1} - live only, not saved.", "ok")
            else:
                self._set_keyboard_status(f"Failed to apply settings for Key {key_idx + 1}.", "danger")
        except Exception as exc:
            self._set_keyboard_status(f"Error applying key settings: {exc}", "danger")

    def apply_to_all_keys(self):
        """Apply current settings to all keys."""
        if not self.device:
            self._set_keyboard_status("Cannot copy keyboard settings because no device is connected.", "warn")
            return

        try:
            settings = self._build_key_settings_payload()
            failed_keys = []
            self._set_keyboard_status("Applying the current key settings to all 6 keys...", "warn")
            self.update_idletasks()

            for key_idx in range(6):
                if not self.device.set_key_settings_extended(key_idx, settings):
                    failed_keys.append(key_idx + 1)

            if failed_keys:
                self._set_keyboard_status(
                    f"Applied settings to most keys, but these failed: {', '.join(f'Key {key}' for key in failed_keys)}.",
                    "danger",
                )
            else:
                self._set_keyboard_status("Applied settings to all 6 keys - live only, not saved.", "ok")
        except Exception as exc:
            self._set_keyboard_status(f"Error applying settings to all keys: {exc}", "danger")

    def _update_socd_hint(self):
        socd = self.key_socd_var.get()
        resolution = SOCD_RESOLUTIONS.get(
            self.key_socd_resolution_var.get(),
            SOCD_RESOLUTIONS["Last Input Wins"],
        )

        if socd == "None":
            self.socd_hint_var.set("SOCD is disabled until a paired key is selected.")
        elif resolution == SOCD_RESOLUTIONS["Most Pressed Wins"]:
            self.socd_hint_var.set("When both paired keys are held, the deeper press wins.")
        else:
            self.socd_hint_var.set("When both paired keys are held, the last pressed key wins.")
