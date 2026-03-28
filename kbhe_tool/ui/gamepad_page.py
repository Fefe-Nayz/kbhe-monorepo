from .common import *
from .scroll import ScrollableFrame


class GamepadPageMixin:
    def create_gamepad_settings_widgets(self, parent):
        """Create Gamepad Settings tab widgets."""

        scrollable = ScrollableFrame(parent)
        scrollable.pack(fill=tk.BOTH, expand=True)
        scrollable_frame = scrollable.content

        # Info banner
        ttk.Label(scrollable_frame, text="🎮 Configure gamepad analog behavior and per-key mapping",
                  foreground="blue").pack(anchor=tk.W, pady=(0, 10), padx=5)

        # Two-column layout
        main_frame = ttk.Frame(scrollable_frame)
        main_frame.pack(fill=tk.BOTH, expand=True)

        left_col = ttk.Frame(main_frame)
        left_col.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        right_col = ttk.Frame(main_frame)
        right_col.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        # === LEFT COLUMN: Global Settings ===

        # Deadzone
        deadzone_frame = ttk.LabelFrame(left_col, text="📏 Deadzone", padding="10")
        deadzone_frame.pack(fill=tk.X, pady=5)

        ttk.Label(deadzone_frame, text="Deadzone (0-50%):").pack(anchor=tk.W)
        self.gamepad_deadzone_var = tk.IntVar(value=10)
        deadzone_slider = ttk.Scale(
            deadzone_frame, from_=0, to=50,
            variable=self.gamepad_deadzone_var,
            orient=tk.HORIZONTAL,
            command=self.on_gamepad_deadzone_change
        )
        deadzone_slider.pack(fill=tk.X, padx=5)
        self.gamepad_deadzone_label = ttk.Label(deadzone_frame, text="10%", font=('Arial', 12))
        self.gamepad_deadzone_label.pack()

        # Curve Type
        curve_frame = ttk.LabelFrame(left_col, text="📈 Analog Curve", padding="10")
        curve_frame.pack(fill=tk.X, pady=5)

        self.gamepad_curve_var = tk.IntVar(value=0)

        curves = [
            (0, "Linear - Direct 1:1 mapping"),
            (1, "Smooth - Gentle acceleration"),
            (2, "Aggressive - Quick response")
        ]

        for val, text in curves:
            rb = ttk.Radiobutton(
                curve_frame, text=text, variable=self.gamepad_curve_var,
                value=val, command=self.apply_gamepad_settings
            )
            rb.pack(anchor=tk.W, pady=2)

        # Joystick Options
        options_frame = ttk.LabelFrame(left_col, text="🔲 Joystick Options", padding="10")
        options_frame.pack(fill=tk.X, pady=5)

        self.gamepad_square_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            options_frame, text="Square Mode - Circular to square",
            variable=self.gamepad_square_var,
            command=self.apply_gamepad_settings
        ).pack(anchor=tk.W, pady=2)

        self.gamepad_snappy_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            options_frame, text="Snappy Mode - Faster centering",
            variable=self.gamepad_snappy_var,
            command=self.apply_gamepad_settings
        ).pack(anchor=tk.W, pady=2)

        self.gamepad_kb_always_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            options_frame, text="Send keyboard along with gamepad",
            variable=self.gamepad_kb_always_var,
            command=self.apply_gamepad_settings
        ).pack(anchor=tk.W, pady=2)

        # === RIGHT COLUMN: Visualization ===

        viz_frame = ttk.LabelFrame(right_col, text="📺 Live Gamepad Preview", padding="10")
        viz_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        # Gamepad visualization canvas
        self.gamepad_canvas = tk.Canvas(viz_frame, width=200, height=200, bg='#2d2d2d')
        self.gamepad_canvas.pack(pady=5)

        # Key axis display
        axis_frame = ttk.Frame(viz_frame)
        axis_frame.pack(fill=tk.X, pady=5)

        self.gamepad_axis_labels = []
        for i in range(6):
            frame = ttk.Frame(axis_frame)
            frame.pack(fill=tk.X, pady=1)
            ttk.Label(frame, text=f"Key {i+1}:", width=8).pack(side=tk.LEFT)
            bar = ttk.Progressbar(frame, length=100, maximum=100, mode='determinate')
            bar.pack(side=tk.LEFT, padx=5)
            label = ttk.Label(frame, text="0%", width=6)
            label.pack(side=tk.LEFT)
            self.gamepad_axis_labels.append((bar, label))

        # Start visualization update
        self.gamepad_viz_enabled = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            viz_frame, text="Enable live visualization",
            variable=self.gamepad_viz_enabled,
            command=self.toggle_gamepad_viz
        ).pack()

        # Draw initial state
        self.draw_gamepad_viz(0, 0)

        # === PER-KEY MAPPING ===

        mapping_frame = ttk.LabelFrame(scrollable_frame, text="🎯 Per-Key Gamepad Mapping", padding="10")
        mapping_frame.pack(fill=tk.X, pady=5, padx=5)

        ttk.Label(mapping_frame, text="Configure what gamepad input each key produces:", 
                  foreground="gray").pack(anchor=tk.W)

        # Mapping grid
        mapping_grid = ttk.Frame(mapping_frame)
        mapping_grid.pack(fill=tk.X, pady=5)

        # Headers
        ttk.Label(mapping_grid, text="Key", width=6, font=('Arial', 9, 'bold')).grid(row=0, column=0)
        ttk.Label(mapping_grid, text="Axis", width=12, font=('Arial', 9, 'bold')).grid(row=0, column=1)
        ttk.Label(mapping_grid, text="Direction", width=10, font=('Arial', 9, 'bold')).grid(row=0, column=2)
        ttk.Label(mapping_grid, text="Button", width=10, font=('Arial', 9, 'bold')).grid(row=0, column=3)

        self.key_gamepad_vars = []
        axis_options = ["Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "Trigger L", "Trigger R"]
        dir_options = ["+", "-"]
        button_options = ["None", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Select"]

        for i in range(6):
            ttk.Label(mapping_grid, text=f"Key {i+1}").grid(row=i+1, column=0, pady=2)

            axis_var = tk.StringVar(value=axis_options[i % len(axis_options)])
            axis_combo = ttk.Combobox(mapping_grid, textvariable=axis_var, 
                                      values=axis_options, width=12, state='readonly')
            axis_combo.grid(row=i+1, column=1, padx=2)

            dir_var = tk.StringVar(value=dir_options[i % 2])
            dir_combo = ttk.Combobox(mapping_grid, textvariable=dir_var, 
                                     values=dir_options, width=8, state='readonly')
            dir_combo.grid(row=i+1, column=2, padx=2)

            btn_var = tk.StringVar(value="None")
            btn_combo = ttk.Combobox(mapping_grid, textvariable=btn_var, 
                                     values=button_options, width=8, state='readonly')
            btn_combo.grid(row=i+1, column=3, padx=2)

            self.key_gamepad_vars.append((axis_var, dir_var, btn_var))

        # Buttons
        btn_frame = ttk.Frame(scrollable_frame)
        btn_frame.pack(fill=tk.X, pady=10, padx=5)

        ttk.Button(btn_frame, text="🔄 Reload Settings", 
                   command=self.load_gamepad_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="📤 Apply Mapping", 
                   command=self.apply_gamepad_mapping).pack(side=tk.LEFT, padx=5)

        # Load initial
        self.load_gamepad_settings()

    def draw_gamepad_viz(self, left_x, left_y):
        """Draw gamepad joystick visualization."""
        canvas = self.gamepad_canvas
        canvas.delete('all')

        w = canvas.winfo_width() or 200
        h = canvas.winfo_height() or 200

        # Center
        cx, cy = w // 2, h // 2
        radius = min(w, h) // 2 - 20

        # Draw outer circle (joystick boundary)
        canvas.create_oval(cx - radius, cy - radius, cx + radius, cy + radius, 
                           outline='#555', width=2)

        # Draw grid lines
        canvas.create_line(cx - radius, cy, cx + radius, cy, fill='#444')
        canvas.create_line(cx, cy - radius, cx, cy + radius, fill='#444')

        # Draw deadzone circle
        deadzone = self.gamepad_deadzone_var.get() / 100
        dz_radius = int(radius * deadzone)
        if dz_radius > 0:
            canvas.create_oval(cx - dz_radius, cy - dz_radius, 
                               cx + dz_radius, cy + dz_radius, 
                               outline='#800', width=1, dash=(2, 2))

        # Draw joystick position
        stick_x = cx + int(left_x * radius)
        stick_y = cy - int(left_y * radius)  # Y inverted

        # Stick indicator
        canvas.create_oval(stick_x - 10, stick_y - 10, stick_x + 10, stick_y + 10, 
                           fill='#0af', outline='white', width=2)

        # Labels
        canvas.create_text(cx, 10, text="Joystick", fill='white', font=('Arial', 9))
        x_pct = int(left_x * 100)
        y_pct = int(left_y * 100)
        canvas.create_text(cx, h - 10, text=f"X:{x_pct:+d}% Y:{y_pct:+d}%", fill='white', font=('Arial', 8))

    def toggle_gamepad_viz(self):
        """Toggle live gamepad visualization."""
        if self.gamepad_viz_enabled.get():
            if self.active_tab == "Gamepad":
                self._start_gamepad_viz()
            else:
                self.status_var.set("Gamepad visualization armed. Open the Gamepad tab to start it.")
        else:
            self._stop_gamepad_viz()

    def update_gamepad_viz(self):
        """Update gamepad visualization with current key states."""
        if not self.gamepad_viz_enabled.get():
            self.gamepad_viz_job = None
            return

        try:
            # Get key states from device
            key_states = self.device.get_key_states()
            if key_states and 'distances' in key_states:
                distances = key_states['distances']

                # Update axis bars
                for i, (bar, label) in enumerate(self.gamepad_axis_labels):
                    if i < len(distances):
                        pct = int(distances[i] * 100 / 255)
                        bar['value'] = pct
                        label.config(text=f"{pct}%")

                # Calculate joystick position from keys
                # Assuming key 0,2,4 are + and 1,3,5 are -
                # Simple mapping: keys 0-3 for left stick
                left_x = 0
                left_y = 0
                if len(distances) >= 4:
                    # Keys 0,1 = X axis, Keys 2,3 = Y axis
                    left_x = (distances[1] - distances[0]) / 255.0
                    left_y = (distances[3] - distances[2]) / 255.0

                self.draw_gamepad_viz(left_x, left_y)
        except Exception as e:
            pass

        # Schedule next update
        if self.gamepad_viz_enabled.get() and self.active_tab == "Gamepad":
            self.gamepad_viz_job = self.after(50, self.update_gamepad_viz)
        else:
            self.gamepad_viz_job = None

    def load_gamepad_settings(self):
        """Load gamepad settings from device."""
        try:
            settings = self.device.get_gamepad_settings()
            if settings:
                self.gamepad_deadzone_var.set(settings.get('deadzone', 10))
                self.gamepad_deadzone_label.config(text=f"{settings.get('deadzone', 10)}%")
                self.gamepad_curve_var.set(settings.get('curve_type', 0))
                self.gamepad_square_var.set(settings.get('square_mode', False))
                self.gamepad_snappy_var.set(settings.get('snappy_mode', False))

            # Also load per-key gamepad mapping
            self.load_gamepad_mapping()

            # Load gamepad+keyboard mode
            gp_kb = self.device.get_gamepad_with_keyboard()
            if gp_kb is not None:
                self.gamepad_kb_always_var.set(gp_kb)

            self.status_var.set("🔄 Loaded gamepad settings")
        except Exception as e:
            self.status_var.set(f"❌ Error loading gamepad settings: {e}")

    def on_gamepad_deadzone_change(self, value):
        """Handle deadzone slider change."""
        deadzone = int(float(value))
        self.gamepad_deadzone_label.config(text=f"{deadzone}%")
        self.apply_gamepad_settings()

    def apply_gamepad_settings(self):
        """Apply gamepad settings to device."""
        try:
            deadzone = self.gamepad_deadzone_var.get()
            curve = self.gamepad_curve_var.get()
            square = self.gamepad_square_var.get()
            snappy = self.gamepad_snappy_var.get()

            success = self.device.set_gamepad_settings(deadzone, curve, square, snappy)

            # Also send gamepad+keyboard mode
            gp_kb = self.gamepad_kb_always_var.get()
            self.device.set_gamepad_with_keyboard(gp_kb)

            if success:
                self.status_var.set("🎮 Gamepad settings applied - LIVE (not saved)")
        except Exception as e:
            self.status_var.set(f"❌ Error applying gamepad settings: {e}")

    def apply_gamepad_mapping(self):
        """Apply per-key gamepad mapping to device."""
        if not self.device:
            self.status_var.set("❌ Not connected to device")
            return

        try:
            success_count = 0
            for i, (axis_var, dir_var, btn_var) in enumerate(self.key_gamepad_vars):
                axis = axis_var.get()
                direction = dir_var.get()
                button = btn_var.get()

                success = self.device.set_key_gamepad_map(i, axis, direction, button)
                if success:
                    success_count += 1

            if success_count == len(self.key_gamepad_vars):
                self.status_var.set(f"✅ Gamepad mapping applied for all {success_count} keys")
            else:
                self.status_var.set(f"⚠️ Gamepad mapping: {success_count}/{len(self.key_gamepad_vars)} keys updated")
        except Exception as e:
            self.status_var.set(f"❌ Error applying gamepad mapping: {e}")

    def load_gamepad_mapping(self):
        """Load per-key gamepad mapping from device."""
        if not self.device:
            return

        try:
            for i, (axis_var, dir_var, btn_var) in enumerate(self.key_gamepad_vars):
                mapping = self.device.get_key_gamepad_map(i)
                if mapping:
                    axis_var.set(mapping.get('axis', 0))
                    dir_var.set(mapping.get('direction', 0))
                    btn_var.set(mapping.get('button', 0))
        except Exception as e:
            print(f"Error loading gamepad mapping: {e}")
