from .common import *


class CalibrationPageMixin:
    def create_calibration_widgets(self, parent):
        """Create Calibration tab widgets."""

        # Info banner
        ttk.Label(parent, text="🔧 ADC Calibration - Calibrate sensor zero points",
                  foreground="blue").pack(anchor=tk.W, pady=(0, 10))
        ttk.Label(
            parent,
            text="The analog curve editor uses the currently selected key from the Keyboard tab.",
            foreground="gray",
        ).pack(anchor=tk.W, pady=(0, 8))

        # Manual calibration values entry
        manual_frame = ttk.LabelFrame(parent, text="📝 Manual Calibration Values", padding="10")
        manual_frame.pack(fill=tk.X, pady=5)

        # LUT Zero Value
        lut_frame = ttk.Frame(manual_frame)
        lut_frame.pack(fill=tk.X, pady=2)
        ttk.Label(lut_frame, text="LUT Zero Value:", width=20).pack(side=tk.LEFT)
        self.cal_lut_zero_entry = ttk.Entry(lut_frame, width=10)
        self.cal_lut_zero_entry.pack(side=tk.LEFT)
        self.cal_lut_zero_var = tk.StringVar(value="--")
        ttk.Label(lut_frame, textvariable=self.cal_lut_zero_var, font=('Consolas', 9), foreground='gray').pack(side=tk.LEFT, padx=10)

        # Key Zero Values
        self.cal_key_entries = []
        self.cal_key_vars = []
        for i in range(6):
            key_frame = ttk.Frame(manual_frame)
            key_frame.pack(fill=tk.X, pady=2)
            ttk.Label(key_frame, text=f"Key {i+1} Zero Value:", width=20).pack(side=tk.LEFT)
            entry = ttk.Entry(key_frame, width=10)
            entry.pack(side=tk.LEFT)
            self.cal_key_entries.append(entry)
            var = tk.StringVar(value="--")
            self.cal_key_vars.append(var)
            ttk.Label(key_frame, textvariable=var, font=('Consolas', 9), foreground='gray').pack(side=tk.LEFT, padx=10)

        # Manual buttons
        manual_btn_frame = ttk.Frame(manual_frame)
        manual_btn_frame.pack(fill=tk.X, pady=5)
        ttk.Button(manual_btn_frame, text="📥 Load from Device", 
                   command=self.load_calibration).pack(side=tk.LEFT, padx=5)
        ttk.Button(manual_btn_frame, text="📤 Apply Manual Values", 
                   command=self.apply_manual_calibration).pack(side=tk.LEFT, padx=5)

        # Auto-calibration
        auto_frame = ttk.LabelFrame(parent, text="🤖 Auto-Calibration", padding="10")
        auto_frame.pack(fill=tk.X, pady=5)

        ttk.Label(auto_frame, text="⚠️ Make sure all keys are fully released before calibrating!",
                  foreground="orange").pack(anchor=tk.W, pady=5)

        btn_frame = ttk.Frame(auto_frame)
        btn_frame.pack(fill=tk.X, pady=5)

        ttk.Button(btn_frame, text="🔧 Calibrate All Keys", 
                   command=lambda: self.auto_calibrate(0xFF)).pack(side=tk.LEFT, padx=5)

        # Individual key calibration
        ttk.Label(auto_frame, text="Calibrate individual key:").pack(anchor=tk.W, pady=(10, 5))

        key_btn_frame = ttk.Frame(auto_frame)
        key_btn_frame.pack(fill=tk.X)

        for i in range(6):
            ttk.Button(key_btn_frame, text=f"Key {i+1}", width=8,
                       command=lambda idx=i: self.auto_calibrate(idx)).pack(side=tk.LEFT, padx=2)

        # Analog Curve Builder
        self.create_curve_builder_frame(parent)

        # Load initial
        self.load_calibration()
        self.load_key_curve()

    def load_calibration(self):
        """Load calibration values from device."""
        try:
            cal = self.device.get_calibration()
            if cal:
                lut_zero = cal.get('lut_zero_value', 0)
                self.cal_lut_zero_var.set(f"(Current: {lut_zero})")
                self.cal_lut_zero_entry.delete(0, tk.END)
                self.cal_lut_zero_entry.insert(0, str(lut_zero))

                key_zeros = cal.get('key_zero_values', [])
                for i, var in enumerate(self.cal_key_vars):
                    if i < len(key_zeros):
                        var.set(f"(Current: {key_zeros[i]})")
                        self.cal_key_entries[i].delete(0, tk.END)
                        self.cal_key_entries[i].insert(0, str(key_zeros[i]))
                    else:
                        var.set("--")
                self.status_var.set("🔄 Loaded calibration values")
        except Exception as e:
            self.status_var.set(f"❌ Error loading calibration: {e}")

    def apply_manual_calibration(self):
        """Apply manually entered calibration values."""
        try:
            lut_zero = int(self.cal_lut_zero_entry.get())
            key_zeros = []
            for entry in self.cal_key_entries:
                key_zeros.append(int(entry.get()))

            success = self.device.set_calibration(lut_zero, key_zeros)
            if success:
                self.load_calibration()  # Refresh display
                self.status_var.set("✅ Manual calibration values applied - LIVE (not saved)")
            else:
                self.status_var.set("❌ Failed to apply calibration values")
        except ValueError as e:
            self.status_var.set(f"❌ Invalid value: {e}")
        except Exception as e:
            self.status_var.set(f"❌ Error applying calibration: {e}")
        except Exception as e:
            self.status_var.set(f"❌ Error loading calibration: {e}")

    def auto_calibrate(self, key_index):
        """Auto-calibrate a key or all keys."""
        try:
            if key_index == 0xFF:
                msg = "all keys"
            else:
                msg = f"Key {key_index + 1}"

            self.status_var.set(f"🔧 Calibrating {msg}...")
            self.update()

            result = self.device.auto_calibrate(key_index)
            if result:
                self.load_calibration()
                self.status_var.set(f"✅ Calibrated {msg} - LIVE (not saved)")
            else:
                self.status_var.set(f"❌ Failed to calibrate {msg}")
        except Exception as e:
            self.status_var.set(f"❌ Error during calibration: {e}")

    def create_curve_builder_frame(self, parent):
        """Create a 4-point bezier curve builder widget."""

        # Main frame for curve builder
        curve_frame = ttk.LabelFrame(parent, text="📈 Analog Response Curve", padding="10")
        curve_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        # Canvas for drawing the curve
        self.curve_canvas = tk.Canvas(curve_frame, width=300, height=300, bg='white', 
                                      highlightthickness=1, highlightbackground='gray')
        self.curve_canvas.pack(side=tk.LEFT, padx=5)

        # Control points (normalized 0-1)
        # P0 = start (0,0), P3 = end (1,1), P1 and P2 are control points
        self.curve_points = [
            (0.0, 0.0),   # P0 - start (fixed)
            (0.3, 0.0),   # P1 - control point 1
            (0.7, 1.0),   # P2 - control point 2
            (1.0, 1.0)    # P3 - end (fixed)
        ]

        # Draw handles tracking
        self.dragging_point = None

        # Bind mouse events
        self.curve_canvas.bind('<Button-1>', self.on_curve_click)
        self.curve_canvas.bind('<B1-Motion>', self.on_curve_drag)
        self.curve_canvas.bind('<ButtonRelease-1>', self.on_curve_release)

        # Control panel
        control_panel = ttk.Frame(curve_frame)
        control_panel.pack(side=tk.LEFT, fill=tk.Y, padx=10)

        ttk.Label(control_panel, text="Control Points:", font=('Arial', 10, 'bold')).pack(anchor=tk.W)

        # Preset curves
        preset_frame = ttk.LabelFrame(control_panel, text="Presets", padding="5")
        preset_frame.pack(fill=tk.X, pady=5)

        ttk.Button(preset_frame, text="Linear", 
                   command=lambda: self.set_curve_preset('linear')).pack(fill=tk.X, pady=1)
        ttk.Button(preset_frame, text="Smooth", 
                   command=lambda: self.set_curve_preset('smooth')).pack(fill=tk.X, pady=1)
        ttk.Button(preset_frame, text="Aggressive", 
                   command=lambda: self.set_curve_preset('aggressive')).pack(fill=tk.X, pady=1)
        ttk.Button(preset_frame, text="Delayed", 
                   command=lambda: self.set_curve_preset('delayed')).pack(fill=tk.X, pady=1)

        # Manual P1/P2 entry
        manual_frame = ttk.LabelFrame(control_panel, text="Manual Entry", padding="5")
        manual_frame.pack(fill=tk.X, pady=5)

        # P1
        p1_frame = ttk.Frame(manual_frame)
        p1_frame.pack(fill=tk.X, pady=2)
        ttk.Label(p1_frame, text="P1 X:", width=6).pack(side=tk.LEFT)
        self.curve_p1x_var = tk.DoubleVar(value=0.3)
        ttk.Spinbox(p1_frame, from_=0, to=1, increment=0.05, width=6, 
                    textvariable=self.curve_p1x_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)
        ttk.Label(p1_frame, text="Y:", width=3).pack(side=tk.LEFT)
        self.curve_p1y_var = tk.DoubleVar(value=0.0)
        ttk.Spinbox(p1_frame, from_=0, to=1, increment=0.05, width=6, 
                    textvariable=self.curve_p1y_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)

        # P2
        p2_frame = ttk.Frame(manual_frame)
        p2_frame.pack(fill=tk.X, pady=2)
        ttk.Label(p2_frame, text="P2 X:", width=6).pack(side=tk.LEFT)
        self.curve_p2x_var = tk.DoubleVar(value=0.7)
        ttk.Spinbox(p2_frame, from_=0, to=1, increment=0.05, width=6, 
                    textvariable=self.curve_p2x_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)
        ttk.Label(p2_frame, text="Y:", width=3).pack(side=tk.LEFT)
        self.curve_p2y_var = tk.DoubleVar(value=1.0)
        ttk.Spinbox(p2_frame, from_=0, to=1, increment=0.05, width=6, 
                    textvariable=self.curve_p2y_var, command=self.update_curve_from_inputs).pack(side=tk.LEFT)

        # Apply button
        ttk.Button(control_panel, text="📤 Apply Curve to Device", 
                   command=self.apply_analog_curve).pack(fill=tk.X, pady=10)

        ttk.Label(control_panel, text="💡 Drag the red dots to\nadjust the curve shape", 
                  foreground="gray", font=('Arial', 8)).pack(anchor=tk.W)

        # Draw initial curve
        self.draw_curve()

    def set_curve_preset(self, preset):
        """Set curve to a preset shape."""
        presets = {
            'linear': [(0, 0), (0.33, 0.33), (0.67, 0.67), (1, 1)],
            'smooth': [(0, 0), (0.4, 0.0), (0.6, 1.0), (1, 1)],
            'aggressive': [(0, 0), (0.7, 0.0), (0.3, 1.0), (1, 1)],
            'delayed': [(0, 0), (0.1, 0.0), (0.9, 1.0), (1, 1)]
        }
        if preset in presets:
            self.curve_points = list(presets[preset])
            self.curve_p1x_var.set(self.curve_points[1][0])
            self.curve_p1y_var.set(self.curve_points[1][1])
            self.curve_p2x_var.set(self.curve_points[2][0])
            self.curve_p2y_var.set(self.curve_points[2][1])
            self.draw_curve()

    def update_curve_from_inputs(self, *args):
        """Update curve from manual input fields."""
        try:
            p1x = self.curve_p1x_var.get()
            p1y = self.curve_p1y_var.get()
            p2x = self.curve_p2x_var.get()
            p2y = self.curve_p2y_var.get()
            self.curve_points[1] = (max(0, min(1, p1x)), max(0, min(1, p1y)))
            self.curve_points[2] = (max(0, min(1, p2x)), max(0, min(1, p2y)))
            self.draw_curve()
        except:
            pass

    def draw_curve(self):
        """Draw the bezier curve and control points."""
        canvas = self.curve_canvas
        canvas.delete('all')

        w = canvas.winfo_width() or 300
        h = canvas.winfo_height() or 300
        margin = 30

        # Drawing area
        x0, y0 = margin, margin
        x1, y1 = w - margin, h - margin

        # Draw grid
        for i in range(5):
            t = i / 4
            gx = x0 + t * (x1 - x0)
            gy = y0 + t * (y1 - y0)
            canvas.create_line(gx, y0, gx, y1, fill='#ddd')
            canvas.create_line(x0, gy, x1, gy, fill='#ddd')

        # Draw axes
        canvas.create_line(x0, y1, x1, y1, fill='black', width=2)  # X axis
        canvas.create_line(x0, y1, x0, y0, fill='black', width=2)  # Y axis

        # Labels
        canvas.create_text(x0 - 15, y1, text="0", font=('Arial', 8))
        canvas.create_text(x1, y1 + 15, text="Input", font=('Arial', 8))
        canvas.create_text(x0 - 15, y0, text="1", font=('Arial', 8))
        canvas.create_text(x0, y0 - 10, text="Output", font=('Arial', 8))

        # Draw bezier curve
        points = []
        for t in range(101):
            t_norm = t / 100.0
            # Cubic bezier formula
            p0, p1, p2, p3 = self.curve_points
            x = ((1-t_norm)**3 * p0[0] + 
                 3*(1-t_norm)**2*t_norm * p1[0] + 
                 3*(1-t_norm)*t_norm**2 * p2[0] + 
                 t_norm**3 * p3[0])
            y = ((1-t_norm)**3 * p0[1] + 
                 3*(1-t_norm)**2*t_norm * p1[1] + 
                 3*(1-t_norm)*t_norm**2 * p2[1] + 
                 t_norm**3 * p3[1])
            # Convert to canvas coords (y inverted)
            cx = x0 + x * (x1 - x0)
            cy = y1 - y * (y1 - y0)
            points.append((cx, cy))

        # Draw curve line
        for i in range(len(points) - 1):
            canvas.create_line(points[i][0], points[i][1], 
                               points[i+1][0], points[i+1][1], 
                               fill='blue', width=2)

        # Draw control point lines
        p0_canvas = (x0 + self.curve_points[0][0] * (x1 - x0), 
                     y1 - self.curve_points[0][1] * (y1 - y0))
        p1_canvas = (x0 + self.curve_points[1][0] * (x1 - x0), 
                     y1 - self.curve_points[1][1] * (y1 - y0))
        p2_canvas = (x0 + self.curve_points[2][0] * (x1 - x0), 
                     y1 - self.curve_points[2][1] * (y1 - y0))
        p3_canvas = (x0 + self.curve_points[3][0] * (x1 - x0), 
                     y1 - self.curve_points[3][1] * (y1 - y0))

        canvas.create_line(p0_canvas[0], p0_canvas[1], p1_canvas[0], p1_canvas[1], 
                           fill='red', dash=(2, 2))
        canvas.create_line(p3_canvas[0], p3_canvas[1], p2_canvas[0], p2_canvas[1], 
                           fill='red', dash=(2, 2))

        # Draw control points
        r = 6
        # P1 (draggable)
        canvas.create_oval(p1_canvas[0]-r, p1_canvas[1]-r, 
                           p1_canvas[0]+r, p1_canvas[1]+r, 
                           fill='red', outline='darkred', tags='p1')
        # P2 (draggable)
        canvas.create_oval(p2_canvas[0]-r, p2_canvas[1]-r, 
                           p2_canvas[0]+r, p2_canvas[1]+r, 
                           fill='red', outline='darkred', tags='p2')
        # P0 and P3 (fixed)
        canvas.create_oval(p0_canvas[0]-4, p0_canvas[1]-4, 
                           p0_canvas[0]+4, p0_canvas[1]+4, 
                           fill='gray', outline='black')
        canvas.create_oval(p3_canvas[0]-4, p3_canvas[1]-4, 
                           p3_canvas[0]+4, p3_canvas[1]+4, 
                           fill='gray', outline='black')

    def on_curve_click(self, event):
        """Handle click on curve canvas."""
        w = self.curve_canvas.winfo_width()
        h = self.curve_canvas.winfo_height()
        margin = 30
        x0, y0 = margin, margin
        x1, y1 = w - margin, h - margin

        # Check if clicking on P1 or P2
        for i, pt_tag in [(1, 'p1'), (2, 'p2')]:
            px = x0 + self.curve_points[i][0] * (x1 - x0)
            py = y1 - self.curve_points[i][1] * (y1 - y0)
            if abs(event.x - px) < 10 and abs(event.y - py) < 10:
                self.dragging_point = i
                return

    def on_curve_drag(self, event):
        """Handle drag on curve canvas."""
        if self.dragging_point is None:
            return

        w = self.curve_canvas.winfo_width()
        h = self.curve_canvas.winfo_height()
        margin = 30
        x0, y0 = margin, margin
        x1, y1 = w - margin, h - margin

        # Convert canvas coords to normalized coords
        nx = (event.x - x0) / (x1 - x0)
        ny = (y1 - event.y) / (y1 - y0)

        # Clamp to 0-1
        nx = max(0, min(1, nx))
        ny = max(0, min(1, ny))

        self.curve_points[self.dragging_point] = (nx, ny)

        # Update input fields
        if self.dragging_point == 1:
            self.curve_p1x_var.set(round(nx, 2))
            self.curve_p1y_var.set(round(ny, 2))
        elif self.dragging_point == 2:
            self.curve_p2x_var.set(round(nx, 2))
            self.curve_p2y_var.set(round(ny, 2))

        self.draw_curve()

    def on_curve_release(self, event):
        """Handle mouse release on curve canvas."""
        self.dragging_point = None

    def apply_analog_curve(self):
        """Apply the analog curve to the device for the selected key."""
        if not self.device:
            self.status_var.set("❌ Not connected to device")
            return

        try:
            key_index = self._get_curve_key_index()

            # Convert normalized curve points to 0-255 range
            p1 = self.curve_points[1]
            p2 = self.curve_points[2]
            p1_x = int(p1[0] * 255)
            p1_y = int(p1[1] * 255)
            p2_x = int(p2[0] * 255)
            p2_y = int(p2[1] * 255)

            # Enable curve and send to device
            success = self.device.set_key_curve(key_index, True, p1_x, p1_y, p2_x, p2_y)

            if success:
                self.status_var.set(f"✅ Applied curve to Key {key_index + 1}: P1({p1_x},{p1_y}) P2({p2_x},{p2_y})")
            else:
                self.status_var.set(f"❌ Failed to apply curve to Key {key_index + 1}")
        except Exception as e:
            self.status_var.set(f"❌ Error applying curve: {e}")

    def _get_curve_key_index(self):
        """Return the active key index used by the curve editor."""
        if hasattr(self, "selected_key_var"):
            try:
                return int(self.selected_key_var.get())
            except Exception:
                pass
        return 0

    def load_key_curve(self, key_index=None):
        """Load curve from device for the specified key or current selection."""
        if not self.device:
            return

        if key_index is None:
            key_index = self._get_curve_key_index()

        try:
            curve = self.device.get_key_curve(key_index)
            if curve:
                # Convert 0-255 to normalized 0-1
                p1_x = curve['p1_x'] / 255.0
                p1_y = curve['p1_y'] / 255.0
                p2_x = curve['p2_x'] / 255.0
                p2_y = curve['p2_y'] / 255.0

                self.curve_points[1] = (p1_x, p1_y)
                self.curve_points[2] = (p2_x, p2_y)

                # Update input fields
                self.curve_p1x_var.set(round(p1_x, 2))
                self.curve_p1y_var.set(round(p1_y, 2))
                self.curve_p2x_var.set(round(p2_x, 2))
                self.curve_p2y_var.set(round(p2_y, 2))

                self.draw_curve()
                self.status_var.set(f"📈 Loaded analog curve for Key {key_index + 1}")
        except Exception as e:
            self.status_var.set(f"❌ Error loading curve: {e}")
