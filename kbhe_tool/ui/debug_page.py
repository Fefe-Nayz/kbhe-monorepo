from .common import *


class DebugPageMixin:
    def create_debug_widgets(self, parent):
        """Create Debug/Sensors tab widgets."""

        # ADC Live Values
        adc_frame = ttk.LabelFrame(parent, text="📊 ADC Sensor Values (Live)", padding="10")
        adc_frame.pack(fill=tk.X, pady=5)

        # Refresh rate control
        rate_frame = ttk.Frame(adc_frame)
        rate_frame.pack(fill=tk.X)

        self.live_update_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            rate_frame, text="Enable Live Updates",
            variable=self.live_update_var,
            command=self.toggle_live_update
        ).pack(side=tk.LEFT)

        ttk.Label(rate_frame, text="  Refresh (ms):").pack(side=tk.LEFT)
        self.refresh_rate_var = tk.IntVar(value=50)
        refresh_spin = ttk.Spinbox(rate_frame, from_=20, to=500, width=5, 
                                   textvariable=self.refresh_rate_var)
        refresh_spin.pack(side=tk.LEFT, padx=5)

        # LUT Parameters info
        ttk.Label(adc_frame, text="LUT: ADC 2100-2672 → Distance 0-4mm | ADC typical range: 2000-2700",
                  font=('Consolas', 8), foreground='gray').pack(anchor=tk.W, pady=(5,0))

        # Create ADC value displays
        adc_values_frame = ttk.Frame(adc_frame)
        adc_values_frame.pack(fill=tk.X, pady=10)

        self.adc_labels = []
        self.adc_bars = []
        self.distance_labels = []

        # Header row
        header_frame = ttk.Frame(adc_values_frame)
        header_frame.pack(fill=tk.X, pady=2)
        ttk.Label(header_frame, text="Key", width=6, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT)
        ttk.Label(header_frame, text="ADC Bar (2000-2700)", width=30, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT, padx=5)
        ttk.Label(header_frame, text="ADC", width=6, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT)
        ttk.Label(header_frame, text="Distance", width=10, font=('Consolas', 9, 'bold')).pack(side=tk.LEFT)

        for i in range(6):
            row_frame = ttk.Frame(adc_values_frame)
            row_frame.pack(fill=tk.X, pady=2)

            ttk.Label(row_frame, text=f"Key {i+1}:", width=6, font=('Consolas', 9)).pack(side=tk.LEFT)

            # Progress bar for visual - optimized range 2000-2700
            bar = ttk.Progressbar(row_frame, length=250, maximum=700, mode='determinate')
            bar.pack(side=tk.LEFT, padx=5)
            self.adc_bars.append(bar)

            # Numeric ADC value
            label = ttk.Label(row_frame, text="----", width=6, font=('Consolas', 10))
            label.pack(side=tk.LEFT)
            self.adc_labels.append(label)

            # Distance value (calculated from LUT)
            dist_label = ttk.Label(row_frame, text="-.--mm", width=10, font=('Consolas', 10))
            dist_label.pack(side=tk.LEFT)
            self.distance_labels.append(dist_label)

        # Key States
        key_frame = ttk.LabelFrame(parent, text="🔘 Key States", padding="10")
        key_frame.pack(fill=tk.X, pady=5)

        key_states_frame = ttk.Frame(key_frame)
        key_states_frame.pack(fill=tk.X)

        self.key_state_labels = []
        self.key_distance_bars = []  # Mini bars for key distances
        for i in range(6):
            frame = ttk.Frame(key_states_frame)
            frame.pack(side=tk.LEFT, padx=10, expand=True)

            ttk.Label(frame, text=f"Key {i+1}", font=('Arial', 9)).pack()
            state_label = ttk.Label(frame, text="⬜", font=('Arial', 20))
            state_label.pack()
            self.key_state_labels.append(state_label)

            # Mini distance bar (0-255 normalized)
            dist_bar = ttk.Progressbar(frame, length=50, maximum=255, mode='determinate')
            dist_bar.pack(pady=2)
            self.key_distance_bars.append(dist_bar)

        # Timing/Performance info
        timing_frame = ttk.LabelFrame(parent, text="⏱️ Performance", padding="10")
        timing_frame.pack(fill=tk.X, pady=5)

        timing_info_frame = ttk.Frame(timing_frame)
        timing_info_frame.pack(fill=tk.X)

        ttk.Label(timing_info_frame, text="HID Poll Rate:", width=15).pack(side=tk.LEFT)
        self.hid_rate_label = ttk.Label(timing_info_frame, text="-- Hz", font=('Consolas', 10))
        self.hid_rate_label.pack(side=tk.LEFT, padx=10)

        ttk.Label(timing_info_frame, text="GUI Update Rate:", width=15).pack(side=tk.LEFT)
        self.gui_rate_label = ttk.Label(timing_info_frame, text="-- Hz", font=('Consolas', 10))
        self.gui_rate_label.pack(side=tk.LEFT, padx=10)

        # Timing tracking
        self.last_update_time = time.time()
        self.update_count = 0

        # Lock Indicators Status
        lock_frame = ttk.LabelFrame(parent, text="🔒 Lock Indicators", padding="10")
        lock_frame.pack(fill=tk.X, pady=5)

        lock_status_frame = ttk.Frame(lock_frame)
        lock_status_frame.pack(fill=tk.X)

        # Caps Lock indicator
        caps_frame = ttk.Frame(lock_status_frame)
        caps_frame.pack(side=tk.LEFT, padx=20)
        ttk.Label(caps_frame, text="Caps Lock", font=('Arial', 10)).pack()
        self.caps_lock_indicator = ttk.Label(caps_frame, text="⬜ OFF", font=('Arial', 14))
        self.caps_lock_indicator.pack()

        # Num Lock indicator
        num_frame = ttk.Frame(lock_status_frame)
        num_frame.pack(side=tk.LEFT, padx=20)
        ttk.Label(num_frame, text="Num Lock", font=('Arial', 10)).pack()
        self.num_lock_indicator = ttk.Label(num_frame, text="⬜ OFF", font=('Arial', 14))
        self.num_lock_indicator.pack()

        # Scroll Lock indicator
        scroll_frame = ttk.Frame(lock_status_frame)
        scroll_frame.pack(side=tk.LEFT, padx=20)
        ttk.Label(scroll_frame, text="Scroll Lock", font=('Arial', 10)).pack()
        self.scroll_lock_indicator = ttk.Label(scroll_frame, text="⬜ OFF", font=('Arial', 14))
        self.scroll_lock_indicator.pack()

        # PE0 LED Status
        pe0_frame = ttk.Frame(lock_status_frame)
        pe0_frame.pack(side=tk.LEFT, padx=20)
        ttk.Label(pe0_frame, text="PE0 LED", font=('Arial', 10)).pack()
        self.pe0_led_indicator = ttk.Label(pe0_frame, text="⬜ OFF", font=('Arial', 14))
        self.pe0_led_indicator.pack()

        # Manual lock toggle buttons (for testing)
        lock_buttons_frame = ttk.Frame(lock_frame)
        lock_buttons_frame.pack(fill=tk.X, pady=5)
        ttk.Button(lock_buttons_frame, text="Toggle Caps Lock", 
                   command=lambda: self.send_keypress(0x39)).pack(side=tk.LEFT, padx=5)
        ttk.Button(lock_buttons_frame, text="Toggle Num Lock", 
                   command=lambda: self.send_keypress(0x53)).pack(side=tk.LEFT, padx=5)
        ttk.Button(lock_buttons_frame, text="Toggle Scroll Lock", 
                   command=lambda: self.send_keypress(0x47)).pack(side=tk.LEFT, padx=5)

        # Current Configuration Summary
        config_frame = ttk.LabelFrame(parent, text="📋 Current Configuration", padding="10")
        config_frame.pack(fill=tk.X, pady=5)

        self.config_text = tk.Text(config_frame, height=10, width=80, font=('Consolas', 9))
        self.config_text.pack(fill=tk.X)

        ttk.Button(config_frame, text="🔄 Refresh Configuration", command=self.refresh_config_display).pack(pady=5)

        # ADC EMA Filter Settings
        filter_frame = ttk.LabelFrame(parent, text="🎚️ ADC EMA Filter Settings", padding="10")
        filter_frame.pack(fill=tk.X, pady=5)

        # Filter enable checkbox
        self.filter_enabled_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            filter_frame, text="Enable ADC EMA Filter",
            variable=self.filter_enabled_var,
            command=self.on_filter_enabled_change
        ).pack(anchor=tk.W)

        ttk.Label(filter_frame, text="(Disabling filter shows raw ADC values, may be noisy)", 
                  foreground="gray").pack(anchor=tk.W, pady=(0, 10))

        # Filter parameters
        params_frame = ttk.Frame(filter_frame)
        params_frame.pack(fill=tk.X)

        # Noise Band
        nb_frame = ttk.Frame(params_frame)
        nb_frame.pack(fill=tk.X, pady=2)
        ttk.Label(nb_frame, text="Noise Band (ADC counts):", width=25).pack(side=tk.LEFT)
        self.filter_noise_band_var = tk.IntVar(value=30)
        ttk.Spinbox(nb_frame, from_=1, to=100, width=8, 
                    textvariable=self.filter_noise_band_var).pack(side=tk.LEFT, padx=5)
        ttk.Label(nb_frame, text="(default: 30)", foreground="gray").pack(side=tk.LEFT)

        # Alpha Min Denominator
        amin_frame = ttk.Frame(params_frame)
        amin_frame.pack(fill=tk.X, pady=2)
        ttk.Label(amin_frame, text="Alpha Min (1/N, slow):", width=25).pack(side=tk.LEFT)
        self.filter_alpha_min_var = tk.IntVar(value=32)
        ttk.Spinbox(amin_frame, from_=2, to=128, width=8, 
                    textvariable=self.filter_alpha_min_var).pack(side=tk.LEFT, padx=5)
        ttk.Label(amin_frame, text="(default: 32 → 1/32 = strong smoothing)", foreground="gray").pack(side=tk.LEFT)

        # Alpha Max Denominator
        amax_frame = ttk.Frame(params_frame)
        amax_frame.pack(fill=tk.X, pady=2)
        ttk.Label(amax_frame, text="Alpha Max (1/N, fast):", width=25).pack(side=tk.LEFT)
        self.filter_alpha_max_var = tk.IntVar(value=4)
        ttk.Spinbox(amax_frame, from_=1, to=32, width=8, 
                    textvariable=self.filter_alpha_max_var).pack(side=tk.LEFT, padx=5)
        ttk.Label(amax_frame, text="(default: 4 → 1/4 = fast response)", foreground="gray").pack(side=tk.LEFT)

        # Apply button
        btn_frame = ttk.Frame(filter_frame)
        btn_frame.pack(fill=tk.X, pady=10)
        ttk.Button(btn_frame, text="📤 Apply Filter Settings", 
                   command=self.apply_filter_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="📥 Reload From Device", 
                   command=self.load_filter_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="🔄 Reset to Defaults", 
                   command=self.reset_filter_defaults).pack(side=tk.LEFT, padx=5)

        # Load initial filter settings
        self.load_filter_settings()

        # LED Diagnostic Mode (for troubleshooting ADC noise)
        diag_frame = ttk.LabelFrame(parent, text="🔬 LED Diagnostic Mode (ADC Noise Testing)", padding="10")
        diag_frame.pack(fill=tk.X, pady=5)

        ttk.Label(diag_frame, text="Use this to diagnose if ADC noise is caused by LED DMA activity or CPU computation:",
                  foreground="gray").pack(anchor=tk.W)

        self.diagnostic_mode_var = tk.IntVar(value=0)

        ttk.Radiobutton(diag_frame, text="Normal Operation", 
                       variable=self.diagnostic_mode_var, value=0,
                       command=self.on_diagnostic_mode_change).pack(anchor=tk.W)

        ttk.Radiobutton(diag_frame, text="Mode 1: DMA Stress (sends LED data, no CPU computation)", 
                       variable=self.diagnostic_mode_var, value=1,
                       command=self.on_diagnostic_mode_change).pack(anchor=tk.W)
        ttk.Label(diag_frame, text="    → If noise appears: DMA/electrical interference is the cause",
                  foreground="blue", font=('Arial', 9)).pack(anchor=tk.W)

        ttk.Radiobutton(diag_frame, text="Mode 2: CPU Stress (computes effects, no LED updates)", 
                       variable=self.diagnostic_mode_var, value=2,
                       command=self.on_diagnostic_mode_change).pack(anchor=tk.W)
        ttk.Label(diag_frame, text="    → If noise appears: CPU load/interrupt latency is the cause",
                  foreground="blue", font=('Arial', 9)).pack(anchor=tk.W)

        ttk.Radiobutton(diag_frame, text="Mode 3: DMA + CPU (computes & sends data, LED pin disabled)", 
                       variable=self.diagnostic_mode_var, value=3,
                       command=self.on_diagnostic_mode_change).pack(anchor=tk.W)
        ttk.Label(diag_frame, text="    → If noise disappears: PWM pin switching is causing interference",
                  foreground="blue", font=('Arial', 9)).pack(anchor=tk.W)

        ttk.Label(diag_frame, text="\n💡 Tips: Also try moving LED cables away from sensor wires to test electrical coupling",
                  foreground="gray").pack(anchor=tk.W)

        # Initial config display
        self.refresh_config_display()

    def toggle_live_update(self):
        """Toggle live sensor updates using background thread."""
        if self.live_update_var.get():
            if self.active_tab == "Debug / Sensors":
                self._start_sensor_updates()
            else:
                self.status_var.set("Live sensor updates armed. Open the Debug / Sensors tab to start polling.")
        else:
            self._stop_sensor_updates()

    def _sensor_reader_thread(self):
        """Background thread that reads sensor data."""
        while self.sensor_thread_running:
            try:
                adc_values = self.device.get_adc_values()
                key_states = self.device.get_key_states()
                lock_states = self.device.get_lock_states()
                self.sensor_queue.put({'adc': adc_values, 'keys': key_states, 'locks': lock_states})
            except Exception as e:
                self.sensor_queue.put({'error': str(e)})

            # Sleep based on refresh rate (convert ms to seconds)
            try:
                refresh_ms = self.refresh_rate_var.get()
            except Exception:
                refresh_ms = 50
            time.sleep(refresh_ms / 1000.0)

    def _process_sensor_queue(self):
        """Process sensor data from queue and update GUI."""
        if not self.live_sensor_update:
            return

        try:
            # Process all queued data (get latest)
            data = None
            while not self.sensor_queue.empty():
                data = self.sensor_queue.get_nowait()

            if data:
                if 'error' in data:
                    self.status_var.set(f"Sensor error: {data['error']}")
                else:
                    self._update_sensor_display(data.get('adc'), data.get('keys'))
                    self._update_lock_display(data.get('locks'))

        except queue.Empty:
            pass
        except Exception as e:
            self.status_var.set(f"GUI update error: {e}")

        # Schedule next GUI update (faster than sensor read for responsiveness)
        self.sensor_update_job = self.after(16, self._process_sensor_queue)  # ~60 FPS GUI

    def _update_sensor_display(self, adc_data, key_states):
        """Update GUI with sensor data (called from main thread)."""
        if adc_data:
            # adc_data is now a dict with 'adc', 'scan_time_us', 'scan_rate_hz'
            adc_values = adc_data.get('adc', [])
            for i, val in enumerate(adc_values):
                # Progress bar range is 2000-2700, so subtract 2000 for bar value
                bar_val = max(0, min(700, val - 2000))
                self.adc_bars[i]['value'] = bar_val
                self.adc_labels[i].config(text=f"{val:4d}")

            # Update MCU timing display
            scan_rate = adc_data.get('scan_rate_hz', 0)
            scan_time = adc_data.get('scan_time_us', 0)
            self.hid_rate_label.config(text=f"{scan_rate} Hz ({scan_time}µs)")

        if key_states:
            for i, state in enumerate(key_states['states']):
                self.key_state_labels[i].config(
                    text="🟢" if state else "⬜",
                    foreground="green" if state else "gray"
                )
            # Update distance bars from normalized distances
            if 'distances' in key_states:
                for i, dist in enumerate(key_states['distances']):
                    self.key_distance_bars[i]['value'] = dist

            # Update distance in mm from MCU (actual LUT values)
            if 'distances_mm' in key_states:
                for i, dist_mm in enumerate(key_states['distances_mm']):
                    self.distance_labels[i].config(text=f"{dist_mm:.2f}mm")

        # Update GUI timing stats
        self.update_count += 1
        now = time.time()
        elapsed = now - self.last_update_time
        if elapsed >= 1.0:
            gui_rate = self.update_count / elapsed
            self.gui_rate_label.config(text=f"{gui_rate:.1f} Hz")
            self.update_count = 0
            self.last_update_time = now

    def _update_lock_display(self, lock_states):
        """Update lock indicator display (called from main thread)."""
        if not lock_states:
            return

        caps = lock_states.get('caps_lock', False)
        num = lock_states.get('num_lock', False)
        scroll = lock_states.get('scroll_lock', False)

        # Update Caps Lock indicator
        self.caps_lock_indicator.config(
            text="🟢 ON" if caps else "⬜ OFF",
            foreground="green" if caps else "gray"
        )

        # Update Num Lock indicator
        self.num_lock_indicator.config(
            text="🟢 ON" if num else "⬜ OFF",
            foreground="green" if num else "gray"
        )

        # Update Scroll Lock indicator
        self.scroll_lock_indicator.config(
            text="🟢 ON" if scroll else "⬜ OFF",
            foreground="green" if scroll else "gray"
        )

        # Update PE0 LED status based on firmware behavior
        # Both ON = solid, Caps only = fast blink, Num only = slow blink, Both OFF = off
        if caps and num:
            pe0_text = "🟢 SOLID"
            pe0_color = "green"
        elif caps and not num:
            pe0_text = "🟡 FAST BLINK"
            pe0_color = "orange"
        elif num and not caps:
            pe0_text = "🟡 SLOW BLINK"
            pe0_color = "orange"
        else:
            pe0_text = "⬜ OFF"
            pe0_color = "gray"

        self.pe0_led_indicator.config(text=pe0_text, foreground=pe0_color)

    def send_keypress(self, keycode):
        """Send a single key press and release via the keyboard interface."""
        # This is just for testing - toggle locks via actual key press
        # For now, show a message that user needs to press physical key
        self.status_var.set(f"Press physical key with HID code 0x{keycode:02X} to toggle")

    def load_filter_settings(self):
        """Load filter settings from device."""
        try:
            enabled = self.device.get_filter_enabled()
            if enabled is not None:
                self.filter_enabled_var.set(enabled)

            params = self.device.get_filter_params()
            if params:
                self.filter_noise_band_var.set(params['noise_band'])
                self.filter_alpha_min_var.set(params['alpha_min_denom'])
                self.filter_alpha_max_var.set(params['alpha_max_denom'])

            self.status_var.set("📥 Filter settings loaded from device")
        except Exception as e:
            self.status_var.set(f"❌ Error loading filter settings: {e}")

    def on_filter_enabled_change(self):
        """Handle filter enable/disable toggle."""
        enabled = self.filter_enabled_var.get()
        if self.device.set_filter_enabled(enabled):
            self.status_var.set(f"🎚️ Filter {'enabled' if enabled else 'disabled'} - LIVE")
        else:
            self.status_var.set("❌ Failed to update filter state")

    def apply_filter_settings(self):
        """Apply filter parameters to device."""
        noise_band = self.filter_noise_band_var.get()
        alpha_min = self.filter_alpha_min_var.get()
        alpha_max = self.filter_alpha_max_var.get()

        if self.device.set_filter_params(noise_band, alpha_min, alpha_max):
            self.status_var.set(f"📤 Filter params applied: band={noise_band}, αmin=1/{alpha_min}, αmax=1/{alpha_max}")
        else:
            self.status_var.set("❌ Failed to apply filter parameters")

    def reset_filter_defaults(self):
        """Reset filter to default values."""
        self.filter_enabled_var.set(True)
        self.filter_noise_band_var.set(30)
        self.filter_alpha_min_var.set(32)
        self.filter_alpha_max_var.set(4)

        # Apply defaults to device
        self.device.set_filter_enabled(True)
        self.device.set_filter_params(30, 32, 4)
        self.status_var.set("🔄 Filter reset to defaults")

    def on_diagnostic_mode_change(self):
        """Handle diagnostic mode change."""
        mode = self.diagnostic_mode_var.get()
        mode_names = {0: "Normal", 1: "DMA Stress", 2: "CPU Stress", 3: "DMA + CPU"}
        if self.device.set_led_diagnostic(mode):
            self.status_var.set(f"🔬 Diagnostic mode: {mode_names.get(mode, 'Unknown')}")
        else:
            self.status_var.set("❌ Failed to set diagnostic mode")

    def refresh_config_display(self):
        """Refresh the configuration display."""
        self.config_text.delete(1.0, tk.END)

        try:
            version = self.device.get_firmware_version()
            options = self.device.get_options()
            brightness = self.device.led_get_brightness()
            led_enabled = self.device.led_get_enabled()

            config_str = f"""=== KBHE Configuration ===

Firmware Version: {version if version else 'Unknown'}

HID Interfaces:
  - Keyboard: {'Enabled' if options and options['keyboard_enabled'] else 'Disabled'}
  - Gamepad:  {'Enabled' if options and options['gamepad_enabled'] else 'Disabled'}
  - Raw HID:  Always Enabled

LED Matrix:
  - Enabled:    {'Yes' if led_enabled else 'No'}
  - Brightness: {brightness if brightness is not None else 'Unknown'}

Note: Changes to toggles are sent immediately to the device
but NOT saved to flash until you click "Save to Flash".
"""
            self.config_text.insert(tk.END, config_str)
        except Exception as e:
            self.config_text.insert(tk.END, f"Error reading config: {e}")
