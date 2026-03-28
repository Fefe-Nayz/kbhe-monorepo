from .common import *
from .scroll import ScrollableFrame


class GamepadPageMixin:
    def create_gamepad_settings_widgets(self, parent):
        """Create the Gamepad settings tab widgets."""

        scrollable = ScrollableFrame(parent)
        scrollable.pack(fill=tk.BOTH, expand=True)
        content = scrollable.content

        self._gamepad_preview_vector = (0.0, 0.0)
        self._gamepad_last_preview_error = None
        self._gamepad_loading_gamepad_settings = False
        self._gamepad_apply_job = None
        self._gamepad_axis_options = list(GAMEPAD_AXES.keys())
        self._gamepad_direction_options = list(GAMEPAD_DIRECTIONS.keys())
        self._gamepad_button_options = list(GAMEPAD_BUTTONS.keys())
        self.key_gamepad_vars = []
        self.gamepad_axis_labels = []
        self.gamepad_mapping_summary_labels = []

        self.gamepad_global_summary_var = tk.StringVar()
        self.gamepad_device_state_var = tk.StringVar()
        self.gamepad_preview_state_var = tk.StringVar()
        self.gamepad_mapping_state_var = tk.StringVar()

        banner = ttk.Frame(content)
        banner.pack(fill=tk.X, padx=5, pady=(0, 10))
        ttk.Label(
            banner,
            text="Gamepad settings",
            font=("Arial", 14, "bold"),
        ).pack(anchor=tk.W)
        ttk.Label(
            banner,
            text="Global settings apply live. Deadzone uses the firmware raw range (0-255). Per-key mappings are loaded and applied separately.",
            foreground="gray",
        ).pack(anchor=tk.W, pady=(2, 0))

        top_frame = ttk.Frame(content)
        top_frame.pack(fill=tk.BOTH, expand=True, padx=5)
        top_frame.columnconfigure(0, weight=1)
        top_frame.columnconfigure(1, weight=1)

        # Global settings
        global_frame = ttk.LabelFrame(top_frame, text="Global Settings", padding=12)
        global_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 6), pady=5)
        global_frame.columnconfigure(0, weight=1)

        ttk.Label(
            global_frame,
            textvariable=self.gamepad_global_summary_var,
            wraplength=360,
            foreground="gray",
        ).grid(row=0, column=0, sticky="w")
        ttk.Label(
            global_frame,
            textvariable=self.gamepad_device_state_var,
            wraplength=360,
        ).grid(row=1, column=0, sticky="w", pady=(3, 10))

        deadzone_frame = ttk.LabelFrame(global_frame, text="Deadzone", padding=10)
        deadzone_frame.grid(row=2, column=0, sticky="ew", pady=4)
        deadzone_frame.columnconfigure(0, weight=1)

        ttk.Label(deadzone_frame, text="Firmware raw deadzone (0-255)").grid(row=0, column=0, sticky="w")
        self.gamepad_deadzone_var = tk.IntVar(value=10)
        deadzone_slider = ttk.Scale(
            deadzone_frame,
            from_=0,
            to=255,
            orient=tk.HORIZONTAL,
            variable=self.gamepad_deadzone_var,
            command=self.on_gamepad_deadzone_change,
        )
        deadzone_slider.grid(row=1, column=0, sticky="ew", pady=(6, 2))
        self.gamepad_deadzone_label = ttk.Label(
            deadzone_frame, text=self._gamepad_deadzone_label_text(self.gamepad_deadzone_var.get())
        )
        self.gamepad_deadzone_label.grid(row=2, column=0, sticky="w")

        curve_frame = ttk.LabelFrame(global_frame, text="Analog Curve", padding=10)
        curve_frame.grid(row=3, column=0, sticky="ew", pady=4)
        curve_frame.columnconfigure(0, weight=1)

        self.gamepad_curve_var = tk.IntVar(value=0)
        curves = [
            (0, "Linear", "Direct 1:1 response"),
            (1, "Smooth", "Gentle acceleration"),
            (2, "Aggressive", "Quick response"),
        ]
        for row, (value, title, detail) in enumerate(curves):
            ttk.Radiobutton(
                curve_frame,
                text=f"{title} - {detail}",
                variable=self.gamepad_curve_var,
                value=value,
                command=self._on_gamepad_global_control_changed,
            ).grid(row=row, column=0, sticky="w", pady=2)

        options_frame = ttk.LabelFrame(global_frame, text="Behavior Options", padding=10)
        options_frame.grid(row=4, column=0, sticky="ew", pady=4)
        options_frame.columnconfigure(0, weight=1)

        self.gamepad_square_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            options_frame,
            text="Square mode - visualize and apply a square stick boundary",
            variable=self.gamepad_square_var,
            command=self._on_gamepad_global_control_changed,
        ).grid(row=0, column=0, sticky="w", pady=2)

        self.gamepad_snappy_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            options_frame,
            text="Snappy mode - bias toward faster centering",
            variable=self.gamepad_snappy_var,
            command=self._on_gamepad_global_control_changed,
        ).grid(row=1, column=0, sticky="w", pady=2)

        self.gamepad_kb_always_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            options_frame,
            text="Mirror keyboard output with gamepad",
            variable=self.gamepad_kb_always_var,
            command=self._on_gamepad_global_control_changed,
        ).grid(row=2, column=0, sticky="w", pady=2)

        actions_frame = ttk.Frame(global_frame)
        actions_frame.grid(row=5, column=0, sticky="ew", pady=(8, 0))
        ttk.Button(
            actions_frame,
            text="Reload From Device",
            command=self.load_gamepad_settings,
        ).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(
            actions_frame,
            text="Apply Global Settings",
            command=self.apply_gamepad_settings,
        ).pack(side=tk.LEFT)
        ttk.Label(
            global_frame,
            text="Global changes are live on the device. Use Save to Flash on the Device page if you want them to persist after reboot.",
            foreground="gray",
        ).grid(row=6, column=0, sticky="w", pady=(8, 0))

        # Live preview
        preview_frame = ttk.LabelFrame(top_frame, text="Live Preview", padding=12)
        preview_frame.grid(row=0, column=1, sticky="nsew", padx=(6, 0), pady=5)
        preview_frame.columnconfigure(0, weight=1)

        ttk.Label(
            preview_frame,
            text="This shows raw key travel from the device. Mapping changes are configured below.",
            foreground="gray",
            wraplength=360,
        ).grid(row=0, column=0, sticky="w")
        ttk.Label(
            preview_frame,
            textvariable=self.gamepad_preview_state_var,
            wraplength=360,
        ).grid(row=1, column=0, sticky="w", pady=(3, 8))

        self.gamepad_canvas = tk.Canvas(
            preview_frame,
            width=320,
            height=240,
            bg="#1d232b",
            highlightthickness=1,
            highlightbackground="#526174",
        )
        self.gamepad_canvas.grid(row=2, column=0, sticky="nsew", pady=(0, 8))
        self.gamepad_canvas.bind("<Configure>", self._gamepad_redraw_preview)

        meters_frame = ttk.Frame(preview_frame)
        meters_frame.grid(row=3, column=0, sticky="ew")
        meters_frame.columnconfigure(0, weight=1)
        meters_frame.columnconfigure(1, weight=1)

        for i in range(6):
            row, column = divmod(i, 2)
            meter_frame = ttk.Frame(meters_frame)
            meter_frame.grid(row=row, column=column, sticky="ew", padx=4, pady=3)
            meter_frame.columnconfigure(1, weight=1)

            ttk.Label(meter_frame, text=f"Key {i + 1}", width=7).grid(row=0, column=0, sticky="w")
            bar = ttk.Progressbar(meter_frame, maximum=100, mode="determinate")
            bar.grid(row=0, column=1, sticky="ew", padx=(4, 4))
            label = ttk.Label(meter_frame, text="0%", width=5, anchor="e")
            label.grid(row=0, column=2, sticky="e")
            self.gamepad_axis_labels.append((bar, label))

        preview_actions = ttk.Frame(preview_frame)
        preview_actions.grid(row=4, column=0, sticky="ew", pady=(8, 0))
        self.gamepad_viz_enabled = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            preview_actions,
            text="Enable live visualization",
            variable=self.gamepad_viz_enabled,
            command=self.toggle_gamepad_viz,
        ).pack(anchor=tk.W)

        # Per-key mappings
        mapping_frame = ttk.LabelFrame(content, text="Per-Key Gamepad Mapping", padding=12)
        mapping_frame.pack(fill=tk.X, padx=5, pady=5)
        mapping_frame.columnconfigure(0, weight=1)

        ttk.Label(
            mapping_frame,
            text="Each row stores one key's axis, direction, and optional button mapping in device settings.",
            foreground="gray",
        ).grid(row=0, column=0, sticky="w")
        ttk.Label(
            mapping_frame,
            textvariable=self.gamepad_mapping_state_var,
            foreground="gray",
        ).grid(row=1, column=0, sticky="w", pady=(2, 8))

        table = ttk.Frame(mapping_frame)
        table.grid(row=2, column=0, sticky="ew")
        table.columnconfigure(4, weight=1)

        headers = ("Key", "Axis", "Dir", "Button", "Summary")
        widths = (7, 16, 8, 12, 36)
        for col, (title, width) in enumerate(zip(headers, widths)):
            ttk.Label(table, text=title, width=width, font=("Arial", 9, "bold")).grid(
                row=0, column=col, sticky="w", padx=4, pady=(0, 4)
            )

        for i in range(6):
            row_frame = ttk.Frame(table)
            row_frame.grid(row=i + 1, column=0, columnspan=5, sticky="ew", pady=2)
            row_frame.columnconfigure(4, weight=1)

            ttk.Label(row_frame, text=f"Key {i + 1}", width=7).grid(row=0, column=0, sticky="w", padx=4)

            axis_var = tk.StringVar(value="None")
            direction_var = tk.StringVar(value="+")
            button_var = tk.StringVar(value="None")

            axis_combo = ttk.Combobox(
                row_frame,
                textvariable=axis_var,
                values=self._gamepad_axis_options,
                width=16,
                state="readonly",
            )
            axis_combo.grid(row=0, column=1, sticky="ew", padx=4)

            direction_combo = ttk.Combobox(
                row_frame,
                textvariable=direction_var,
                values=self._gamepad_direction_options,
                width=8,
                state="readonly",
            )
            direction_combo.grid(row=0, column=2, sticky="ew", padx=4)

            button_combo = ttk.Combobox(
                row_frame,
                textvariable=button_var,
                values=self._gamepad_button_options,
                width=12,
                state="readonly",
            )
            button_combo.grid(row=0, column=3, sticky="ew", padx=4)

            summary_label = ttk.Label(row_frame, text="Unassigned", foreground="gray")
            summary_label.grid(row=0, column=4, sticky="w", padx=4)

            for var in (axis_var, direction_var, button_var):
                var.trace_add("write", lambda *_args, index=i: self._gamepad_refresh_mapping_row(index))

            self.key_gamepad_vars.append((axis_var, direction_var, button_var))
            self.gamepad_mapping_summary_labels.append(summary_label)

        mapping_actions = ttk.Frame(mapping_frame)
        mapping_actions.grid(row=3, column=0, sticky="ew", pady=(8, 0))
        ttk.Button(
            mapping_actions,
            text="Reload Mappings",
            command=self.load_gamepad_mapping,
        ).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(
            mapping_actions,
            text="Apply Mapping",
            command=self.apply_gamepad_mapping,
        ).pack(side=tk.LEFT)
        ttk.Label(
            mapping_actions,
            text="Mapping changes are sent to device settings only when you click Apply Mapping.",
            foreground="gray",
        ).pack(side=tk.LEFT, padx=12)

        self._gamepad_refresh_global_summary()
        self._gamepad_update_preview_state("Preview is off.")
        self._gamepad_update_device_state("Connect a device to load global settings and mappings.")
        self._gamepad_update_mapping_state("Mappings are not loaded yet.")

        self.load_gamepad_settings()
        self.after_idle(self._gamepad_redraw_preview)

    def _gamepad_update_status_bundle(self, *, status=None, preview=None, device=None, mapping=None):
        if status is not None and hasattr(self, "status_var"):
            self.status_var.set(status)
        if preview is not None and hasattr(self, "gamepad_preview_state_var"):
            self.gamepad_preview_state_var.set(preview)
        if device is not None and hasattr(self, "gamepad_device_state_var"):
            self.gamepad_device_state_var.set(device)
        if mapping is not None and hasattr(self, "gamepad_mapping_state_var"):
            self.gamepad_mapping_state_var.set(mapping)

    def _gamepad_update_preview_state(self, message):
        self._gamepad_update_status_bundle(preview=message)

    def _gamepad_update_device_state(self, message):
        self._gamepad_update_status_bundle(device=message)

    def _gamepad_update_mapping_state(self, message):
        self._gamepad_update_status_bundle(mapping=message)

    def _gamepad_curve_name(self, curve_value):
        curve_map = {
            0: "Linear",
            1: "Smooth",
            2: "Aggressive",
        }
        try:
            return curve_map.get(int(curve_value), "Linear")
        except (TypeError, ValueError):
            return "Linear"

    @staticmethod
    def _gamepad_deadzone_percent(raw_value):
        try:
            return (max(0, min(255, int(raw_value))) * 100.0) / 255.0
        except (TypeError, ValueError):
            return 0.0

    def _gamepad_deadzone_label_text(self, raw_value):
        try:
            raw = max(0, min(255, int(raw_value)))
        except (TypeError, ValueError):
            raw = 0
        return f"{raw}/255 (~{self._gamepad_deadzone_percent(raw):.1f}%)"

    def _gamepad_choice_from_value(self, value, reverse_map, default="None"):
        try:
            return reverse_map.get(int(value), default)
        except (TypeError, ValueError):
            if isinstance(value, str) and value in reverse_map.values():
                return value
            return default

    def _gamepad_refresh_global_summary(self):
        if not hasattr(self, "gamepad_global_summary_var"):
            return

        summary = " | ".join(
            [
                f"Deadzone {self._gamepad_deadzone_label_text(self.gamepad_deadzone_var.get())}",
                f"Curve {self._gamepad_curve_name(self.gamepad_curve_var.get())}",
                f"Square {'On' if self.gamepad_square_var.get() else 'Off'}",
                f"Snappy {'On' if self.gamepad_snappy_var.get() else 'Off'}",
                f"Keyboard mirror {'On' if self.gamepad_kb_always_var.get() else 'Off'}",
            ]
        )
        self.gamepad_global_summary_var.set(summary)

    def _gamepad_format_mapping_summary(self, axis, direction, button):
        parts = []
        if axis and axis != "None":
            parts.append(f"Axis: {axis} {direction}")
        if button and button != "None":
            parts.append(f"Button: {button}")
        return " | ".join(parts) if parts else "Unassigned"

    def _gamepad_refresh_mapping_row(self, index):
        if not hasattr(self, "gamepad_mapping_summary_labels"):
            return
        if index >= len(self.key_gamepad_vars) or index >= len(self.gamepad_mapping_summary_labels):
            return

        axis_var, direction_var, button_var = self.key_gamepad_vars[index]
        summary = self._gamepad_format_mapping_summary(
            axis_var.get(),
            direction_var.get(),
            button_var.get(),
        )
        self.gamepad_mapping_summary_labels[index].configure(text=summary, foreground="black" if summary != "Unassigned" else "gray")

    def _gamepad_refresh_all_mapping_rows(self):
        for index in range(len(self.key_gamepad_vars)):
            self._gamepad_refresh_mapping_row(index)

    def _gamepad_redraw_preview(self, *_):
        left_x, left_y = getattr(self, "_gamepad_preview_vector", (0.0, 0.0))
        self.draw_gamepad_viz(left_x, left_y)

    def _gamepad_tab_is_active(self):
        return getattr(self, "active_tab", None) == "Gamepad"

    def draw_gamepad_viz(self, left_x=0.0, left_y=0.0):
        """Draw the gamepad joystick visualization."""
        self._gamepad_preview_vector = (float(left_x), float(left_y))

        canvas = getattr(self, "gamepad_canvas", None)
        if canvas is None or not canvas.winfo_exists():
            return

        canvas.delete("all")

        w = max(canvas.winfo_width(), 1)
        h = max(canvas.winfo_height(), 1)
        cx, cy = w // 2, h // 2
        radius = max(20, min(w, h) // 2 - 24)

        outer_fill = "#22303b"
        outer_outline = "#5d7486"
        guide_color = "#405060"
        deadzone_color = "#b05d5d"
        stick_fill = "#2ea8ff"

        if self.gamepad_square_var.get():
            canvas.create_rectangle(
                cx - radius,
                cy - radius,
                cx + radius,
                cy + radius,
                fill=outer_fill,
                outline=outer_outline,
                width=2,
            )
            canvas.create_line(cx - radius, cy, cx + radius, cy, fill=guide_color)
            canvas.create_line(cx, cy - radius, cx, cy + radius, fill=guide_color)
        else:
            canvas.create_oval(
                cx - radius,
                cy - radius,
                cx + radius,
                cy + radius,
                fill=outer_fill,
                outline=outer_outline,
                width=2,
            )
            canvas.create_line(cx - radius, cy, cx + radius, cy, fill=guide_color)
            canvas.create_line(cx, cy - radius, cx, cy + radius, fill=guide_color)

        deadzone = max(0.0, min(1.0, self.gamepad_deadzone_var.get() / 255.0))
        dz_radius = int(radius * deadzone)
        if dz_radius > 0:
            canvas.create_oval(
                cx - dz_radius,
                cy - dz_radius,
                cx + dz_radius,
                cy + dz_radius,
                outline=deadzone_color,
                width=1,
                dash=(3, 3),
            )

        left_x = max(-1.0, min(1.0, float(left_x)))
        left_y = max(-1.0, min(1.0, float(left_y)))
        stick_x = cx + int(left_x * radius)
        stick_y = cy - int(left_y * radius)

        canvas.create_oval(
            stick_x - 9,
            stick_y - 9,
            stick_x + 9,
            stick_y + 9,
            fill=stick_fill,
            outline="white",
            width=2,
        )
        canvas.create_oval(
            cx - 2,
            cy - 2,
            cx + 2,
            cy + 2,
            fill="white",
            outline="white",
        )

        mode_label = "Square mode" if self.gamepad_square_var.get() else "Circle mode"
        canvas.create_text(10, 10, anchor="nw", text="Live joystick preview", fill="white", font=("Arial", 9, "bold"))
        canvas.create_text(
            10,
            28,
            anchor="nw",
            text=f"Deadzone {self._gamepad_deadzone_label_text(self.gamepad_deadzone_var.get())} | {mode_label}",
            fill="#d8e2ec",
            font=("Arial", 8),
        )

        x_pct = int(left_x * 100)
        y_pct = int(left_y * 100)
        canvas.create_text(
            cx,
            h - 12,
            text=f"X {x_pct:+d}%   Y {y_pct:+d}%",
            fill="white",
            font=("Arial", 9, "bold"),
        )

    def toggle_gamepad_viz(self):
        """Toggle live gamepad visualization."""
        if self.gamepad_viz_enabled.get():
            if self._gamepad_tab_is_active():
                self._gamepad_update_preview_state("Live preview running.")
                self._start_gamepad_viz()
            else:
                self._gamepad_update_preview_state("Preview armed. Open the Gamepad tab to start live updates.")
        else:
            self._gamepad_update_preview_state("Preview off.")
            self._stop_gamepad_viz()

    def update_gamepad_viz(self):
        """Update the live gamepad visualization from the device."""
        if not self.gamepad_viz_enabled.get():
            self.gamepad_viz_job = None
            return

        if not self._gamepad_tab_is_active():
            self.gamepad_viz_job = None
            return

        if not getattr(self, "device", None):
            message = "Preview error: no device connected."
            if message != self._gamepad_last_preview_error:
                self._gamepad_last_preview_error = message
                self._gamepad_update_preview_state(message)
                self._gamepad_update_status_bundle(status="Gamepad preview requires a connected device.")
            self.draw_gamepad_viz(0, 0)
            self.gamepad_viz_job = self.after(250, self.update_gamepad_viz)
            return

        try:
            key_states = self.device.get_key_states()
            distances = []
            if key_states and "distances" in key_states:
                distances = key_states["distances"] or []

            for i, (bar, label) in enumerate(self.gamepad_axis_labels):
                if i < len(distances):
                    pct = int((float(distances[i]) * 100) / 255)
                else:
                    pct = 0
                bar["value"] = pct
                label.config(text=f"{pct}%")

            left_x = 0.0
            left_y = 0.0
            if len(distances) >= 4:
                left_x = (float(distances[1]) - float(distances[0])) / 255.0
                left_y = (float(distances[3]) - float(distances[2])) / 255.0

            if self._gamepad_last_preview_error is not None:
                self._gamepad_update_preview_state("Live preview running.")
            self._gamepad_last_preview_error = None
            self.draw_gamepad_viz(left_x, left_y)
        except Exception as exc:
            message = f"Preview error: {exc}"
            if message != self._gamepad_last_preview_error:
                self._gamepad_last_preview_error = message
                self._gamepad_update_preview_state(message)
                self._gamepad_update_status_bundle(status=message)

        if self.gamepad_viz_enabled.get() and self._gamepad_tab_is_active():
            self.gamepad_viz_job = self.after(75, self.update_gamepad_viz)
        else:
            self.gamepad_viz_job = None

    def load_gamepad_settings(self):
        """Load gamepad settings from the device."""
        if not getattr(self, "device", None):
            self._gamepad_update_status_bundle(
                status="No device connected.",
                device="Connect a device to load global settings and mappings.",
            )
            self._gamepad_update_mapping_state("Mappings cannot be loaded without a device.")
            self._gamepad_refresh_global_summary()
            return

        global_message = None
        device_message = None
        mapping_message = None
        status_message = None

        try:
            self._gamepad_loading_gamepad_settings = True
            settings = self.device.get_gamepad_settings()
            if settings:
                self.gamepad_deadzone_var.set(settings.get("deadzone", 10))
                self.gamepad_deadzone_label.config(
                    text=self._gamepad_deadzone_label_text(settings.get("deadzone", 10))
                )
                self.gamepad_curve_var.set(settings.get("curve_type", 0))
                self.gamepad_square_var.set(bool(settings.get("square_mode", False)))
                self.gamepad_snappy_var.set(bool(settings.get("snappy_mode", False)))
                global_message = "Global settings loaded from device."
            else:
                global_message = "Device returned no global gamepad settings."
                if status_message is None:
                    status_message = global_message
        except Exception as exc:
            global_message = f"Global settings load failed: {exc}"
            status_message = global_message
        finally:
            self._gamepad_loading_gamepad_settings = False

        try:
            gp_kb = self.device.get_gamepad_with_keyboard()
            if gp_kb is not None:
                self.gamepad_kb_always_var.set(bool(gp_kb))
                device_message = f"Keyboard mirroring is {'on' if gp_kb else 'off'}."
            else:
                device_message = "Keyboard mirroring state is unavailable."
        except Exception as exc:
            device_message = f"Keyboard mirroring load failed: {exc}"
            if status_message is None:
                status_message = device_message

        loaded_count, missing_keys = self.load_gamepad_mapping()
        if loaded_count == len(self.key_gamepad_vars):
            mapping_message = f"Loaded all {loaded_count} per-key mappings."
        elif loaded_count > 0:
            missing = ", ".join(f"Key {index}" for index in missing_keys) if missing_keys else "unknown keys"
            mapping_message = f"Loaded {loaded_count}/{len(self.key_gamepad_vars)} mappings. Missing: {missing}."
            if status_message is None:
                status_message = mapping_message
        else:
            mapping_message = "No per-key mappings were loaded."
            if status_message is None:
                status_message = mapping_message

        self._gamepad_refresh_global_summary()
        self._gamepad_redraw_preview()

        self._gamepad_update_status_bundle(
            status=status_message or "Gamepad settings loaded.",
            device=f"{global_message} {device_message}".strip(),
            mapping=mapping_message,
        )

    def _on_gamepad_global_control_changed(self):
        self._gamepad_refresh_global_summary()
        if self._gamepad_loading_gamepad_settings:
            return
        self._schedule_gamepad_settings_apply()

    def on_gamepad_deadzone_change(self, value):
        """Handle deadzone slider changes."""
        deadzone = max(0, min(255, int(float(value))))
        self.gamepad_deadzone_label.config(text=self._gamepad_deadzone_label_text(deadzone))
        self._gamepad_refresh_global_summary()
        self._gamepad_redraw_preview()
        if self._gamepad_loading_gamepad_settings:
            return
        self._schedule_gamepad_settings_apply()

    def _schedule_gamepad_settings_apply(self, delay_ms=160):
        if self._gamepad_apply_job is not None:
            self.after_cancel(self._gamepad_apply_job)
        self._gamepad_update_device_state("Applying gamepad settings...")
        self._gamepad_apply_job = self.after(delay_ms, self._apply_gamepad_settings_now)

    def apply_gamepad_settings(self):
        if self._gamepad_apply_job is not None:
            self.after_cancel(self._gamepad_apply_job)
            self._gamepad_apply_job = None
        self._apply_gamepad_settings_now()

    def _apply_gamepad_settings_now(self):
        """Apply global gamepad settings to the device."""
        self._gamepad_apply_job = None
        if not getattr(self, "device", None):
            self._gamepad_update_status_bundle(status="Not connected to a device.")
            self._gamepad_update_device_state("Connect a device before applying global settings.")
            return

        try:
            deadzone = max(0, min(255, int(self.gamepad_deadzone_var.get())))
            curve = int(self.gamepad_curve_var.get())
            square = bool(self.gamepad_square_var.get())
            snappy = bool(self.gamepad_snappy_var.get())
            gp_kb = bool(self.gamepad_kb_always_var.get())

            settings_ok = bool(self.device.set_gamepad_settings(deadzone, curve, square, snappy))
            mirror_ok = bool(self.device.set_gamepad_with_keyboard(gp_kb))

            self._gamepad_refresh_global_summary()
            self._gamepad_redraw_preview()

            if settings_ok and mirror_ok:
                message = "Global gamepad settings applied live. Save to Flash on the Device page to persist them."
                device_message = (
                    f"Keyboard mirroring is {'on' if gp_kb else 'off'}. Current gamepad settings are active in RAM."
                )
            elif settings_ok:
                message = "Global settings applied, but keyboard mirroring update failed."
                device_message = (
                    "Device accepted the gamepad settings in RAM, but keyboard mirroring did not confirm."
                )
            elif mirror_ok:
                message = "Keyboard mirroring updated, but gamepad settings failed."
                device_message = "Device accepted the keyboard mirroring update, but global settings did not confirm."
            else:
                message = "Gamepad settings failed to apply."
                device_message = "The device rejected the global gamepad settings."

            self._gamepad_update_status_bundle(status=message, device=device_message)
        except Exception as exc:
            message = f"Error applying global gamepad settings: {exc}"
            self._gamepad_update_status_bundle(status=message, device=message)

    def apply_gamepad_mapping(self):
        """Apply the per-key gamepad mapping to the device."""
        if not getattr(self, "device", None):
            self._gamepad_update_status_bundle(status="Not connected to a device.")
            self._gamepad_update_mapping_state("Connect a device before applying mappings.")
            return

        try:
            success_count = 0
            failed_keys = []

            for i, (axis_var, direction_var, button_var) in enumerate(self.key_gamepad_vars):
                axis = axis_var.get() if axis_var.get() in self._gamepad_axis_options else "None"
                direction = direction_var.get() if direction_var.get() in self._gamepad_direction_options else "+"
                button = button_var.get() if button_var.get() in self._gamepad_button_options else "None"

                try:
                    success = self.device.set_key_gamepad_map(i, axis, direction, button)
                except Exception:
                    success = False

                if success:
                    success_count += 1
                else:
                    failed_keys.append(i + 1)

            self._gamepad_refresh_all_mapping_rows()

            total = len(self.key_gamepad_vars)
            if success_count == total:
                message = f"Applied mappings for all {success_count} keys."
                mapping_message = "All per-key mappings were updated on the device."
            elif success_count > 0:
                failed_text = ", ".join(f"Key {index}" for index in failed_keys)
                message = f"Applied mappings for {success_count}/{total} keys."
                mapping_message = f"{success_count}/{total} mappings updated. Failed: {failed_text}."
            else:
                message = "Failed to apply per-key mappings."
                mapping_message = "The device rejected every mapping update."

            self._gamepad_update_status_bundle(status=message, mapping=mapping_message)
        except Exception as exc:
            message = f"Error applying gamepad mapping: {exc}"
            self._gamepad_update_status_bundle(status=message, mapping=message)

    def load_gamepad_mapping(self):
        """Load the per-key gamepad mapping from the device."""
        if not getattr(self, "device", None):
            self._gamepad_update_mapping_state("Connect a device to load per-key mappings.")
            return 0, list(range(1, len(self.key_gamepad_vars) + 1))

        loaded_count = 0
        missing_keys = []

        for i, (axis_var, direction_var, button_var) in enumerate(self.key_gamepad_vars):
            try:
                mapping = self.device.get_key_gamepad_map(i)
            except Exception:
                mapping = None

            if mapping:
                axis_var.set(self._gamepad_choice_from_value(mapping.get("axis", 0), GAMEPAD_AXIS_NAMES))
                direction_var.set(self._gamepad_choice_from_value(mapping.get("direction", 0), GAMEPAD_DIRECTION_NAMES, "+"))
                button_var.set(self._gamepad_choice_from_value(mapping.get("button", 0), GAMEPAD_BUTTON_NAMES))
                loaded_count += 1
            else:
                axis_var.set("None")
                direction_var.set("+")
                button_var.set("None")
                missing_keys.append(i + 1)

        self._gamepad_refresh_all_mapping_rows()

        if loaded_count == len(self.key_gamepad_vars):
            self._gamepad_update_mapping_state(f"Loaded all {loaded_count} per-key mappings.")
        elif loaded_count > 0:
            missing = ", ".join(f"Key {index}" for index in missing_keys)
            self._gamepad_update_mapping_state(f"Loaded {loaded_count}/{len(self.key_gamepad_vars)} mappings. Missing: {missing}.")
        else:
            self._gamepad_update_mapping_state("No per-key mappings were loaded.")

        return loaded_count, missing_keys
