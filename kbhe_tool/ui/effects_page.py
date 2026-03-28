from .common import *


class EffectsPageMixin:
    def create_led_effects_widgets(self, parent):
        """Create LED Effects tab widgets."""

        # Effect Mode Selection
        mode_frame = ttk.LabelFrame(parent, text="🎨 Effect Mode", padding="10")
        mode_frame.pack(fill=tk.X, pady=5)

        self.effect_mode_var = tk.IntVar(value=0)

        effect_modes = [
            (0, "None (Static Pattern)"),
            (1, "Rainbow Wave"),
            (2, "Breathing"),
            (3, "Static Rainbow"),
            (4, "Solid Color"),
            (5, "Plasma"),
            (6, "Fire"),
            (7, "Ocean Waves"),
            (8, "Matrix Rain"),
            (9, "Sparkle"),
            (10, "Breathing Rainbow"),
            (11, "Spiral"),
            (12, "Color Cycle"),
            (13, "Reactive (Key Press)")
        ]

        # Use a scrollable frame for effect modes
        effects_canvas = tk.Canvas(mode_frame, height=200)
        effects_scrollbar = ttk.Scrollbar(mode_frame, orient="vertical", command=effects_canvas.yview)
        effects_inner_frame = ttk.Frame(effects_canvas)

        effects_canvas.configure(yscrollcommand=effects_scrollbar.set)
        effects_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        effects_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        effects_canvas_window = effects_canvas.create_window((0, 0), window=effects_inner_frame, anchor="nw")

        for val, text in effect_modes:
            rb = ttk.Radiobutton(
                effects_inner_frame, text=text, variable=self.effect_mode_var,
                value=val, command=self.on_effect_mode_change
            )
            rb.pack(anchor=tk.W, pady=2)

        def on_effects_frame_configure(event):
            effects_canvas.configure(scrollregion=effects_canvas.bbox("all"))
        effects_inner_frame.bind("<Configure>", on_effects_frame_configure)

        # Effect Speed
        speed_frame = ttk.LabelFrame(parent, text="⚡ Effect Speed", padding="10")
        speed_frame.pack(fill=tk.X, pady=5)

        self.effect_speed_var = tk.IntVar(value=128)
        speed_slider = ttk.Scale(
            speed_frame, from_=1, to=255,
            variable=self.effect_speed_var,
            orient=tk.HORIZONTAL,
            command=self.on_effect_speed_change
        )
        speed_slider.pack(fill=tk.X, padx=5)

        self.effect_speed_label = ttk.Label(speed_frame, text="128", font=('Arial', 12))
        self.effect_speed_label.pack()

        ttk.Label(speed_frame, text="(1 = Slow, 255 = Fast)", foreground="gray").pack()

        # FPS Limit
        fps_frame = ttk.LabelFrame(parent, text="🎬 FPS Limit", padding="10")
        fps_frame.pack(fill=tk.X, pady=5)

        self.fps_limit_var = tk.IntVar(value=60)
        fps_slider = ttk.Scale(
            fps_frame, from_=0, to=120,
            variable=self.fps_limit_var,
            orient=tk.HORIZONTAL,
            command=self.on_fps_limit_change
        )
        fps_slider.pack(fill=tk.X, padx=5)

        self.fps_limit_label = ttk.Label(fps_frame, text="60 FPS", font=('Arial', 12))
        self.fps_limit_label.pack()

        ttk.Label(fps_frame, text="(0 = Unlimited, 1-120 = Limit)", foreground="gray").pack()

        # Effect Color
        color_frame = ttk.LabelFrame(parent, text="🎨 Effect Color", padding="10")
        color_frame.pack(fill=tk.X, pady=5)

        ttk.Label(color_frame, text="Used by Breathing, Color Cycle, and Reactive effects:").pack(anchor=tk.W)

        self.effect_color = [0, 255, 0]  # Default green
        self.effect_color_preview = tk.Canvas(color_frame, width=200, height=40, bg='#00ff00', highlightthickness=1)
        self.effect_color_preview.pack(pady=5)

        ttk.Button(color_frame, text="🎨 Pick Effect Color...", command=self.pick_effect_color).pack(fill=tk.X, pady=2)

        # Quick colors
        quick_frame = ttk.Frame(color_frame)
        quick_frame.pack(fill=tk.X, pady=5)

        quick_colors = [
            ('#ff0000', 'Red'), ('#00ff00', 'Green'), ('#0000ff', 'Blue'), ('#ffffff', 'White'),
            ('#ffff00', 'Yellow'), ('#ff00ff', 'Magenta'), ('#00ffff', 'Cyan'), ('#ff8000', 'Orange'),
        ]

        for i, (color, label) in enumerate(quick_colors):
            btn = tk.Button(
                quick_frame, bg=color, width=8, height=1,
                text=label, fg='white' if color not in ['#ffffff', '#ffff00', '#00ffff'] else 'black',
                command=lambda c=color: self.set_effect_color_hex(c)
            )
            btn.grid(row=i//4, column=i%4, padx=2, pady=2, sticky='nsew')

        for i in range(4):
            quick_frame.columnconfigure(i, weight=1)

        # Load current effect settings
        self.load_led_effect_settings()

    def load_led_effect_settings(self):
        """Load current LED effect settings from device."""
        try:
            mode = self.device.get_led_effect()
            if mode is not None:
                self.effect_mode_var.set(mode)

            speed = self.device.get_led_effect_speed()
            if speed is not None:
                self.effect_speed_var.set(speed)
                self.effect_speed_label.config(text=str(speed))

            fps = self.device.get_led_fps_limit()
            if fps is not None:
                self.fps_limit_var.set(fps)
                self.fps_limit_label.config(text=f"{fps} FPS" if fps > 0 else "Unlimited")
        except Exception as e:
            self.status_var.set(f"❌ Error loading effect settings: {e}")

    def on_effect_mode_change(self):
        """Handle effect mode change."""
        mode = self.effect_mode_var.get()
        self.device.set_led_effect(mode)
        mode_names = ["None", "Rainbow", "Breathing", "Color Cycle", "Wave", "Reactive"]
        self.status_var.set(f"🎨 Effect mode set to {mode_names[mode]} - LIVE (not saved)")

    def on_effect_speed_change(self, value):
        """Handle effect speed change."""
        speed = int(float(value))
        self.effect_speed_label.config(text=str(speed))
        self.device.set_led_effect_speed(speed)
        self.status_var.set(f"⚡ Effect speed set to {speed} - LIVE (not saved)")

    def on_fps_limit_change(self, value):
        """Handle FPS limit change."""
        fps = int(float(value))
        self.fps_limit_label.config(text=f"{fps} FPS" if fps > 0 else "Unlimited")
        self.device.set_led_fps_limit(fps)
        self.status_var.set(f"🎬 FPS limit set to {fps if fps > 0 else 'Unlimited'} - LIVE (not saved)")

    def pick_effect_color(self):
        """Pick effect color."""
        color = colorchooser.askcolor(
            initialcolor=f'#{self.effect_color[0]:02x}{self.effect_color[1]:02x}{self.effect_color[2]:02x}',
            title="Choose Effect Color"
        )
        if color[0]:
            self.effect_color = [int(c) for c in color[0]]
            self.effect_color_preview.config(bg=color[1])
            self.device.set_led_effect_color(*self.effect_color)
            self.status_var.set(f"🎨 Effect color set to RGB({self.effect_color[0]}, {self.effect_color[1]}, {self.effect_color[2]}) - LIVE (not saved)")

    def set_effect_color_hex(self, hex_color):
        """Set effect color from hex."""
        hex_color = hex_color.lstrip('#')
        self.effect_color = [int(hex_color[i:i+2], 16) for i in (0, 2, 4)]
        self.effect_color_preview.config(bg=f'#{hex_color}')
        self.device.set_led_effect_color(*self.effect_color)
        self.status_var.set(f"🎨 Effect color set - LIVE (not saved)")
