from .common import *


class DevicePageMixin:
    def create_settings_widgets(self, parent):
        """Create Settings tab widgets."""

        # Device Info
        info_frame = ttk.LabelFrame(parent, text="📱 Device Information", padding="10")
        info_frame.pack(fill=tk.X, pady=5)

        self.firmware_label = ttk.Label(info_frame, text="Firmware Version: Loading...", font=('Arial', 10))
        self.firmware_label.pack(anchor=tk.W)

        # Refresh firmware version
        version = self.device.get_firmware_version()
        self.firmware_label.config(text=f"Firmware Version: {version if version else 'Unknown'}")

        # HID Interfaces
        interfaces_frame = ttk.LabelFrame(parent, text="🔌 HID Interfaces (LIVE changes)", padding="10")
        interfaces_frame.pack(fill=tk.X, pady=5)

        ttk.Label(interfaces_frame, text="⚠️ Changes take effect immediately but are NOT saved until you click 'Save to Flash'",
                  foreground="orange").pack(anchor=tk.W, pady=(0, 10))

        self.keyboard_enabled_var = tk.BooleanVar(value=False)
        kb_check = ttk.Checkbutton(
            interfaces_frame, text="⌨️ Keyboard HID (sends keypresses)",
            variable=self.keyboard_enabled_var,
            command=self.on_keyboard_enabled_change
        )
        kb_check.pack(anchor=tk.W, pady=2)

        self.gamepad_enabled_var = tk.BooleanVar(value=True)
        gp_check = ttk.Checkbutton(
            interfaces_frame, text="🎮 Gamepad HID (analog axes)",
            variable=self.gamepad_enabled_var,
            command=self.on_gamepad_enabled_change
        )
        gp_check.pack(anchor=tk.W, pady=2)

        self.led_enabled_var = tk.BooleanVar(value=True)
        led_check = ttk.Checkbutton(
            interfaces_frame, text="💡 LED Matrix (WS2812)",
            variable=self.led_enabled_var,
            command=self.on_led_enabled_change
        )
        led_check.pack(anchor=tk.W, pady=2)

        # NKRO Mode
        ttk.Separator(interfaces_frame, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=5)
        ttk.Label(interfaces_frame, text="Keyboard Mode:", font=('Arial', 9, 'bold')).pack(anchor=tk.W)

        self.nkro_enabled_var = tk.BooleanVar(value=False)
        nkro_check = ttk.Checkbutton(
            interfaces_frame, text="🔠 NKRO Mode (N-Key Rollover instead of 6KRO)",
            variable=self.nkro_enabled_var,
            command=self.on_nkro_enabled_change
        )
        nkro_check.pack(anchor=tk.W, pady=2)
        ttk.Label(interfaces_frame, text="    Uses independent HID interface for unlimited simultaneous keys",
                  foreground="gray").pack(anchor=tk.W)

        # Save section
        save_frame = ttk.LabelFrame(parent, text="💾 Save Settings to Flash", padding="10")
        save_frame.pack(fill=tk.X, pady=5)

        ttk.Label(save_frame, text="Click to save ALL settings (LED pattern, brightness, enabled states)\nto flash memory. Settings will persist after power cycle.",
                  justify=tk.LEFT).pack(anchor=tk.W, pady=5)

        ttk.Button(save_frame, text="💾 SAVE ALL SETTINGS TO FLASH", 
                   command=self.save_to_device).pack(fill=tk.X, pady=5)

        # Factory Reset
        reset_frame = ttk.LabelFrame(parent, text="🔧 Factory Reset", padding="10")
        reset_frame.pack(fill=tk.X, pady=5)

        ttk.Label(reset_frame, text="Reset all settings to factory defaults.\nThis will clear LED patterns and reset all options.",
                  foreground="red").pack(anchor=tk.W, pady=5)

        ttk.Button(reset_frame, text="⚠️ Factory Reset", command=self.factory_reset).pack(anchor=tk.W)

    def load_from_device(self):
        """Load current state from device."""
        try:
            version = self.device.get_firmware_version()
            if hasattr(self, 'firmware_label'):
                self.firmware_label.config(text=f"Firmware Version: {version if version else 'Unknown'}")

            # Load brightness
            brightness = self.device.led_get_brightness()
            if brightness is not None:
                self.brightness_var.set(brightness)
                self.brightness_label.config(text=str(brightness))

            # Load enabled states
            led_enabled = self.device.led_get_enabled()
            if led_enabled is not None:
                self.led_enabled_var.set(led_enabled)

            options = self.device.get_options()
            if options:
                self.keyboard_enabled_var.set(options['keyboard_enabled'])
                self.gamepad_enabled_var.set(options['gamepad_enabled'])

            # Load NKRO state
            nkro_enabled = self.device.get_nkro_enabled()
            if nkro_enabled is not None and hasattr(self, 'nkro_enabled_var'):
                self.nkro_enabled_var.set(nkro_enabled)

            # Load pixel data
            pixel_data = self.device.led_download_all()
            if pixel_data:
                for i in range(64):
                    self.pixels[i] = [pixel_data[i*3], pixel_data[i*3+1], pixel_data[i*3+2]]
                self.update_led_display()

            self.status_var.set("🔄 Loaded current state from device")
        except Exception as e:
            self.status_var.set(f"❌ Error loading: {e}")

    def save_to_device(self):
        """Save current state to device flash."""
        try:
            # Upload pixel data first
            pixel_data = []
            for pixel in self.pixels:
                pixel_data.extend(pixel)

            self.status_var.set("💾 Uploading LED data...")
            self.update_idletasks()

            if self.device.led_upload_all(pixel_data):
                self.status_var.set("💾 Saving to flash...")
                self.update_idletasks()

                if self.device.save_settings():
                    self.status_var.set("✅ All settings saved to flash!")
                    self.refresh_from_device()
                    messagebox.showinfo("Success", "Settings saved to device flash!\n\nYour LED pattern and settings will persist after power cycle.")
                else:
                    self.status_var.set("❌ Failed to save to flash")
                    messagebox.showerror("Error", "Failed to save settings to flash")
            else:
                self.status_var.set("❌ Failed to upload LED data")
                messagebox.showerror("Error", "Failed to upload LED data to device")
        except Exception as e:
            self.status_var.set(f"❌ Error saving: {e}")
            messagebox.showerror("Error", f"Error saving to device: {e}")

    def export_to_file(self):
        """Export LED pattern to file."""
        filename = filedialog.asksaveasfilename(
            defaultextension=".led",
            filetypes=[("LED Pattern", "*.led"), ("All Files", "*.*")]
        )
        if filename:
            try:
                with open(filename, 'wb') as f:
                    f.write(bytes([self.brightness_var.get()]))
                    for pixel in self.pixels:
                        f.write(bytes(pixel))
                self.status_var.set(f"📁 Exported to {filename}")
            except Exception as e:
                self.status_var.set(f"❌ Export error: {e}")

    def import_from_file(self):
        """Import LED pattern from file."""
        filename = filedialog.askopenfilename(
            filetypes=[("LED Pattern", "*.led"), ("All Files", "*.*")]
        )
        if filename:
            try:
                with open(filename, 'rb') as f:
                    data = f.read()

                if len(data) >= 193:
                    brightness = data[0]
                    self.brightness_var.set(brightness)
                    self.brightness_label.config(text=str(brightness))
                    self.device.led_set_brightness(brightness)

                    for i in range(64):
                        self.pixels[i] = [data[1 + i*3], data[1 + i*3 + 1], data[1 + i*3 + 2]]

                    self.update_led_display()

                    # Upload to device
                    pixel_data = list(data[1:193])
                    self.device.led_upload_all(pixel_data)

                    self.status_var.set(f"📂 Imported from {filename} - LIVE (not saved)")
                else:
                    self.status_var.set("❌ Invalid file format")
            except Exception as e:
                self.status_var.set(f"❌ Import error: {e}")

    def on_led_enabled_change(self):
        """Handle LED enabled checkbox change."""
        enabled = self.led_enabled_var.get()
        self.device.led_set_enabled(enabled)
        self.status_var.set(f"💡 LED {'enabled' if enabled else 'disabled'} - LIVE (not saved)")

    def on_keyboard_enabled_change(self):
        """Handle keyboard enabled checkbox change."""
        enabled = self.keyboard_enabled_var.get()
        self.device.set_keyboard_enabled(enabled)
        self.status_var.set(f"⌨️ Keyboard {'enabled' if enabled else 'disabled'} - LIVE (not saved)")

    def on_gamepad_enabled_change(self):
        """Handle gamepad enabled checkbox change."""
        enabled = self.gamepad_enabled_var.get()
        self.device.set_gamepad_enabled(enabled)
        self.status_var.set(f"🎮 Gamepad {'enabled' if enabled else 'disabled'} - LIVE (not saved)")

    def on_nkro_enabled_change(self):
        """Handle NKRO mode checkbox change."""
        enabled = self.nkro_enabled_var.get()
        self.device.set_nkro_enabled(enabled)
        self.status_var.set(f"🔠 NKRO {'enabled' if enabled else 'disabled'} - LIVE (not saved)")

    def factory_reset(self):
        """Reset to factory defaults."""
        if messagebox.askyesno("Factory Reset", 
                "Are you sure you want to reset ALL settings to factory defaults?\n\n"
                "This will:\n"
                "- Clear all LED patterns\n"
                "- Reset brightness to default\n"
                "- Reset all interface settings\n\n"
                "This cannot be undone!"):
            if self.device.factory_reset():
                self.status_var.set("🔧 Factory reset complete!")
                messagebox.showinfo("Success", "Factory reset complete!\n\nReloading settings...")
                self.load_from_device()
            else:
                self.status_var.set("❌ Factory reset failed")
                messagebox.showerror("Error", "Factory reset failed")
