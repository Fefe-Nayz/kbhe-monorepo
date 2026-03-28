from .common import *
from .theme import APP_COLORS


class LightingPageMixin:
    def _set_lighting_status(self, message):
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

    def _set_preview_color(self, widget, rgb):
        if widget is not None:
            widget.config(bg=self._rgb_to_hex(rgb))

    def _set_current_color(self, rgb):
        self.current_color = [self._clamp_channel(channel) for channel in rgb]
        self._set_preview_color(self.color_preview, self.current_color)

    def _serialize_pixels(self):
        payload = []
        for pixel in getattr(self, "pixels", []):
            if not pixel:
                payload.extend([0, 0, 0])
                continue
            channels = list(pixel[:3]) + [0, 0, 0]
            payload.extend(self._clamp_channel(channel) for channel in channels[:3])
        return payload[:192]

    def _apply_pixel_blob(self, blob):
        pixel_count = min(64, len(blob) // 3)
        for index in range(pixel_count):
            base = index * 3
            self.pixels[index] = [blob[base], blob[base + 1], blob[base + 2]]
        return pixel_count

    def create_led_widgets(self, parent):
        """Create LED Matrix tab widgets."""
        self.current_color = getattr(self, "current_color", [255, 0, 0])
        self.pixels = getattr(self, "pixels", [[0, 0, 0] for _ in range(64)])
        self._lighting_brightness_job = None

        intro_frame = ttk.LabelFrame(parent, text="Lighting Overview", padding=10, style="Card.TLabelframe")
        intro_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            intro_frame,
            text="Live edits are sent immediately. Use Save to Flash only when you want the current lighting state to survive a power cycle.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)

        main_container = ttk.Frame(parent, style="Surface.TFrame")
        main_container.pack(fill=tk.BOTH, expand=True)
        main_container.columnconfigure(0, weight=1)
        main_container.columnconfigure(1, weight=0)

        left_frame = ttk.Frame(main_container, style="Surface.TFrame")
        left_frame.grid(row=0, column=0, sticky="nsew")

        grid_card = ttk.LabelFrame(left_frame, text="LED Matrix (8x8)", padding=10, style="Card.TLabelframe")
        grid_card.pack(fill=tk.BOTH, expand=True, pady=(0, 10))
        ttk.Label(
            grid_card,
            text="Click a cell to paint it with the selected color.",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W, pady=(0, 8))

        grid_frame = ttk.Frame(grid_card, style="Surface.TFrame")
        grid_frame.pack(fill=tk.BOTH, expand=True)

        self.led_buttons = []
        for y in range(8):
            row_buttons = []
            for x in range(8):
                index = y * 8 + x
                button = tk.Button(
                    grid_frame,
                    width=4,
                    height=2,
                    bg="#000000",
                    activebackground="#1f1f1f",
                    relief=tk.FLAT,
                    bd=0,
                    highlightthickness=1,
                    highlightbackground=APP_COLORS["border"],
                    highlightcolor=APP_COLORS["accent"],
                    cursor="hand2",
                    command=lambda idx=index: self.on_led_click(idx),
                )
                button.grid(row=y, column=x, padx=1, pady=1, sticky="nsew")
                row_buttons.append(button)
            self.led_buttons.append(row_buttons)

        for i in range(8):
            grid_frame.columnconfigure(i, weight=1)
            grid_frame.rowconfigure(i, weight=1)

        right_frame = ttk.Frame(main_container, width=320, style="Surface.TFrame")
        right_frame.grid(row=0, column=1, sticky="nsew", padx=(12, 0))
        right_frame.pack_propagate(False)

        color_card = ttk.LabelFrame(right_frame, text="Paint Color", padding=10, style="Card.TLabelframe")
        color_card.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            color_card,
            text="This color is used when you paint individual cells or fill the matrix.",
            style="SurfaceSubtle.TLabel",
            wraplength=280,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)

        self.color_preview = tk.Canvas(
            color_card,
            width=260,
            height=42,
            bg=self._rgb_to_hex(self.current_color),
            bd=0,
            highlightthickness=1,
            highlightbackground=APP_COLORS["border"],
        )
        self.color_preview.pack(fill=tk.X, pady=8)

        ttk.Button(color_card, text="🎨 Pick Color...", command=self.pick_color, style="Ghost.TButton").pack(fill=tk.X)

        quick_frame = ttk.Frame(color_card, style="Surface.TFrame")
        quick_frame.pack(fill=tk.X, pady=(8, 0))
        quick_colors = [
            ("#ff0000", "R"),
            ("#00ff00", "G"),
            ("#0000ff", "B"),
            ("#ffffff", "W"),
            ("#ffff00", "Y"),
            ("#ff00ff", "M"),
            ("#00ffff", "C"),
            ("#000000", "X"),
            ("#ff8000", "O"),
            ("#80ff00", "L"),
            ("#0080ff", "S"),
            ("#8000ff", "P"),
        ]

        for i, (color, label) in enumerate(quick_colors):
            button = tk.Button(
                quick_frame,
                bg=color,
                width=3,
                height=1,
                text=label,
                fg="white" if color != "#ffffff" else "black",
                activebackground=color,
                relief=tk.FLAT,
                bd=0,
                highlightthickness=1,
                highlightbackground=APP_COLORS["border"],
                cursor="hand2",
                command=lambda c=color: self.set_color_hex(c),
            )
            button.grid(row=i // 4, column=i % 4, padx=2, pady=2, sticky="nsew")

        for i in range(4):
            quick_frame.columnconfigure(i, weight=1)

        brightness_card = ttk.LabelFrame(right_frame, text="Brightness", padding=10, style="Card.TLabelframe")
        brightness_card.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            brightness_card,
            text="Brightness changes are applied live to the device.",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W)

        self.brightness_var = tk.IntVar(value=50)
        self.brightness_slider = ttk.Scale(
            brightness_card,
            from_=0,
            to=255,
            variable=self.brightness_var,
            orient=tk.HORIZONTAL,
            command=self.on_brightness_change,
        )
        self.brightness_slider.pack(fill=tk.X, pady=(8, 4))

        self.brightness_label = ttk.Label(brightness_card, text="50", font=("Segoe UI Semibold", 14, "bold"))
        self.brightness_label.pack(anchor=tk.CENTER)

        actions_card = ttk.LabelFrame(right_frame, text="Live Actions", padding=10, style="Card.TLabelframe")
        actions_card.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            actions_card,
            text="These operations update the keyboard immediately.",
            style="SurfaceSubtle.TLabel",
        ).pack(anchor=tk.W)

        ttk.Button(actions_card, text="🎨 Fill with Color", command=self.fill_color, style="Primary.TButton").pack(fill=tk.X, pady=(8, 4))
        ttk.Button(actions_card, text="🗑️ Clear All", command=self.clear_all, style="Ghost.TButton").pack(fill=tk.X, pady=4)
        ttk.Button(actions_card, text="🌈 Rainbow Test", command=self.rainbow_test, style="Ghost.TButton").pack(fill=tk.X, pady=4)
        ttk.Button(actions_card, text="🔄 Reload from Device", command=self.load_from_device, style="Ghost.TButton").pack(fill=tk.X, pady=(4, 0))

        save_card = ttk.LabelFrame(right_frame, text="Persistence", padding=10, style="Card.TLabelframe")
        save_card.pack(fill=tk.X)
        ttk.Label(
            save_card,
            text="Use flash saving to keep the live state after reboot.",
            style="SurfaceSubtle.TLabel",
            wraplength=280,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)

        ttk.Button(save_card, text="💾 Save to Flash", command=self.save_to_device, style="Primary.TButton").pack(fill=tk.X, pady=(8, 6))

        ttk.Separator(save_card, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=6)

        file_row = ttk.Frame(save_card, style="Surface.TFrame")
        file_row.pack(fill=tk.X)
        ttk.Button(file_row, text="📁 Export", command=self.export_to_file, style="Ghost.TButton").pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 4))
        ttk.Button(file_row, text="📂 Import", command=self.import_from_file, style="Ghost.TButton").pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(4, 0))

        self.update_led_display()

    def on_led_click(self, index):
        """Handle LED button click - sends immediately to device."""
        if index < 0 or index >= len(self.pixels):
            return

        r, g, b = self.current_color
        previous_pixel = list(self.pixels[index])
        self.pixels[index] = [r, g, b]
        self.update_led_display(index)

        try:
            self.device.led_set_pixel(index, r, g, b)
        except Exception as exc:
            self.pixels[index] = previous_pixel
            self.update_led_display(index)
            self._set_lighting_status(f"❌ Failed to update LED {index + 1}: {exc}")
            return

        self._set_lighting_status(f"✏️ LED {index + 1} set to RGB({r}, {g}, {b}) - LIVE (not saved)")

    def update_led_display(self, index=None):
        """Update LED button colors."""
        if not getattr(self, "led_buttons", None):
            return

        if index is not None:
            if 0 <= index < len(self.pixels):
                y, x = divmod(index, 8)
                self.led_buttons[y][x].config(bg=self._rgb_to_hex(self.pixels[index]))
            return

        for idx, pixel in enumerate(self.pixels[:64]):
            y, x = divmod(idx, 8)
            self.led_buttons[y][x].config(bg=self._rgb_to_hex(pixel))

    def pick_color(self):
        """Open color picker dialog."""
        color = colorchooser.askcolor(
            initialcolor=self._rgb_to_hex(self.current_color),
            title="Choose LED Color",
        )
        if color and color[0]:
            self._set_current_color([int(c) for c in color[0]])
            self._set_lighting_status(
                f"🎨 Paint color set to RGB({self.current_color[0]}, {self.current_color[1]}, {self.current_color[2]})"
            )

    def set_color_hex(self, hex_color):
        """Set current color from hex string."""
        try:
            hex_color = hex_color.lstrip("#")
            self._set_current_color([int(hex_color[i:i + 2], 16) for i in (0, 2, 4)])
            self._set_lighting_status(
                f"🎨 Paint color set to #{hex_color.upper()}"
            )
        except ValueError as exc:
            self._set_lighting_status(f"❌ Invalid color value: {exc}")

    def on_brightness_change(self, value):
        """Handle brightness slider change with a short debounce."""
        brightness = self._clamp_channel(float(value))
        self.brightness_label.config(text=str(brightness))
        if self._lighting_brightness_job is not None:
            self.after_cancel(self._lighting_brightness_job)
        self._lighting_brightness_job = self.after(120, self._commit_brightness_change)

    def _commit_brightness_change(self):
        self._lighting_brightness_job = None
        brightness = self._clamp_channel(self.brightness_var.get())

        try:
            self.device.led_set_brightness(brightness)
        except Exception as exc:
            self._set_lighting_status(f"❌ Failed to set brightness: {exc}")
            return

        self._set_lighting_status(f"💡 Brightness set to {brightness} - LIVE (not saved)")

    def clear_all(self):
        """Clear all LEDs - sends immediately."""
        previous_pixels = [pixel[:] for pixel in self.pixels]
        self.pixels = [[0, 0, 0] for _ in range(64)]
        self.update_led_display()

        try:
            self.device.led_clear()
        except Exception as exc:
            self.pixels = previous_pixels
            self.update_led_display()
            self._set_lighting_status(f"❌ Failed to clear LEDs: {exc}")
            return

        self._set_lighting_status("🗑️ All LEDs cleared - LIVE (not saved)")

    def fill_color(self):
        """Fill all LEDs with current color - sends immediately."""
        r, g, b = self.current_color
        previous_pixels = [pixel[:] for pixel in self.pixels]
        self.pixels = [[r, g, b] for _ in range(64)]
        self.update_led_display()

        try:
            self.device.led_fill(r, g, b)
        except Exception as exc:
            self.pixels = previous_pixels
            self.update_led_display()
            self._set_lighting_status(f"❌ Failed to fill LEDs: {exc}")
            return

        self._set_lighting_status(f"🎨 Filled all LEDs with RGB({r}, {g}, {b}) - LIVE (not saved)")

    def rainbow_test(self):
        """Show rainbow test pattern."""
        try:
            self.device.led_test_rainbow()
        except Exception as exc:
            self._set_lighting_status(f"❌ Rainbow test failed: {exc}")
            return

        self._set_lighting_status("🌈 Rainbow test pattern displayed")
        self.after(200, self.load_from_device)
