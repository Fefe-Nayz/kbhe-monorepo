from .common import *
from .theme import APP_COLORS
from .widgets import create_card, create_page_frame, create_section_row, make_status_chip, set_status_style


class CalibrationPageMixin:
    def create_calibration_widgets(self, parent):
        """Create the calibration screen."""

        _, body = create_page_frame(
            parent,
            "Calibration",
            "ADC zero points and per-key analog response curves. The curve editor follows the shared selected key from the app shell.",
        )

        if not hasattr(self, "selected_key_var"):
            self.selected_key_var = tk.IntVar(value=0)

        self.calibration_status_var = tk.StringVar(
            value="Load calibration values from the device, or auto-calibrate a key when the switches are fully released."
        )
        self.curve_status_var = tk.StringVar(
            value="Load the active key curve, then drag the red handles or edit the exact control points."
        )

        context_card = create_card(
            body,
            "Calibration Context",
            "Manual calibration values are global. The analog curve editor always targets the shared active key.",
        )
        context_row = create_section_row(context_card)
        ttk.Label(context_row, text="Active Key:", width=20).pack(side=tk.LEFT)
        self.calibration_key_var = tk.StringVar(value=f"Key {self._active_curve_key_index() + 1}")
        ttk.Label(context_row, textvariable=self.calibration_key_var, style="SectionTitle.TLabel").pack(side=tk.LEFT)
        self.calibration_status_label = make_status_chip(context_card, self.calibration_status_var)

        manual_card = create_card(
            body,
            "Manual Calibration",
            "Enter raw zero points directly, or load the current values from the device and adjust them from there.",
        )
        self.cal_lut_zero_entry = None
        self.cal_lut_zero_var = tk.StringVar(value="--")
        self.cal_key_entries = []
        self.cal_key_vars = []

        def add_manual_row(card, label, entry_attr=None):
            row = create_section_row(card)
            ttk.Label(row, text=label, width=20).pack(side=tk.LEFT)
            entry = ttk.Entry(row, width=10)
            entry.pack(side=tk.LEFT)
            current_var = tk.StringVar(value="--")
            ttk.Label(row, textvariable=current_var, style="SurfaceSubtle.TLabel").pack(side=tk.LEFT, padx=10)
            if entry_attr:
                setattr(self, entry_attr, entry)
            return entry, current_var

        self.cal_lut_zero_entry, self.cal_lut_zero_var = add_manual_row(manual_card, "LUT Zero Value:", "cal_lut_zero_entry")
        self.cal_key_entries = []
        self.cal_key_vars = []
        for index in range(6):
            entry, current_var = add_manual_row(manual_card, f"Key {index + 1} Zero Value:")
            self.cal_key_entries.append(entry)
            self.cal_key_vars.append(current_var)

        manual_actions = create_section_row(manual_card)
        ttk.Button(
            manual_actions,
            text="Load from Device",
            command=self.load_calibration,
            style="Ghost.TButton",
        ).pack(side=tk.LEFT)
        ttk.Button(
            manual_actions,
            text="Apply Manual Values",
            command=self.apply_manual_calibration,
            style="Primary.TButton",
        ).pack(side=tk.LEFT, padx=6)
        ttk.Label(
            manual_card,
            text="Values are applied live to the device. Save to Flash separately if you want them persisted.",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W, pady=(6, 0))

        auto_card = create_card(
            body,
            "Auto Calibration",
            "Use auto-calibration after fully releasing every key. You can calibrate one switch or the entire board.",
        )
        ttk.Label(
            auto_card,
            text="Make sure every key is fully released before you run auto-calibration.",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W, pady=(0, 8))

        ttk.Button(
            auto_card,
            text="Calibrate All Keys",
            command=lambda: self.auto_calibrate(0xFF),
            style="Primary.TButton",
        ).pack(anchor=tk.W)

        ttk.Label(
            auto_card,
            text="Or calibrate a single key:",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W, pady=(10, 4))

        key_grid = ttk.Frame(auto_card, style="Surface.TFrame")
        key_grid.pack(fill=tk.X)
        for index in range(6):
            ttk.Button(
                key_grid,
                text=f"Key {index + 1}",
                command=lambda idx=index: self.auto_calibrate(idx),
                style="Ghost.TButton",
                width=10,
            ).grid(row=index // 3, column=index % 3, sticky="ew", padx=4, pady=4)
            key_grid.columnconfigure(index % 3, weight=1)

        self.create_curve_builder_frame(body)

        self.load_calibration()
        self.load_key_curve()

    def _active_curve_key_index(self):
        try:
            if hasattr(self, "selected_key_var"):
                return max(0, min(5, int(self.selected_key_var.get())))
        except Exception:
            pass
        return 0

    def _sync_active_curve_key(self, key_index):
        key_index = max(0, min(5, int(key_index)))
        if hasattr(self, "selected_key_var") and int(self.selected_key_var.get()) != key_index:
            self.selected_key_var.set(key_index)
        if hasattr(self, "calibration_key_var"):
            self.calibration_key_var.set(f"Key {key_index + 1}")
        return key_index

    def _set_calibration_status(self, message, level="default"):
        if hasattr(self, "status_var"):
            self.status_var.set(message)
        if hasattr(self, "calibration_status_var"):
            self.calibration_status_var.set(message)
        if hasattr(self, "calibration_status_label"):
            set_status_style(self.calibration_status_label, level)

    def _set_curve_status(self, message, level="default"):
        if hasattr(self, "status_var"):
            self.status_var.set(message)
        if hasattr(self, "curve_status_var"):
            self.curve_status_var.set(message)
        if hasattr(self, "curve_status_label"):
            set_status_style(self.curve_status_label, level)

    def _apply_calibration_values(self, cal):
        lut_zero = int(cal.get("lut_zero_value", 0))
        key_zeros = list(cal.get("key_zero_values", []))

        self.cal_lut_zero_var.set(f"(Current: {lut_zero})")
        self.cal_lut_zero_entry.delete(0, tk.END)
        self.cal_lut_zero_entry.insert(0, str(lut_zero))

        for index, var in enumerate(self.cal_key_vars):
            if index < len(key_zeros):
                value = int(key_zeros[index])
                var.set(f"(Current: {value})")
                self.cal_key_entries[index].delete(0, tk.END)
                self.cal_key_entries[index].insert(0, str(value))
            else:
                var.set("--")

    def load_calibration(self):
        """Load calibration values from the device."""
        if not self.device:
            self._set_calibration_status("Calibration values are unavailable because no device is connected.", "warn")
            return

        try:
            cal = self.device.get_calibration()
            if cal:
                self._apply_calibration_values(cal)
                self._set_calibration_status("Loaded calibration values from the device.", "ok")
            else:
                self._set_calibration_status("No calibration values were returned by the device.", "warn")
        except Exception as exc:
            self._set_calibration_status(f"Error loading calibration: {exc}", "danger")

    def apply_manual_calibration(self):
        """Apply manually entered calibration values."""
        if not self.device:
            self._set_calibration_status("Cannot apply calibration because no device is connected.", "warn")
            return

        try:
            lut_zero = int(self.cal_lut_zero_entry.get().strip())
            key_zeros = [int(entry.get().strip()) for entry in self.cal_key_entries]
        except ValueError as exc:
            self._set_calibration_status(f"Invalid calibration value: {exc}", "danger")
            return
        except Exception as exc:
            self._set_calibration_status(f"Error reading calibration values: {exc}", "danger")
            return

        try:
            success = self.device.set_calibration(lut_zero, key_zeros)
            if success:
                self._apply_calibration_values({"lut_zero_value": lut_zero, "key_zero_values": key_zeros})
                self._set_calibration_status("Applied manual calibration values - live only, not saved.", "ok")
            else:
                self._set_calibration_status("Failed to apply calibration values.", "danger")
        except Exception as exc:
            self._set_calibration_status(f"Error applying calibration: {exc}", "danger")

    def auto_calibrate(self, key_index):
        """Auto-calibrate a key or all keys."""
        if not self.device:
            self._set_calibration_status("Cannot auto-calibrate because no device is connected.", "warn")
            return

        key_index = int(key_index)
        message = "all keys" if key_index == 0xFF else f"Key {key_index + 1}"
        try:
            self._set_calibration_status(f"Calibrating {message}...", "warn")
            self.update_idletasks()

            result = self.device.auto_calibrate(key_index)
            if result:
                self._apply_calibration_values(result)
                self._set_calibration_status(f"Calibrated {message} - live only, not saved.", "ok")
            else:
                self._set_calibration_status(f"Failed to calibrate {message}.", "danger")
        except Exception as exc:
            self._set_calibration_status(f"Error during calibration: {exc}", "danger")

    def create_curve_builder_frame(self, parent):
        """Create a 4-point bezier curve builder widget."""

        curve_card = create_card(
            parent,
            "Analog Response Curve",
            "Each key stores a cubic Bézier curve. Drag the red handles or type exact values for the active key.",
        )

        state_row = create_section_row(curve_card)
        ttk.Label(state_row, text="Active Key:", width=16).pack(side=tk.LEFT)
        self.curve_key_var = tk.StringVar(value=f"Key {self._active_curve_key_index() + 1}")
        ttk.Label(state_row, textvariable=self.curve_key_var, style="SectionTitle.TLabel").pack(
            side=tk.LEFT, padx=(0, 16)
        )
        ttk.Label(state_row, text="Curve State:", width=14).pack(side=tk.LEFT)
        self.curve_state_var = tk.StringVar(value="Disabled")
        ttk.Label(state_row, textvariable=self.curve_state_var, style="SectionTitle.TLabel").pack(side=tk.LEFT)
        self.curve_status_label = make_status_chip(curve_card, self.curve_status_var)

        action_row = create_section_row(curve_card)
        ttk.Button(action_row, text="Load Current", command=self.load_key_curve, style="Ghost.TButton").pack(
            side=tk.LEFT
        )
        ttk.Button(action_row, text="Apply Curve", command=self.apply_analog_curve, style="Primary.TButton").pack(
            side=tk.LEFT, padx=6
        )

        content = ttk.Frame(curve_card, style="Surface.TFrame")
        content.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        canvas_panel = ttk.Frame(content, style="Surface.TFrame")
        canvas_panel.pack(side=tk.LEFT, padx=(0, 12))

        self.curve_canvas = tk.Canvas(
            canvas_panel,
            width=320,
            height=320,
            bg=APP_COLORS["surface"],
            highlightthickness=1,
            highlightbackground=APP_COLORS["border"],
        )
        self.curve_canvas.pack()
        ttk.Label(
            canvas_panel,
            text="Drag the red handles to shape the curve. Input fields stay in sync.",
            style="SurfaceSubtle.TLabel",
            wraplength=320,
        ).pack(anchor=tk.W, pady=(6, 0))

        self.curve_points = [
            (0.0, 0.0),
            (0.3, 0.0),
            (0.7, 1.0),
            (1.0, 1.0),
        ]
        self.dragging_point = None

        self.curve_canvas.bind("<Button-1>", self.on_curve_click)
        self.curve_canvas.bind("<B1-Motion>", self.on_curve_drag)
        self.curve_canvas.bind("<ButtonRelease-1>", self.on_curve_release)

        control_panel = ttk.Frame(content, style="Surface.TFrame")
        control_panel.pack(side=tk.LEFT, fill=tk.Y, expand=True)

        preset_card = create_card(control_panel, "Presets", "Choose a starting shape before fine-tuning the handles.")
        preset_grid = ttk.Frame(preset_card, style="Surface.TFrame")
        preset_grid.pack(fill=tk.X)
        preset_buttons = [
            ("Linear", "linear"),
            ("Smooth", "smooth"),
            ("Aggressive", "aggressive"),
            ("Delayed", "delayed"),
        ]
        for index, (label, preset) in enumerate(preset_buttons):
            ttk.Button(
                preset_grid,
                text=label,
                command=lambda name=preset: self.set_curve_preset(name),
                style="Ghost.TButton",
            ).grid(row=index // 2, column=index % 2, sticky="ew", padx=3, pady=3)
            preset_grid.columnconfigure(index % 2, weight=1)

        manual_card = create_card(
            control_panel,
            "Manual Control Points",
            "Use exact normalized coordinates when you want reproducible results across devices.",
        )

        def add_point_row(prefix, x_attr, y_attr, x_default, y_default):
            row = create_section_row(manual_card)
            ttk.Label(row, text=f"{prefix} X:", width=8).pack(side=tk.LEFT)
            x_var = tk.DoubleVar(value=x_default)
            setattr(self, x_attr, x_var)
            x_spin = ttk.Spinbox(
                row,
                from_=0,
                to=1,
                increment=0.05,
                width=7,
                textvariable=x_var,
                command=self.update_curve_from_inputs,
            )
            x_spin.pack(side=tk.LEFT)
            ttk.Label(row, text="Y:", width=3).pack(side=tk.LEFT, padx=(6, 0))
            y_var = tk.DoubleVar(value=y_default)
            setattr(self, y_attr, y_var)
            y_spin = ttk.Spinbox(
                row,
                from_=0,
                to=1,
                increment=0.05,
                width=7,
                textvariable=y_var,
                command=self.update_curve_from_inputs,
            )
            y_spin.pack(side=tk.LEFT)

            for spinbox in (x_spin, y_spin):
                spinbox.bind("<Return>", lambda event: self.update_curve_from_inputs())
                spinbox.bind("<FocusOut>", lambda event: self.update_curve_from_inputs())

        add_point_row("P1", "curve_p1x_var", "curve_p1y_var", 0.3, 0.0)
        add_point_row("P2", "curve_p2x_var", "curve_p2y_var", 0.7, 1.0)

        ttk.Label(
            manual_card,
            text="Points are normalized from 0 to 1. Dragging the canvas handles updates these fields as well.",
            style="SurfaceSubtle.TLabel",
            wraplength=260,
        ).pack(anchor=tk.W, pady=(6, 0))

        self.draw_curve()

    def set_curve_preset(self, preset):
        """Set curve to a preset shape."""
        presets = {
            "linear": [(0, 0), (0.33, 0.33), (0.67, 0.67), (1, 1)],
            "smooth": [(0, 0), (0.4, 0.0), (0.6, 1.0), (1, 1)],
            "aggressive": [(0, 0), (0.7, 0.0), (0.3, 1.0), (1, 1)],
            "delayed": [(0, 0), (0.1, 0.0), (0.9, 1.0), (1, 1)],
        }
        if preset not in presets:
            return

        self.curve_points = list(presets[preset])
        self.curve_p1x_var.set(self.curve_points[1][0])
        self.curve_p1y_var.set(self.curve_points[1][1])
        self.curve_p2x_var.set(self.curve_points[2][0])
        self.curve_p2y_var.set(self.curve_points[2][1])
        self.draw_curve()
        self._set_curve_status(f"Loaded the {preset} preset.", "ok")

    def _clamp01(self, value):
        return max(0.0, min(1.0, float(value)))

    def _curve_canvas_bounds(self):
        w = self.curve_canvas.winfo_width() or 320
        h = self.curve_canvas.winfo_height() or 320
        margin = 30
        x0, y0 = margin, margin
        x1, y1 = max(x0 + 1, w - margin), max(y0 + 1, h - margin)
        return x0, y0, x1, y1

    def _sync_curve_input_vars(self):
        self.curve_p1x_var.set(round(self.curve_points[1][0], 2))
        self.curve_p1y_var.set(round(self.curve_points[1][1], 2))
        self.curve_p2x_var.set(round(self.curve_points[2][0], 2))
        self.curve_p2y_var.set(round(self.curve_points[2][1], 2))

    def update_curve_from_inputs(self, *args):
        """Update curve from manual input fields."""
        del args
        try:
            p1x = self._clamp01(self.curve_p1x_var.get())
            p1y = self._clamp01(self.curve_p1y_var.get())
            p2x = self._clamp01(self.curve_p2x_var.get())
            p2y = self._clamp01(self.curve_p2y_var.get())
        except (tk.TclError, ValueError):
            return

        self.curve_points[1] = (p1x, p1y)
        self.curve_points[2] = (p2x, p2y)
        self.draw_curve()

    def draw_curve(self):
        """Draw the bezier curve and control points."""
        canvas = self.curve_canvas
        canvas.delete("all")

        x0, y0, x1, y1 = self._curve_canvas_bounds()

        for index in range(5):
            t = index / 4
            gx = x0 + t * (x1 - x0)
            gy = y0 + t * (y1 - y0)
            canvas.create_line(gx, y0, gx, y1, fill="#ddd")
            canvas.create_line(x0, gy, x1, gy, fill="#ddd")

        canvas.create_line(x0, y1, x1, y1, fill="black", width=2)
        canvas.create_line(x0, y1, x0, y0, fill="black", width=2)
        canvas.create_text(x0 - 15, y1, text="0", font=("Arial", 8))
        canvas.create_text(x1, y1 + 15, text="Input", font=("Arial", 8))
        canvas.create_text(x0 - 15, y0, text="1", font=("Arial", 8))
        canvas.create_text(x0, y0 - 10, text="Output", font=("Arial", 8))

        p0, p1, p2, p3 = self.curve_points
        points = []
        for step in range(101):
            t = step / 100.0
            x = (
                (1 - t) ** 3 * p0[0]
                + 3 * (1 - t) ** 2 * t * p1[0]
                + 3 * (1 - t) * t**2 * p2[0]
                + t**3 * p3[0]
            )
            y = (
                (1 - t) ** 3 * p0[1]
                + 3 * (1 - t) ** 2 * t * p1[1]
                + 3 * (1 - t) * t**2 * p2[1]
                + t**3 * p3[1]
            )
            points.append((x0 + x * (x1 - x0), y1 - y * (y1 - y0)))

        for index in range(len(points) - 1):
            canvas.create_line(
                points[index][0],
                points[index][1],
                points[index + 1][0],
                points[index + 1][1],
                fill=APP_COLORS["accent_alt"],
                width=2,
            )

        p0_canvas = (x0 + p0[0] * (x1 - x0), y1 - p0[1] * (y1 - y0))
        p1_canvas = (x0 + p1[0] * (x1 - x0), y1 - p1[1] * (y1 - y0))
        p2_canvas = (x0 + p2[0] * (x1 - x0), y1 - p2[1] * (y1 - y0))
        p3_canvas = (x0 + p3[0] * (x1 - x0), y1 - p3[1] * (y1 - y0))

        canvas.create_line(p0_canvas[0], p0_canvas[1], p1_canvas[0], p1_canvas[1], fill=APP_COLORS["accent"], dash=(2, 2))
        canvas.create_line(p3_canvas[0], p3_canvas[1], p2_canvas[0], p2_canvas[1], fill=APP_COLORS["accent"], dash=(2, 2))

        radius = 6
        canvas.create_oval(
            p1_canvas[0] - radius,
            p1_canvas[1] - radius,
            p1_canvas[0] + radius,
            p1_canvas[1] + radius,
            fill=APP_COLORS["danger"],
            outline="#661f16",
            tags="p1",
        )
        canvas.create_oval(
            p2_canvas[0] - radius,
            p2_canvas[1] - radius,
            p2_canvas[0] + radius,
            p2_canvas[1] + radius,
            fill=APP_COLORS["danger"],
            outline="#661f16",
            tags="p2",
        )
        canvas.create_oval(
            p0_canvas[0] - 4,
            p0_canvas[1] - 4,
            p0_canvas[0] + 4,
            p0_canvas[1] + 4,
            fill=APP_COLORS["text_muted"],
            outline="black",
        )
        canvas.create_oval(
            p3_canvas[0] - 4,
            p3_canvas[1] - 4,
            p3_canvas[0] + 4,
            p3_canvas[1] + 4,
            fill=APP_COLORS["text_muted"],
            outline="black",
        )

    def on_curve_click(self, event):
        """Handle click on curve canvas."""
        x0, y0, x1, y1 = self._curve_canvas_bounds()
        for index in (1, 2):
            px = x0 + self.curve_points[index][0] * (x1 - x0)
            py = y1 - self.curve_points[index][1] * (y1 - y0)
            if abs(event.x - px) < 10 and abs(event.y - py) < 10:
                self.dragging_point = index
                return

    def on_curve_drag(self, event):
        """Handle drag on curve canvas."""
        if self.dragging_point is None:
            return

        x0, y0, x1, y1 = self._curve_canvas_bounds()
        nx = self._clamp01((event.x - x0) / (x1 - x0))
        ny = self._clamp01((y1 - event.y) / (y1 - y0))

        self.curve_points[self.dragging_point] = (nx, ny)
        if self.dragging_point == 1:
            self.curve_p1x_var.set(round(nx, 2))
            self.curve_p1y_var.set(round(ny, 2))
        elif self.dragging_point == 2:
            self.curve_p2x_var.set(round(nx, 2))
            self.curve_p2y_var.set(round(ny, 2))
        self.draw_curve()

    def on_curve_release(self, event):
        """Handle mouse release on curve canvas."""
        del event
        self.dragging_point = None

    def apply_analog_curve(self):
        """Apply the analog curve to the device for the selected key."""
        if not self.device:
            self._set_curve_status("Cannot apply the curve because no device is connected.", "warn")
            return

        try:
            key_index = self._sync_active_curve_key(self._active_curve_key_index())
            p1 = self.curve_points[1]
            p2 = self.curve_points[2]
            p1_x = int(self._clamp01(p1[0]) * 255)
            p1_y = int(self._clamp01(p1[1]) * 255)
            p2_x = int(self._clamp01(p2[0]) * 255)
            p2_y = int(self._clamp01(p2[1]) * 255)

            success = self.device.set_key_curve(key_index, True, p1_x, p1_y, p2_x, p2_y)
            if success:
                self.curve_state_var.set("Enabled")
                self._set_curve_status(
                    f"Applied curve to Key {key_index + 1}: P1({p1_x},{p1_y}) P2({p2_x},{p2_y}) - live only, not saved.",
                    "ok",
                )
            else:
                self._set_curve_status(f"Failed to apply the curve to Key {key_index + 1}.", "danger")
        except Exception as exc:
            self._set_curve_status(f"Error applying curve: {exc}", "danger")

    def load_key_curve(self, key_index=None):
        """Load curve from device for the specified key or current selection."""
        if not self.device:
            self._set_curve_status("Cannot load a curve because no device is connected.", "warn")
            return

        if key_index is None:
            key_index = self._active_curve_key_index()
        key_index = self._sync_active_curve_key(key_index)

        try:
            curve = self.device.get_key_curve(key_index)
            if curve:
                p1_x = curve["p1_x"] / 255.0
                p1_y = curve["p1_y"] / 255.0
                p2_x = curve["p2_x"] / 255.0
                p2_y = curve["p2_y"] / 255.0

                self.curve_points[1] = (p1_x, p1_y)
                self.curve_points[2] = (p2_x, p2_y)
                self._sync_curve_input_vars()
                self.curve_state_var.set("Enabled" if curve.get("curve_enabled") else "Disabled")
                self.draw_curve()
                self._set_curve_status(f"Loaded analog curve for Key {key_index + 1}.", "ok")
            else:
                self._set_curve_status(f"No analog curve was returned for Key {key_index + 1}.", "warn")
        except Exception as exc:
            self._set_curve_status(f"Error loading curve: {exc}", "danger")
