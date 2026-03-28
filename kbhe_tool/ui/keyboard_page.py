from .common import *
from .scroll import ScrollableFrame


class KeyboardPageMixin:
    def create_key_settings_widgets(self, parent):
        """Create Key Settings tab widgets."""

        # Info banner
        ttk.Label(parent, text="⌨️ Configure per-key settings: actuation point, rapid trigger, SOCD, and keycodes",
                  foreground="blue").pack(anchor=tk.W, pady=(0, 10))

        # Key selector
        selector_frame = ttk.Frame(parent)
        selector_frame.pack(fill=tk.X, pady=5)

        ttk.Label(selector_frame, text="Select Key:", font=('Arial', 10, 'bold')).pack(side=tk.LEFT)

        self.selected_key_var = tk.IntVar(value=0)
        for i in range(6):
            rb = ttk.Radiobutton(
                selector_frame, text=f"Key {i+1}",
                variable=self.selected_key_var, value=i,
                command=lambda idx=i: self.load_selected_key_settings(idx)
            )
            rb.pack(side=tk.LEFT, padx=10)

        scrollable = ScrollableFrame(parent)
        scrollable.pack(fill=tk.BOTH, expand=True, pady=5)
        scrollable_frame = scrollable.content

        # === HID Keycode ===
        keycode_frame = ttk.LabelFrame(scrollable_frame, text="🔤 HID Keycode", padding="10")
        keycode_frame.pack(fill=tk.X, pady=5, padx=5)

        keycode_inner = ttk.Frame(keycode_frame)
        keycode_inner.pack(fill=tk.X, pady=5)

        ttk.Label(keycode_inner, text="Keycode:", width=15).pack(side=tk.LEFT)
        self.key_hid_keycode_var = tk.StringVar(value="Q")
        keycode_combo = ttk.Combobox(
            keycode_inner, textvariable=self.key_hid_keycode_var,
            values=list(HID_KEYCODES.keys()), width=15
        )
        keycode_combo.pack(side=tk.LEFT, padx=5)

        # === Fixed Actuation Settings (when Rapid Trigger disabled) ===
        fixed_frame = ttk.LabelFrame(scrollable_frame, text="📍 Fixed Actuation (when Rapid Trigger disabled)", padding="10")
        fixed_frame.pack(fill=tk.X, pady=5, padx=5)

        # Actuation Point
        actuation_row = ttk.Frame(fixed_frame)
        actuation_row.pack(fill=tk.X, pady=3)

        ttk.Label(actuation_row, text="Actuation Point:", width=20).pack(side=tk.LEFT)
        self.key_actuation_var = tk.DoubleVar(value=2.0)
        actuation_slider = ttk.Scale(
            actuation_row, from_=0.1, to=4.0,
            variable=self.key_actuation_var,
            orient=tk.HORIZONTAL, length=200,
            command=lambda v: self.key_actuation_label.config(text=f"{float(v):.1f}mm")
        )
        actuation_slider.pack(side=tk.LEFT, padx=5)
        self.key_actuation_label = ttk.Label(actuation_row, text="2.0mm", width=8)
        self.key_actuation_label.pack(side=tk.LEFT)

        # Release Point
        release_row = ttk.Frame(fixed_frame)
        release_row.pack(fill=tk.X, pady=3)

        ttk.Label(release_row, text="Release Point:", width=20).pack(side=tk.LEFT)
        self.key_release_var = tk.DoubleVar(value=1.8)
        release_slider = ttk.Scale(
            release_row, from_=0.1, to=4.0,
            variable=self.key_release_var,
            orient=tk.HORIZONTAL, length=200,
            command=lambda v: self.key_release_label.config(text=f"{float(v):.1f}mm")
        )
        release_slider.pack(side=tk.LEFT, padx=5)
        self.key_release_label = ttk.Label(release_row, text="1.8mm", width=8)
        self.key_release_label.pack(side=tk.LEFT)

        ttk.Label(fixed_frame, text="💡 Key activates at Actuation Point, releases at Release Point", 
                  foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)

        # === Rapid Trigger Settings ===
        rapid_frame = ttk.LabelFrame(scrollable_frame, text="⚡ Rapid Trigger", padding="10")
        rapid_frame.pack(fill=tk.X, pady=5, padx=5)

        # Enable checkbox
        self.key_rapid_enabled_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            rapid_frame, text="Enable Rapid Trigger for this key",
            variable=self.key_rapid_enabled_var,
            command=self.on_rapid_trigger_toggle
        ).pack(anchor=tk.W)

        # Rapid trigger sub-frame (visible when enabled)
        self.rapid_settings_frame = ttk.Frame(rapid_frame)
        self.rapid_settings_frame.pack(fill=tk.X, pady=5)

        # Activation Distance
        activation_row = ttk.Frame(self.rapid_settings_frame)
        activation_row.pack(fill=tk.X, pady=3)

        ttk.Label(activation_row, text="Activation Distance:", width=20).pack(side=tk.LEFT)
        self.key_rapid_activation_var = tk.DoubleVar(value=0.5)
        activation_slider = ttk.Scale(
            activation_row, from_=0.1, to=2.0,
            variable=self.key_rapid_activation_var,
            orient=tk.HORIZONTAL, length=200,
            command=lambda v: self.key_rapid_activation_label.config(text=f"{float(v):.2f}mm")
        )
        activation_slider.pack(side=tk.LEFT, padx=5)
        self.key_rapid_activation_label = ttk.Label(activation_row, text="0.50mm", width=8)
        self.key_rapid_activation_label.pack(side=tk.LEFT)

        # Press Sensitivity
        press_row = ttk.Frame(self.rapid_settings_frame)
        press_row.pack(fill=tk.X, pady=3)

        ttk.Label(press_row, text="Press Sensitivity:", width=20).pack(side=tk.LEFT)
        self.key_rapid_press_var = tk.DoubleVar(value=0.3)
        press_slider = ttk.Scale(
            press_row, from_=0.1, to=1.0,
            variable=self.key_rapid_press_var,
            orient=tk.HORIZONTAL, length=200,
            command=lambda v: self.key_rapid_press_label.config(text=f"{float(v):.2f}mm")
        )
        press_slider.pack(side=tk.LEFT, padx=5)
        self.key_rapid_press_label = ttk.Label(press_row, text="0.30mm", width=8)
        self.key_rapid_press_label.pack(side=tk.LEFT)

        # Separate press/release sensitivity option
        self.key_separate_sensitivity_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            self.rapid_settings_frame, text="Use separate Press/Release sensitivity",
            variable=self.key_separate_sensitivity_var,
            command=self.on_separate_sensitivity_toggle
        ).pack(anchor=tk.W, pady=3)

        # Release Sensitivity (visible when separate sensitivity enabled)
        self.release_sens_frame = ttk.Frame(self.rapid_settings_frame)

        ttk.Label(self.release_sens_frame, text="Release Sensitivity:", width=20).pack(side=tk.LEFT)
        self.key_rapid_release_var = tk.DoubleVar(value=0.3)
        release_sens_slider = ttk.Scale(
            self.release_sens_frame, from_=0.1, to=1.0,
            variable=self.key_rapid_release_var,
            orient=tk.HORIZONTAL, length=200,
            command=lambda v: self.key_rapid_release_label.config(text=f"{float(v):.2f}mm")
        )
        release_sens_slider.pack(side=tk.LEFT, padx=5)
        self.key_rapid_release_label = ttk.Label(self.release_sens_frame, text="0.30mm", width=8)
        self.key_rapid_release_label.pack(side=tk.LEFT)

        ttk.Label(rapid_frame, text="💡 Rapid trigger activates when key moves down by sensitivity amount after initial activation", 
                  foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)

        # Initialize visibility
        self.on_rapid_trigger_toggle()

        # === SOCD Settings ===
        socd_frame = ttk.LabelFrame(scrollable_frame, text="🔀 SOCD (Simultaneous Opposing Cardinal Directions)", padding="10")
        socd_frame.pack(fill=tk.X, pady=5, padx=5)

        socd_row = ttk.Frame(socd_frame)
        socd_row.pack(fill=tk.X, pady=5)

        ttk.Label(socd_row, text="Paired Key:", width=15).pack(side=tk.LEFT)
        self.key_socd_var = tk.StringVar(value="None")
        socd_combo = ttk.Combobox(
            socd_row, textvariable=self.key_socd_var,
            values=["None", "Key 1", "Key 2", "Key 3", "Key 4", "Key 5", "Key 6"],
            width=10, state="readonly"
        )
        socd_combo.pack(side=tk.LEFT, padx=5)

        ttk.Label(socd_frame, text="💡 When both keys pressed, last pressed wins (Last Input Priority)", 
                  foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)

        # === Gamepad Options ===
        gamepad_key_frame = ttk.LabelFrame(scrollable_frame, text="🎮 Per-Key Gamepad Options", padding="10")
        gamepad_key_frame.pack(fill=tk.X, pady=5, padx=5)

        self.key_disable_kb_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            gamepad_key_frame, text="Disable keyboard output when this key is in gamepad mode",
            variable=self.key_disable_kb_var
        ).pack(anchor=tk.W)

        # === Action Buttons ===
        button_frame = ttk.Frame(scrollable_frame)
        button_frame.pack(fill=tk.X, pady=10, padx=5)

        ttk.Button(button_frame, text="📥 Load Settings", command=self.load_selected_key_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="📤 Apply Settings", command=self.apply_key_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="🔄 Apply to All Keys", command=self.apply_to_all_keys).pack(side=tk.LEFT, padx=5)

        # Load initial
        self.load_selected_key_settings()

    def on_rapid_trigger_toggle(self):
        """Toggle rapid trigger settings visibility."""
        if self.key_rapid_enabled_var.get():
            self.rapid_settings_frame.pack(fill=tk.X, pady=5)
        else:
            self.rapid_settings_frame.pack_forget()

    def on_separate_sensitivity_toggle(self):
        """Toggle separate release sensitivity visibility."""
        if self.key_separate_sensitivity_var.get():
            self.release_sens_frame.pack(fill=tk.X, pady=3)
        else:
            self.release_sens_frame.pack_forget()

    def load_selected_key_settings(self, key_idx=None):
        """Load settings for the selected key."""
        if key_idx is None:
            key_idx = self.selected_key_var.get()
        try:
            settings = self.device.get_key_settings(key_idx)
            if settings:
                # Find keycode name
                keycode = settings.get('hid_keycode', 0x14)
                keyname = HID_KEYCODE_NAMES.get(keycode, 'Q')
                self.key_hid_keycode_var.set(keyname)

                # Fixed actuation settings
                actuation_mm = settings.get('actuation_point_mm', 2.0)
                release_mm = settings.get('release_point_mm', 1.8)
                self.key_actuation_var.set(actuation_mm)
                self.key_release_var.set(release_mm)
                self.key_actuation_label.config(text=f"{actuation_mm:.1f}mm")
                self.key_release_label.config(text=f"{release_mm:.1f}mm")

                # Rapid trigger settings
                rapid_enabled = settings.get('rapid_trigger_enabled', False)
                self.key_rapid_enabled_var.set(rapid_enabled)
                self.on_rapid_trigger_toggle()

                rapid_activation = settings.get('rapid_trigger_activation', 0.5)
                rapid_press = settings.get('rapid_trigger_press', 0.3)
                rapid_release = settings.get('rapid_trigger_release', 0.3)

                self.key_rapid_activation_var.set(rapid_activation)
                self.key_rapid_press_var.set(rapid_press)
                self.key_rapid_release_var.set(rapid_release)
                self.key_rapid_activation_label.config(text=f"{rapid_activation:.2f}mm")
                self.key_rapid_press_label.config(text=f"{rapid_press:.2f}mm")
                self.key_rapid_release_label.config(text=f"{rapid_release:.2f}mm")

                # Separate sensitivity
                separate = rapid_press != rapid_release
                self.key_separate_sensitivity_var.set(separate)
                self.on_separate_sensitivity_toggle()

                # SOCD
                socd = settings.get('socd_pair')
                if socd is not None and socd < 6:
                    self.key_socd_var.set(f"Key {socd + 1}")
                else:
                    self.key_socd_var.set("None")

                # Disable KB on gamepad
                self.key_disable_kb_var.set(settings.get('disable_kb_on_gamepad', False))

                self.status_var.set(f"📥 Loaded settings for Key {key_idx + 1}")
        except Exception as e:
            self.status_var.set(f"❌ Error loading key settings: {e}")

    def apply_key_settings(self):
        """Apply settings for the selected key."""
        key_idx = self.selected_key_var.get()
        try:
            keycode = HID_KEYCODES.get(self.key_hid_keycode_var.get(), 0x14)

            # Get SOCD pair
            socd_str = self.key_socd_var.get()
            if socd_str == "None":
                socd = 255  # No pair
            else:
                socd = int(socd_str.split()[1]) - 1

            # Build settings dict
            settings = {
                'hid_keycode': keycode,
                'actuation_point_mm': self.key_actuation_var.get(),
                'release_point_mm': self.key_release_var.get(),
                'rapid_trigger_enabled': self.key_rapid_enabled_var.get(),
                'rapid_trigger_activation': self.key_rapid_activation_var.get(),
                'rapid_trigger_press': self.key_rapid_press_var.get(),
                'rapid_trigger_release': self.key_rapid_release_var.get() if self.key_separate_sensitivity_var.get() else self.key_rapid_press_var.get(),
                'socd_pair': socd,
                'disable_kb_on_gamepad': self.key_disable_kb_var.get()
            }

            success = self.device.set_key_settings_extended(key_idx, settings)

            if success:
                self.status_var.set(f"📤 Applied settings for Key {key_idx + 1} - LIVE (not saved)")
            else:
                self.status_var.set(f"❌ Failed to apply key settings")
        except Exception as e:
            self.status_var.set(f"❌ Error applying key settings: {e}")

    def apply_to_all_keys(self):
        """Apply current settings to all keys."""
        try:
            for key_idx in range(6):
                keycode = HID_KEYCODES.get(self.key_hid_keycode_var.get(), 0x14)

                socd_str = self.key_socd_var.get()
                if socd_str == "None":
                    socd = 255
                else:
                    socd = int(socd_str.split()[1]) - 1

                settings = {
                    'hid_keycode': keycode,
                    'actuation_point_mm': self.key_actuation_var.get(),
                    'release_point_mm': self.key_release_var.get(),
                    'rapid_trigger_enabled': self.key_rapid_enabled_var.get(),
                    'rapid_trigger_activation': self.key_rapid_activation_var.get(),
                    'rapid_trigger_press': self.key_rapid_press_var.get(),
                    'rapid_trigger_release': self.key_rapid_release_var.get() if self.key_separate_sensitivity_var.get() else self.key_rapid_press_var.get(),
                    'socd_pair': socd,
                    'disable_kb_on_gamepad': self.key_disable_kb_var.get()
                }

                self.device.set_key_settings_extended(key_idx, settings)

            self.status_var.set(f"📤 Applied settings to all 6 keys - LIVE (not saved)")
        except Exception as e:
            self.status_var.set(f"❌ Error applying to all keys: {e}")
