from .common import *


class GraphPageMixin:
    def create_graph_widgets(self, parent):
        """Create Live Graph tab widgets."""

        # Info banner
        ttk.Label(parent, text="📊 Live data visualization - ADC values, key states, and analog axes",
                  foreground="blue").pack(anchor=tk.W, pady=(0, 10))

        # Controls frame
        control_frame = ttk.Frame(parent)
        control_frame.pack(fill=tk.X, pady=5)

        self.graph_live_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            control_frame, text="📊 Enable Live Graph",
            variable=self.graph_live_var,
            command=self.toggle_graph_update
        ).pack(side=tk.LEFT)

        ttk.Label(control_frame, text="  Data Points:").pack(side=tk.LEFT)
        self.graph_points_var = tk.IntVar(value=200)
        ttk.Spinbox(control_frame, from_=50, to=1000, width=6,
                    textvariable=self.graph_points_var).pack(side=tk.LEFT, padx=5)

        ttk.Label(control_frame, text="  Update (ms):").pack(side=tk.LEFT)
        self.graph_update_var = tk.IntVar(value=50)
        ttk.Spinbox(control_frame, from_=10, to=500, width=5,
                    textvariable=self.graph_update_var).pack(side=tk.LEFT, padx=5)

        ttk.Button(control_frame, text="🗑️ Clear", command=self.clear_graph_data).pack(side=tk.LEFT, padx=5)

        # Data type selection
        dtype_frame = ttk.LabelFrame(parent, text="📈 Data Type", padding="5")
        dtype_frame.pack(fill=tk.X, pady=5)

        self.graph_dtype_var = tk.StringVar(value='adc')
        dtypes = [
            ('adc', 'ADC Raw (2000-2700)'),
            ('distance', 'Distance (0-4mm)'),
            ('normalized', 'Normalized (0-100%)')
        ]
        for val, text in dtypes:
            rb = ttk.Radiobutton(dtype_frame, text=text, variable=self.graph_dtype_var, 
                                 value=val, command=self.on_graph_dtype_change)
            rb.pack(side=tk.LEFT, padx=10)

        # Channel selection
        channel_frame = ttk.LabelFrame(parent, text="📡 Channels", padding="5")
        channel_frame.pack(fill=tk.X, pady=5)

        self.graph_channel_vars = []
        colors = ['#ff0000', '#00ff00', '#0000ff', '#ffff00', '#ff00ff', '#00ffff']
        for i in range(6):
            var = tk.BooleanVar(value=True)
            self.graph_channel_vars.append(var)
            cb = ttk.Checkbutton(channel_frame, text=f"Key {i+1}", variable=var)
            cb.pack(side=tk.LEFT, padx=5)
            # Color indicator
            color_label = tk.Label(channel_frame, text="●", fg=colors[i], font=('Arial', 12))
            color_label.pack(side=tk.LEFT)

        # Zoom and pan controls
        zoom_frame = ttk.LabelFrame(parent, text="🔍 View Controls", padding="5")
        zoom_frame.pack(fill=tk.X, pady=5)

        ttk.Label(zoom_frame, text="Y-Range:").pack(side=tk.LEFT)
        self.graph_ymin_var = tk.IntVar(value=2000)
        ttk.Spinbox(zoom_frame, from_=0, to=4000, width=6,
                    textvariable=self.graph_ymin_var).pack(side=tk.LEFT, padx=2)
        ttk.Label(zoom_frame, text="-").pack(side=tk.LEFT)
        self.graph_ymax_var = tk.IntVar(value=2700)
        ttk.Spinbox(zoom_frame, from_=0, to=4000, width=6,
                    textvariable=self.graph_ymax_var).pack(side=tk.LEFT, padx=2)

        ttk.Button(zoom_frame, text="Auto Y", command=self.auto_y_range).pack(side=tk.LEFT, padx=5)
        ttk.Button(zoom_frame, text="Reset View", command=self.reset_graph_view).pack(side=tk.LEFT, padx=5)

        # Graph canvas frame
        graph_frame = ttk.LabelFrame(parent, text="📈 Graph", padding="5")
        graph_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        self.graph_canvas = tk.Canvas(graph_frame, bg='#1a1a1a', height=350)
        self.graph_canvas.pack(fill=tk.BOTH, expand=True)

        # Tooltip label
        self.graph_tooltip = ttk.Label(graph_frame, text="", background='#ffffe0', 
                                       relief='solid', borderwidth=1)
        self.graph_tooltip_visible = False

        # Bind mouse events for tooltips and pan
        self.graph_canvas.bind('<Motion>', self.on_graph_mouse_move)
        self.graph_canvas.bind('<Leave>', self.on_graph_mouse_leave)
        self.graph_canvas.bind('<Button-1>', self.on_graph_click)
        self.graph_canvas.bind('<B1-Motion>', self.on_graph_drag)
        self.graph_canvas.bind('<MouseWheel>', self.on_graph_scroll)

        # Initialize data buffers
        self.graph_data = {i: [] for i in range(6)}
        self.graph_colors = colors
        self.graph_job = None
        self.graph_pan_start = None

        # Statistics labels
        stats_frame = ttk.Frame(parent)
        stats_frame.pack(fill=tk.X, pady=5)

        self.graph_stats_labels = []
        for i in range(6):
            lbl = ttk.Label(stats_frame, text=f"K{i+1}: --", font=('Consolas', 9), foreground=colors[i])
            lbl.pack(side=tk.LEFT, padx=10)
            self.graph_stats_labels.append(lbl)

        # Draw grid
        self.draw_graph_grid()

    def on_graph_dtype_change(self):
        """Handle data type change."""
        dtype = self.graph_dtype_var.get()
        if dtype == 'adc':
            self.graph_ymin_var.set(2000)
            self.graph_ymax_var.set(2700)
        elif dtype == 'distance':
            self.graph_ymin_var.set(0)
            self.graph_ymax_var.set(400)  # 0.01mm units
        else:  # normalized
            self.graph_ymin_var.set(0)
            self.graph_ymax_var.set(100)
        self.clear_graph_data()

    def auto_y_range(self):
        """Auto-adjust Y range based on data."""
        all_vals = []
        for data in self.graph_data.values():
            all_vals.extend(data)

        if all_vals:
            min_val = min(all_vals)
            max_val = max(all_vals)
            margin = (max_val - min_val) * 0.1
            self.graph_ymin_var.set(int(min_val - margin))
            self.graph_ymax_var.set(int(max_val + margin))

    def reset_graph_view(self):
        """Reset graph view to defaults."""
        self.on_graph_dtype_change()

    def on_graph_mouse_move(self, event):
        """Show tooltip on mouse hover."""
        w = self.graph_canvas.winfo_width()
        h = self.graph_canvas.winfo_height()

        if w < 10 or h < 10:
            return

        # Find closest data point
        max_points = self.graph_points_var.get()
        data_idx = int(event.x / w * max_points)

        ymin = self.graph_ymin_var.get()
        ymax = self.graph_ymax_var.get()
        y_val = ymax - (event.y / h) * (ymax - ymin)

        # Build tooltip text
        tooltip_lines = [f"Sample: {data_idx}", f"Y: {y_val:.1f}"]
        for i in range(6):
            if self.graph_channel_vars[i].get() and data_idx < len(self.graph_data[i]):
                val = self.graph_data[i][data_idx]
                tooltip_lines.append(f"Key {i+1}: {val}")

        # Show tooltip
        self.graph_tooltip.config(text="\n".join(tooltip_lines))
        self.graph_tooltip.place(x=event.x + 15, y=event.y + 15)
        self.graph_tooltip_visible = True

    def on_graph_mouse_leave(self, event):
        """Hide tooltip when mouse leaves."""
        if self.graph_tooltip_visible:
            self.graph_tooltip.place_forget()
            self.graph_tooltip_visible = False

    def on_graph_click(self, event):
        """Handle click for pan start."""
        self.graph_pan_start = (event.x, event.y)

    def on_graph_drag(self, event):
        """Handle drag for panning."""
        if self.graph_pan_start is None:
            return

        dx = event.x - self.graph_pan_start[0]
        dy = event.y - self.graph_pan_start[1]
        self.graph_pan_start = (event.x, event.y)

        # Pan Y range
        h = self.graph_canvas.winfo_height()
        ymin = self.graph_ymin_var.get()
        ymax = self.graph_ymax_var.get()
        y_range = ymax - ymin

        dy_val = int(-dy / h * y_range)
        self.graph_ymin_var.set(ymin + dy_val)
        self.graph_ymax_var.set(ymax + dy_val)

        self.draw_graph()

    def on_graph_scroll(self, event):
        """Handle mouse wheel for zoom."""
        ymin = self.graph_ymin_var.get()
        ymax = self.graph_ymax_var.get()
        y_center = (ymin + ymax) / 2
        y_range = ymax - ymin

        # Zoom in/out
        if event.delta > 0:
            zoom = 0.9  # Zoom in
        else:
            zoom = 1.1  # Zoom out

        new_range = y_range * zoom
        self.graph_ymin_var.set(int(y_center - new_range / 2))
        self.graph_ymax_var.set(int(y_center + new_range / 2))

        self.draw_graph_grid()
        self.draw_graph()

    def draw_graph_grid(self):
        """Draw graph grid lines."""
        if not hasattr(self, 'graph_canvas'):
            return
        self.graph_canvas.delete("grid")
        w = self.graph_canvas.winfo_width()
        h = self.graph_canvas.winfo_height()

        if w < 10 or h < 10:
            # Widget not yet sized
            self.after(100, self.draw_graph_grid)
            return

        ymin = self.graph_ymin_var.get()
        ymax = self.graph_ymax_var.get()
        y_range = ymax - ymin

        # Calculate grid step
        step = max(1, y_range // 10)
        # Round to nice values
        if step >= 100:
            step = (step // 100) * 100
        elif step >= 10:
            step = (step // 10) * 10

        # Horizontal grid lines
        val = (ymin // step) * step
        while val <= ymax:
            y = h - ((val - ymin) / y_range * h)
            if 0 <= y <= h:
                self.graph_canvas.create_line(0, y, w, y, fill='#333333', tags="grid")
                self.graph_canvas.create_text(5, y, text=str(val), anchor=tk.W, 
                                               fill='#666666', font=('Consolas', 8), tags="grid")
            val += step

    def toggle_graph_update(self):
        """Toggle live graph updates."""
        if self.graph_live_var.get():
            if self.active_tab == "Live Graph":
                self._start_graph_updates()
            else:
                self.status_var.set("Graph polling armed. Open the Live Graph tab to start updates.")
        else:
            self._stop_graph_updates()

    def update_graph(self):
        """Update graph with new data."""
        if not self.graph_live_var.get():
            self.graph_job = None
            return

        try:
            dtype = self.graph_dtype_var.get()
            max_points = self.graph_points_var.get()

            if dtype == 'adc':
                # Get raw ADC data
                adc_data = self.device.get_adc_values()
                if adc_data and 'adc' in adc_data:
                    for i, val in enumerate(adc_data['adc']):
                        self.graph_data[i].append(val)
                        if len(self.graph_data[i]) > max_points:
                            self.graph_data[i] = self.graph_data[i][-max_points:]
            elif dtype == 'distance':
                # Get distance in 0.01mm units directly from MCU
                key_states = self.device.get_key_states()
                if key_states and 'distances_01mm' in key_states:
                    for i, val in enumerate(key_states['distances_01mm']):
                        self.graph_data[i].append(val)
                        if len(self.graph_data[i]) > max_points:
                            self.graph_data[i] = self.graph_data[i][-max_points:]
            else:  # normalized
                key_states = self.device.get_key_states()
                if key_states and 'distances' in key_states:
                    for i, dist_norm in enumerate(key_states['distances']):
                        val = int(dist_norm * 100 / 255)  # 0-255 to 0-100%
                        self.graph_data[i].append(val)
                        if len(self.graph_data[i]) > max_points:
                            self.graph_data[i] = self.graph_data[i][-max_points:]

            # Update statistics
            self.update_graph_stats()
            self.draw_graph()
        except Exception as e:
            pass  # Silently ignore errors during graph update

        # Schedule next update
        update_ms = self.graph_update_var.get()
        if self.graph_live_var.get() and self.active_tab == "Live Graph":
            self.graph_job = self.after(update_ms, self.update_graph)
        else:
            self.graph_job = None

    def update_graph_stats(self):
        """Update graph statistics labels."""
        for i, label in enumerate(self.graph_stats_labels):
            data = self.graph_data[i]
            if data:
                current = data[-1]
                avg = sum(data) / len(data)
                label.config(text=f"K{i+1}: {current} (avg:{avg:.0f})")
            else:
                label.config(text=f"K{i+1}: --")

    def draw_graph(self):
        """Draw the graph lines."""
        self.graph_canvas.delete("data")
        w = self.graph_canvas.winfo_width()
        h = self.graph_canvas.winfo_height()

        if w < 10 or h < 10:
            return

        ymin = self.graph_ymin_var.get()
        ymax = self.graph_ymax_var.get()
        y_range = max(1, ymax - ymin)

        for i in range(6):
            if not self.graph_channel_vars[i].get():
                continue

            data = self.graph_data[i]
            if len(data) < 2:
                continue

            points = []
            for j, val in enumerate(data):
                x = j / len(data) * w
                # Map value to canvas height using dynamic range
                y = h - ((val - ymin) / y_range * h)
                y = max(0, min(h, y))  # Clamp
                points.append((x, y))

            # Draw line
            for j in range(len(points) - 1):
                self.graph_canvas.create_line(
                    points[j][0], points[j][1],
                    points[j+1][0], points[j+1][1],
                    fill=self.graph_colors[i], width=1, tags="data"
                )

    def clear_graph_data(self):
        """Clear graph data."""
        self.graph_data = {i: [] for i in range(6)}
        if hasattr(self, 'graph_canvas'):
            self.graph_canvas.delete("data")
        if hasattr(self, 'status_var'):
            self.status_var.set("🗑️ Graph data cleared")
