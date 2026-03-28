from .common import *


class LightingPageMixin:
    def create_led_widgets(self, parent):
        """Create LED Matrix tab widgets."""

        # Info banner
        info_frame = ttk.Frame(parent)
        info_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(info_frame, text="💡 Changes are sent LIVE to the keyboard. Use 'Save to Flash' to make them permanent.",
                  foreground="blue").pack()

        # Main container with two columns
        main_container = ttk.Frame(parent)
        main_container.pack(fill=tk.BOTH, expand=True)

        # Left column: LED Grid
        left_frame = ttk.Frame(main_container)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # --- LED Grid ---
        grid_frame = ttk.LabelFrame(left_frame, text="LED Matrix (8x8) - Click to paint", padding="5")
        grid_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        self.led_buttons = []
        for y in range(8):
            row_buttons = []
            for x in range(8):
                btn = tk.Button(
                    grid_frame,
                    width=5, height=2,
                    bg='#000000',
                    activebackground='#333333',
                    relief=tk.RAISED,
                    command=lambda idx=y*8+x: self.on_led_click(idx)
                )
                btn.grid(row=y, column=x, padx=1, pady=1, sticky='nsew')
                row_buttons.append(btn)
            self.led_buttons.append(row_buttons)

        # Make grid cells expand
        for i in range(8):
            grid_frame.columnconfigure(i, weight=1)
            grid_frame.rowconfigure(i, weight=1)

        # Right column: Controls
        right_frame = ttk.Frame(main_container, width=250)
        right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=(10, 0))
        right_frame.pack_propagate(False)

        # --- Color Picker ---
        color_frame = ttk.LabelFrame(right_frame, text="Paint Color", padding="5")
        color_frame.pack(fill=tk.X, pady=5)

        self.color_preview = tk.Canvas(color_frame, width=220, height=40, bg='#ff0000', highlightthickness=1)
        self.color_preview.pack(pady=5)

        ttk.Button(color_frame, text="🎨 Pick Color...", command=self.pick_color).pack(fill=tk.X, pady=2)

        # Quick colors grid
        quick_frame = ttk.Frame(color_frame)
        quick_frame.pack(fill=tk.X, pady=5)

        quick_colors = [
            ('#ff0000', 'R'), ('#00ff00', 'G'), ('#0000ff', 'B'), ('#ffffff', 'W'),
            ('#ffff00', 'Y'), ('#ff00ff', 'M'), ('#00ffff', 'C'), ('#000000', 'X'),
            ('#ff8000', 'O'), ('#80ff00', 'L'), ('#0080ff', 'S'), ('#8000ff', 'P'),
        ]

        for i, (color, label) in enumerate(quick_colors):
            btn = tk.Button(
                quick_frame, bg=color, width=3, height=1,
                text=label, fg='white' if color != '#ffffff' else 'black',
                command=lambda c=color: self.set_color_hex(c)
            )
            btn.grid(row=i//4, column=i%4, padx=2, pady=2, sticky='nsew')

        for i in range(4):
            quick_frame.columnconfigure(i, weight=1)

        # --- Brightness ---
        brightness_frame = ttk.LabelFrame(right_frame, text="Brightness (LIVE)", padding="5")
        brightness_frame.pack(fill=tk.X, pady=5)

        self.brightness_var = tk.IntVar(value=50)
        self.brightness_slider = ttk.Scale(
            brightness_frame, from_=0, to=255,
            variable=self.brightness_var,
            orient=tk.HORIZONTAL,
            command=self.on_brightness_change
        )
        self.brightness_slider.pack(fill=tk.X, padx=5)

        self.brightness_label = ttk.Label(brightness_frame, text="50", font=('Arial', 14, 'bold'))
        self.brightness_label.pack()

        # --- Quick Actions ---
        action_frame = ttk.LabelFrame(right_frame, text="Quick Actions (LIVE)", padding="5")
        action_frame.pack(fill=tk.X, pady=5)

        ttk.Button(action_frame, text="🗑️ Clear All", command=self.clear_all).pack(fill=tk.X, pady=2)
        ttk.Button(action_frame, text="🎨 Fill with Color", command=self.fill_color).pack(fill=tk.X, pady=2)
        ttk.Button(action_frame, text="🌈 Rainbow Test", command=self.rainbow_test).pack(fill=tk.X, pady=2)
        ttk.Button(action_frame, text="🔄 Reload from Device", command=self.load_from_device).pack(fill=tk.X, pady=2)

        # --- Persistence ---
        save_frame = ttk.LabelFrame(right_frame, text="💾 Persistence", padding="5")
        save_frame.pack(fill=tk.X, pady=5)

        ttk.Label(save_frame, text="Save to make changes\nsurvive power cycle:", 
                  justify=tk.CENTER).pack(pady=2)
        ttk.Button(save_frame, text="💾 SAVE TO FLASH", 
                   command=self.save_to_device, style='Accent.TButton').pack(fill=tk.X, pady=5)

        ttk.Separator(save_frame, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=5)

        btn_frame = ttk.Frame(save_frame)
        btn_frame.pack(fill=tk.X)
        ttk.Button(btn_frame, text="📁 Export", command=self.export_to_file, width=10).pack(side=tk.LEFT, expand=True, padx=2)
        ttk.Button(btn_frame, text="📂 Import", command=self.import_from_file, width=10).pack(side=tk.LEFT, expand=True, padx=2)

    def on_led_click(self, index):
        """Handle LED button click - sends immediately to device."""
        r, g, b = self.current_color
        self.pixels[index] = [r, g, b]
        self.update_led_display(index)

        # Send to device immediately
        self.device.led_set_pixel(index, r, g, b)
        self.status_var.set(f"✏️ Set LED {index} to RGB({r}, {g}, {b}) - LIVE (not saved)")

    def update_led_display(self, index=None):
        """Update LED button colors."""
        if index is not None:
            y, x = divmod(index, 8)
            r, g, b = self.pixels[index]
            color = f'#{r:02x}{g:02x}{b:02x}'
            self.led_buttons[y][x].config(bg=color)
        else:
            for idx in range(64):
                y, x = divmod(idx, 8)
                r, g, b = self.pixels[idx]
                color = f'#{r:02x}{g:02x}{b:02x}'
                self.led_buttons[y][x].config(bg=color)

    def pick_color(self):
        """Open color picker dialog."""
        color = colorchooser.askcolor(
            initialcolor=f'#{self.current_color[0]:02x}{self.current_color[1]:02x}{self.current_color[2]:02x}',
            title="Choose LED Color"
        )
        if color[0]:
            self.current_color = [int(c) for c in color[0]]
            self.color_preview.config(bg=color[1])

    def set_color_hex(self, hex_color):
        """Set current color from hex string."""
        hex_color = hex_color.lstrip('#')
        self.current_color = [int(hex_color[i:i+2], 16) for i in (0, 2, 4)]
        self.color_preview.config(bg=f'#{hex_color}')

    def on_brightness_change(self, value):
        """Handle brightness slider change - sends immediately."""
        brightness = int(float(value))
        self.brightness_label.config(text=str(brightness))
        self.device.led_set_brightness(brightness)
        self.status_var.set(f"💡 Brightness set to {brightness} - LIVE (not saved)")

    def clear_all(self):
        """Clear all LEDs - sends immediately."""
        self.pixels = [[0, 0, 0] for _ in range(64)]
        self.update_led_display()
        self.device.led_clear()
        self.status_var.set("🗑️ All LEDs cleared - LIVE (not saved)")

    def fill_color(self):
        """Fill all LEDs with current color - sends immediately."""
        r, g, b = self.current_color
        self.pixels = [[r, g, b] for _ in range(64)]
        self.update_led_display()
        self.device.led_fill(r, g, b)
        self.status_var.set(f"🎨 Filled all LEDs with RGB({r}, {g}, {b}) - LIVE (not saved)")

    def rainbow_test(self):
        """Show rainbow test pattern."""
        self.device.led_test_rainbow()
        self.status_var.set("🌈 Rainbow test pattern displayed")
        # Reload to show actual state
        self.after(200, self.load_from_device)
