from .common import *
from .theme import APP_COLORS


class EffectsPageMixin:
    EFFECT_MODE_GROUPS = [
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
                (16, "Rainbow Dome"),
                (17, "Sphere"),
                (18, "Dual Sphere"),
                (19, "Strip Spin + Zoom"),
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
            "Reactive + Audio",
            [
                (13, "Reactive (Wave)"),
                (20, "Impact Rainbow"),
                (21, "Reactive Heatmap"),
                (22, "Reactive Ghost"),
                (23, "Audio Spectrum"),
                (15, "Sensor Distance"),
                (24, "Key State Demo"),
            ],
        ),
        (
            "Core",
            [
                (25, "Cycle Pinwheel"),
                (26, "Cycle Spiral"),
                (27, "Cycle Out-In Dual"),
                (28, "Rainbow Beacon"),
                (29, "Rainbow Pinwheels"),
                (30, "Rainbow Moving Chevron"),
                (31, "Hue Breathing"),
                (32, "Hue Pendulum"),
                (33, "Hue Wave"),
                (34, "Riverflow"),
            ],
        ),
        (
            "RGB Matrix",
            [
                (35, "Solid Color"),
                (36, "Alpha Mods"),
                (37, "Gradient Up Down"),
                (38, "Gradient Left Right"),
                (39, "Breathing"),
                (40, "Colorband Sat"),
                (41, "Colorband Val"),
                (42, "Colorband Pinwheel Sat"),
                (43, "Colorband Pinwheel Val"),
                (44, "Colorband Spiral Sat"),
                (45, "Colorband Spiral Val"),
                (46, "Cycle All"),
                (47, "Cycle Left Right"),
                (48, "Cycle Up Down"),
                (49, "Cycle Out In"),
                (50, "Dual Beacon"),
                (51, "Flower Blooming"),
                (52, "Raindrops"),
                (53, "Jellybean Raindrops"),
                (54, "Pixel Rain"),
                (55, "Pixel Flow"),
                (56, "Pixel Fractal"),
                (57, "Typing Heatmap"),
                (58, "Digital Rain"),
                (59, "Solid Reactive Simple"),
                (60, "Solid Reactive"),
                (61, "Solid Reactive Wide"),
                (70, "Solid Reactive Multi Wide"),
                (62, "Solid Reactive Cross"),
                (71, "Solid Reactive Multi Cross"),
                (63, "Solid Reactive Nexus"),
                (72, "Solid Reactive Multi Nexus"),
                (64, "Splash"),
                (73, "Multi Splash"),
                (65, "Solid Splash"),
                (74, "Solid Multi Splash"),
                (66, "Starlight Smooth"),
                (67, "Starlight"),
                (68, "Starlight Dual Sat"),
                (69, "Starlight Dual Hue"),
            ],
        ),
    ]

    EFFECT_MODE_METADATA = {
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
        13: ("Reactive (Wave)", "Launches expanding color ripples from each key press."),
        14: ("Third-Party Live", "Matrix is read-only in this app; live frame is displayed from device state."),
        15: ("Sensor Distance", "Each key LED color changes live according to its sensor travel distance."),
        16: ("Rainbow Dome", "Curved dome-style rainbow centered on the keyboard."),
        17: ("Sphere", "Spherical pulse from a configurable center point."),
        18: ("Dual Sphere", "Two spherical emitters with independent centers and blend."),
        19: ("Strip Spin + Zoom", "Rotating color strips with zoom and twist."),
        20: ("Impact Rainbow", "Rainbow wave with transient key or audio impact boosts."),
        21: ("Reactive Heatmap", "Presses accumulate and fade as a heatmap."),
        22: ("Reactive Ghost", "Soft ghost trails from key presses."),
        23: ("Audio Spectrum", "Host audio-driven spectrum bars with impact accents."),
        24: ("Key State Demo", "Digital key-state colors for pressed versus released keys."),
        25: ("Cycle Pinwheel", "angular hue pinwheel around center."),
        26: ("Cycle Spiral", "radial spiral hue sweep."),
        27: ("Cycle Out-In Dual", "dual out-in hue cycle."),
        28: ("Rainbow Beacon", "rotating rainbow beacon."),
        29: ("Rainbow Pinwheels", "multi-pinwheel rainbow rotation."),
        30: ("Rainbow Moving Chevron", "moving chevron rainbow bands."),
        31: ("Hue Breathing", "hue breathing modulation."),
        32: ("Hue Pendulum", "pendulum hue wave."),
        33: ("Hue Wave", "horizontal hue wave."),
        34: ("Riverflow", "per-key phase riverflow."),
        35: ("Solid Color", "solid color fill."),
        36: ("Alpha Mods", "alpha/modifier split brightness look."),
        37: ("Gradient Up Down", "vertical hue gradient."),
        38: ("Gradient Left Right", "horizontal hue gradient."),
        39: ("Breathing", "color breathing pulse."),
        40: ("Colorband Sat", "moving saturation colorband."),
        41: ("Colorband Val", "moving brightness colorband."),
        42: ("Colorband Pinwheel Sat", "pinwheel saturation band."),
        43: ("Colorband Pinwheel Val", "pinwheel brightness band."),
        44: ("Colorband Spiral Sat", "spiral saturation band."),
        45: ("Colorband Spiral Val", "spiral brightness band."),
        46: ("Cycle All", "global hue cycle."),
        47: ("Cycle Left Right", "left-to-right hue cycle."),
        48: ("Cycle Up Down", "up-down hue cycle."),
        49: ("Cycle Out In", "center radial hue cycle."),
        50: ("Dual Beacon", "dual rotating beacon."),
        51: ("Flower Blooming", "blooming petal pattern."),
        52: ("Raindrops", "ripple-style raindrops."),
        53: ("Jellybean Raindrops", "raindrops with random hues."),
        54: ("Pixel Rain", "falling pixel streaks."),
        55: ("Pixel Flow", "flowing horizontal pixels."),
        56: ("Pixel Fractal", "fractal-like pixel motion."),
        57: ("Typing Heatmap", "typing heatmap."),
        58: ("Digital Rain", "digital rain matrix pattern."),
        59: ("Solid Reactive Simple", "simple solid reactive effect."),
        60: ("Solid Reactive", "radial solid reactive effect."),
        61: ("Solid Reactive Wide", "wide reactive spread."),
        62: ("Solid Reactive Cross", "cross reactive spread."),
        63: ("Solid Reactive Nexus", "nexus reactive blend."),
        64: ("Splash", "colorful splash waves."),
        65: ("Solid Splash", "single-color splash."),
        66: ("Starlight Smooth", "smooth twinkling starlight."),
        67: ("Starlight", "hard twinkling starlight."),
        68: ("Starlight Dual Sat", "dual-color smooth starlight."),
        69: ("Starlight Dual Hue", "dual-hue starlight flashes."),
        70: ("Solid Reactive Multi Wide", "wide reactive spread using all remembered key hits."),
        71: ("Solid Reactive Multi Cross", "cross reactive spread using all remembered key hits."),
        72: ("Solid Reactive Multi Nexus", "nexus reactive blend using all remembered key hits."),
        73: ("Multi Splash", "colorful splash waves from all remembered key hits."),
        74: ("Solid Multi Splash", "single-color splash waves from all remembered key hits."),
    }

    def _set_effect_status(self, message):
        if hasattr(self, "status_var"):
            self.status_var.set(message)

    @staticmethod
    def _clamp_channel(value):
        try:
            return max(0, min(255, int(value)))
        except (TypeError, ValueError):
            return 0

    def _rgb_to_hex(self, rgb):
        r, g, b = (self._clamp_channel(channel) for channel in rgb)
        return f"#{r:02x}{g:02x}{b:02x}"

    def _update_effect_summary(self, mode):
        if hasattr(self, "effect_mode_summary_var"):
            label, description = self.EFFECT_MODE_METADATA.get(
                int(mode),
                (f"Mode {mode}", "This mode is supported by the device but not described in the local UI metadata."),
            )
            self.effect_mode_summary_var.set(f"{label}: {description}")

    def _apply_effect_color(self, rgb, message):
        self.effect_color = [self._clamp_channel(channel) for channel in rgb]
        if hasattr(self, "effect_color_preview"):
            self.effect_color_preview.config(bg=self._rgb_to_hex(self.effect_color))

        try:
            self.device.set_led_effect_color(*self.effect_color)
        except Exception as exc:
            self._set_effect_status(f"❌ Failed to set effect color: {exc}")
            return False

        self._set_effect_status(message)
        return True

    def create_led_effects_widgets(self, parent):
        """Create LED Effects tab widgets."""
        self.effect_color = getattr(self, "effect_color", [0, 255, 0])
        self._effect_speed_job = None
        self._effect_fps_job = None

        intro_frame = ttk.LabelFrame(parent, text="Effect Overview", padding=10, style="Card.TLabelframe")
        intro_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            intro_frame,
            text="These controls adjust the selected effect, its speed, frame rate, and color live on the keyboard.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)
        self.effect_mode_summary_var = tk.StringVar(
            value="Select an effect mode to see a short description here."
        )
        ttk.Label(
            intro_frame,
            textvariable=self.effect_mode_summary_var,
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(6, 0))

        mode_frame = ttk.LabelFrame(parent, text="Effect Mode", padding=10, style="Card.TLabelframe")
        mode_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 10))
        ttk.Label(
            mode_frame,
            text="Choose an effect. The device applies the change immediately.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 8))

        self.effect_mode_var = tk.IntVar(value=0)

        effects_canvas = tk.Canvas(mode_frame, height=240, highlightthickness=0)
        effects_scrollbar = ttk.Scrollbar(mode_frame, orient="vertical", command=effects_canvas.yview)
        effects_inner_frame = ttk.Frame(effects_canvas, style="Surface.TFrame")

        effects_canvas.configure(yscrollcommand=effects_scrollbar.set)
        effects_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        effects_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        effects_canvas_window = effects_canvas.create_window((0, 0), window=effects_inner_frame, anchor="nw")

        def _sync_scrollregion(event):
            effects_canvas.configure(scrollregion=effects_canvas.bbox("all"))

        def _sync_window_width(event):
            effects_canvas.itemconfigure(effects_canvas_window, width=event.width)

        effects_inner_frame.bind("<Configure>", _sync_scrollregion)
        effects_canvas.bind("<Configure>", _sync_window_width)

        for group_name, effect_modes in self.EFFECT_MODE_GROUPS:
            ttk.Label(
                effects_inner_frame,
                text=group_name.upper(),
                style="SidebarGroup.TLabel",
            ).pack(anchor=tk.W, pady=(8, 4))

            for value, text in effect_modes:
                rb = ttk.Radiobutton(
                    effects_inner_frame,
                    text=text,
                    variable=self.effect_mode_var,
                    value=value,
                    command=self.on_effect_mode_change,
                )
                rb.pack(anchor=tk.W, pady=2)

        timing_row = ttk.Frame(parent, style="Surface.TFrame")
        timing_row.pack(fill=tk.X, pady=(0, 10))
        timing_row.columnconfigure(0, weight=1)
        timing_row.columnconfigure(1, weight=1)

        speed_card = ttk.LabelFrame(timing_row, text="Current Effect Speed", padding=10, style="Card.TLabelframe")
        speed_card.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        ttk.Label(
            speed_card,
            text="Controls only the currently selected effect mode.",
            style="SurfaceSubtle.TLabel",
            wraplength=320,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)

        self.effect_speed_var = tk.IntVar(value=128)
        speed_slider = ttk.Scale(
            speed_card,
            from_=1,
            to=255,
            variable=self.effect_speed_var,
            orient=tk.HORIZONTAL,
            command=self.on_effect_speed_change,
        )
        speed_slider.pack(fill=tk.X, pady=(8, 4))

        self.effect_speed_label = ttk.Label(speed_card, text="128", font=("Segoe UI Semibold", 14, "bold"))
        self.effect_speed_label.pack(anchor=tk.CENTER)
        ttk.Label(speed_card, text="1 = slow, 255 = fast (saved per mode)", style="SurfaceSubtle.TLabel").pack(anchor=tk.CENTER, pady=(2, 0))

        fps_card = ttk.LabelFrame(timing_row, text="FPS Limit", padding=10, style="Card.TLabelframe")
        fps_card.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        ttk.Label(
            fps_card,
            text="Caps the frame rate when the firmware supports throttling.",
            style="SurfaceSubtle.TLabel",
            wraplength=320,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)

        self.fps_limit_var = tk.IntVar(value=60)
        fps_slider = ttk.Scale(
            fps_card,
            from_=0,
            to=120,
            variable=self.fps_limit_var,
            orient=tk.HORIZONTAL,
            command=self.on_fps_limit_change,
        )
        fps_slider.pack(fill=tk.X, pady=(8, 4))

        self.fps_limit_label = ttk.Label(fps_card, text="60 FPS", font=("Segoe UI Semibold", 14, "bold"))
        self.fps_limit_label.pack(anchor=tk.CENTER)
        ttk.Label(fps_card, text="0 = unlimited, 1-120 = limit", style="SurfaceSubtle.TLabel").pack(anchor=tk.CENTER, pady=(2, 0))

        color_card = ttk.LabelFrame(parent, text="Effect Color", padding=10, style="Card.TLabelframe")
        color_card.pack(fill=tk.X)
        ttk.Label(
            color_card,
            text="Used by color-aware effects such as Breathing, Reactive, Sphere, and Audio Spectrum.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)

        self.effect_color_preview = tk.Canvas(
            color_card,
            width=260,
            height=42,
            bg=self._rgb_to_hex(self.effect_color),
            bd=0,
            highlightthickness=1,
            highlightbackground=APP_COLORS["border"],
        )
        self.effect_color_preview.pack(fill=tk.X, pady=(8, 8))

        ttk.Button(color_card, text="🎨 Pick Effect Color...", command=self.pick_effect_color, style="Ghost.TButton").pack(fill=tk.X)

        quick_frame = ttk.Frame(color_card, style="Surface.TFrame")
        quick_frame.pack(fill=tk.X, pady=(8, 0))
        quick_colors = [
            ("#ff0000", "Red"),
            ("#00ff00", "Green"),
            ("#0000ff", "Blue"),
            ("#ffffff", "White"),
            ("#ffff00", "Yellow"),
            ("#ff00ff", "Magenta"),
            ("#00ffff", "Cyan"),
            ("#ff8000", "Orange"),
        ]

        for i, (color, label) in enumerate(quick_colors):
            button = tk.Button(
                quick_frame,
                bg=color,
                width=8,
                height=1,
                text=label,
                fg="white" if color not in ["#ffffff", "#ffff00", "#00ffff"] else "black",
                activebackground=color,
                relief=tk.FLAT,
                bd=0,
                highlightthickness=1,
                highlightbackground=APP_COLORS["border"],
                cursor="hand2",
                command=lambda c=color: self.set_effect_color_hex(c),
            )
            button.grid(row=i // 4, column=i % 4, padx=2, pady=2, sticky="nsew")

        for i in range(4):
            quick_frame.columnconfigure(i, weight=1)

        self.load_led_effect_settings()

    def load_led_effect_settings(self):
        """Load current LED effect settings from device."""
        errors = []

        try:
            mode = self.device.get_led_effect()
            if mode is not None:
                self.effect_mode_var.set(mode)
                self._update_effect_summary(mode)
        except Exception as exc:
            errors.append(f"mode: {exc}")

        try:
            speed = self.device.get_led_effect_speed()
            if speed is not None:
                self.effect_speed_var.set(speed)
                self.effect_speed_label.config(text=str(speed))
        except Exception as exc:
            errors.append(f"speed: {exc}")

        try:
            fps = self.device.get_led_fps_limit()
            if fps is not None:
                self.fps_limit_var.set(fps)
                self.fps_limit_label.config(text=f"{fps} FPS" if fps > 0 else "Unlimited")
        except Exception as exc:
            errors.append(f"fps: {exc}")

        try:
            effect_color = self.device.get_led_effect_color()
            if effect_color is not None and len(effect_color) >= 3:
                self.effect_color = [int(effect_color[0]), int(effect_color[1]), int(effect_color[2])]
                if hasattr(self, "effect_color_preview"):
                    self.effect_color_preview.config(bg=self._rgb_to_hex(self.effect_color))
        except Exception as exc:
            errors.append(f"effect color: {exc}")

        if errors:
            self._set_effect_status("⚠️ Error loading effect settings: " + "; ".join(errors))
        else:
            self._set_effect_status("🔄 Loaded current effect settings from device")

    def on_effect_mode_change(self):
        """Handle effect mode change."""
        mode = int(self.effect_mode_var.get())
        label, _ = self.EFFECT_MODE_METADATA.get(mode, (f"Mode {mode}", ""))

        try:
            self.device.set_led_effect(mode)
        except Exception as exc:
            self._set_effect_status(f"❌ Failed to set effect mode: {exc}")
            return

        self._update_effect_summary(mode)

        # Speed is stored in the active mode params, so refresh the slider after each mode switch.
        try:
            speed = self.device.get_led_effect_speed()
            if speed is not None:
                speed = self._clamp_channel(speed)
                self.effect_speed_var.set(speed)
                self.effect_speed_label.config(text=str(speed))
        except Exception:
            pass

        self._set_effect_status(f"🎨 Effect mode set to {label} - LIVE (not saved)")

    def on_effect_speed_change(self, value):
        """Handle effect speed change with a short debounce."""
        speed = self._clamp_channel(float(value))
        self.effect_speed_label.config(text=str(speed))
        if self._effect_speed_job is not None:
            self.after_cancel(self._effect_speed_job)
        self._effect_speed_job = self.after(120, self._commit_effect_speed_change)

    def _commit_effect_speed_change(self):
        self._effect_speed_job = None
        speed = self._clamp_channel(self.effect_speed_var.get())

        try:
            self.device.set_led_effect_speed(speed)
        except Exception as exc:
            self._set_effect_status(f"❌ Failed to set effect speed: {exc}")
            return

        self._set_effect_status(f"⚡ Effect speed set to {speed} - LIVE (not saved)")

    def on_fps_limit_change(self, value):
        """Handle FPS limit change with a short debounce."""
        fps = self._clamp_channel(float(value))
        self.fps_limit_label.config(text=f"{fps} FPS" if fps > 0 else "Unlimited")
        if self._effect_fps_job is not None:
            self.after_cancel(self._effect_fps_job)
        self._effect_fps_job = self.after(120, self._commit_fps_limit_change)

    def _commit_fps_limit_change(self):
        self._effect_fps_job = None
        fps = self._clamp_channel(self.fps_limit_var.get())

        try:
            self.device.set_led_fps_limit(fps)
        except Exception as exc:
            self._set_effect_status(f"❌ Failed to set FPS limit: {exc}")
            return

        self._set_effect_status(f"🎬 FPS limit set to {fps if fps > 0 else 'Unlimited'} - LIVE (not saved)")

    def pick_effect_color(self):
        """Pick effect color."""
        color = colorchooser.askcolor(
            initialcolor=self._rgb_to_hex(self.effect_color),
            title="Choose Effect Color",
        )
        if color and color[0]:
            self._apply_effect_color(
                [int(c) for c in color[0]],
                f"🎨 Effect color set to RGB({int(color[0][0])}, {int(color[0][1])}, {int(color[0][2])}) - LIVE (not saved)",
            )

    def set_effect_color_hex(self, hex_color):
        """Set effect color from hex."""
        try:
            hex_color = hex_color.lstrip("#")
            rgb = [int(hex_color[i:i + 2], 16) for i in (0, 2, 4)]
        except ValueError as exc:
            self._set_effect_status(f"❌ Invalid effect color value: {exc}")
            return

        self._apply_effect_color(rgb, f"🎨 Effect color set to #{hex_color.upper()} - LIVE (not saved)")
