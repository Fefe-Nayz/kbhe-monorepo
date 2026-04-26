from .common import *


class DevicePageMixin:
    def _set_device_status(self, message):
        if hasattr(self, "status_var"):
            self.status_var.set(message)

    @staticmethod
    def _clamp_channel(value):
        try:
            return max(0, min(255, int(value)))
        except (TypeError, ValueError):
            return 0

    def _serialize_pixels(self):
        payload = []
        for index in range(64):
            if index < len(getattr(self, "pixels", [])):
                pixel = self.pixels[index]
            else:
                pixel = (0, 0, 0)
            channels = list(pixel[:3]) + [0, 0, 0]
            payload.extend(self._clamp_channel(channel) for channel in channels[:3])
        return payload[:192]

    def _load_pixel_blob(self, blob):
        if len(blob) < 192:
            return False

        for index in range(64):
            base = index * 3
            self.pixels[index] = [blob[base], blob[base + 1], blob[base + 2]]
        return True

    def create_settings_widgets(self, parent):
        """Create Settings tab widgets."""
        info_frame = ttk.LabelFrame(parent, text="Device Information", padding=10, style="Card.TLabelframe")
        info_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            info_frame,
            text="Use this page to inspect the current firmware, toggle HID interfaces, and save or reset persistent settings.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 8))

        self.firmware_label = ttk.Label(info_frame, text="Firmware Version: Loading...", font=("Segoe UI Semibold", 10, "bold"))
        self.firmware_label.pack(anchor=tk.W)

        try:
            version = self.device.get_firmware_version()
        except Exception as exc:
            version = None
            self._set_device_status(f"⚠️ Failed to read firmware version: {exc}")
        self.firmware_label.config(text=f"Firmware Version: {version if version else 'Unknown'}")

        interfaces_frame = ttk.LabelFrame(parent, text="HID Interfaces", padding=10, style="Card.TLabelframe")
        interfaces_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            interfaces_frame,
            text="Keyboard, gamepad, and keyboard mode changes are saved immediately in firmware. LED enable stays live-only until you save.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 8))

        self.keyboard_enabled_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            interfaces_frame,
            text="⌨️ Keyboard HID (sends keypresses)",
            variable=self.keyboard_enabled_var,
            command=self.on_keyboard_enabled_change,
        ).pack(anchor=tk.W, pady=2)

        self.gamepad_enabled_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            interfaces_frame,
            text="🎮 Gamepad HID (analog axes)",
            variable=self.gamepad_enabled_var,
            command=self.on_gamepad_enabled_change,
        ).pack(anchor=tk.W, pady=2)

        self.led_enabled_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            interfaces_frame,
            text="💡 LED Matrix (WS2812)",
            variable=self.led_enabled_var,
            command=self.on_led_enabled_change,
        ).pack(anchor=tk.W, pady=2)

        ttk.Separator(interfaces_frame, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)
        ttk.Label(interfaces_frame, text="Keyboard mode", style="SurfaceSubtle.TLabel").pack(anchor=tk.W, pady=(0, 2))

        self.nkro_enabled_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            interfaces_frame,
            text="🔠 Auto NKRO (fallback to 6KRO if NKRO is unavailable at startup)",
            variable=self.nkro_enabled_var,
            command=self.on_nkro_enabled_change,
        ).pack(anchor=tk.W, pady=2)
        ttk.Label(
            interfaces_frame,
            text="Enabled = Auto NKRO. Disabled = 6KRO only.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 0))

        save_frame = ttk.LabelFrame(parent, text="Save Settings", padding=10, style="Card.TLabelframe")
        save_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            save_frame,
            text="Saving writes the current LED matrix contents, brightness, and LED enable state into flash memory. Keyboard, gamepad, and keyboard mode changes already persist when changed.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 8))

        ttk.Button(
            save_frame,
            text="💾 Save All Settings to Flash",
            command=self.save_to_device,
            style="Primary.TButton",
        ).pack(fill=tk.X)

        reset_frame = ttk.LabelFrame(parent, text="Factory Reset", padding=10, style="Card.TLabelframe")
        reset_frame.pack(fill=tk.X)
        ttk.Label(
            reset_frame,
            text="Resetting clears LED patterns and returns interface options to their defaults.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 8))

        ttk.Button(reset_frame, text="⚠️ Factory Reset", command=self.factory_reset, style="Ghost.TButton").pack(anchor=tk.W)

    def load_from_device(self):
        """Load current state from device."""
        errors = []

        try:
            version = self.device.get_firmware_version()
            if hasattr(self, "firmware_label"):
                self.firmware_label.config(text=f"Firmware Version: {version if version else 'Unknown'}")
        except Exception as exc:
            errors.append(f"firmware version: {exc}")

        try:
            brightness = self.device.led_get_brightness()
            if brightness is not None:
                self.brightness_var.set(brightness)
                self.brightness_label.config(text=str(brightness))
        except Exception as exc:
            errors.append(f"brightness: {exc}")

        try:
            led_enabled = self.device.led_get_enabled()
            if led_enabled is not None:
                self.led_enabled_var.set(led_enabled)
        except Exception as exc:
            errors.append(f"LED enabled: {exc}")

        try:
            options = self.device.get_options()
            if options:
                keyboard_enabled = options.get("keyboard_enabled")
                gamepad_enabled = options.get("gamepad_enabled")
                if keyboard_enabled is not None:
                    self.keyboard_enabled_var.set(keyboard_enabled)
                if gamepad_enabled is not None:
                    self.gamepad_enabled_var.set(gamepad_enabled)
        except Exception as exc:
            errors.append(f"options: {exc}")

        if hasattr(self, "nkro_enabled_var"):
            try:
                nkro_enabled = self.device.get_nkro_enabled()
                if nkro_enabled is not None:
                    self.nkro_enabled_var.set(nkro_enabled)
            except Exception as exc:
                errors.append(f"NKRO: {exc}")

        try:
            pixel_data = self.device.led_download_all()
            if pixel_data:
                if len(pixel_data) >= 192:
                    self._load_pixel_blob(pixel_data[:192])
                    self.update_led_display()
                else:
                    errors.append(f"pixels: expected 192 bytes, got {len(pixel_data)}")
        except Exception as exc:
            errors.append(f"pixels: {exc}")

        if errors:
            self._set_device_status("⚠️ Loaded with warnings: " + "; ".join(errors))
        else:
            self._set_device_status("🔄 Loaded current state from device")

    def save_to_device(self):
        """Save current state to device flash."""
        try:
            pixel_data = self._serialize_pixels()
        except Exception as exc:
            self._set_device_status(f"❌ Failed to prepare LED data: {exc}")
            messagebox.showerror("Error", f"Failed to prepare LED data:\n{exc}")
            return

        try:
            self._set_device_status("💾 Uploading LED data...")
            self.update_idletasks()

            if not self.device.led_upload_all(pixel_data):
                self._set_device_status("❌ Failed to upload LED data")
                messagebox.showerror("Error", "Failed to upload LED data to device")
                return

            self._set_device_status("💾 Saving to flash...")
            self.update_idletasks()

            if not self.device.save_settings():
                self._set_device_status("❌ Failed to save to flash")
                messagebox.showerror("Error", "Failed to save settings to flash")
                return
        except Exception as exc:
            self._set_device_status(f"❌ Error saving: {exc}")
            messagebox.showerror("Error", f"Error saving to device: {exc}")
            return

        self._set_device_status("✅ All settings saved to flash!")
        try:
            self.refresh_from_device()
        except Exception as exc:
            self._set_device_status(f"✅ Saved to flash, but refresh failed: {exc}")
        messagebox.showinfo(
            "Success",
            "Settings saved to device flash!\n\nYour LED pattern and settings will persist after power cycle.",
        )

    def export_to_file(self):
        """Export LED pattern to file."""
        filename = filedialog.asksaveasfilename(
            defaultextension=".led",
            filetypes=[("LED Pattern", "*.led"), ("All Files", "*.*")],
            title="Export LED Pattern",
        )
        if not filename:
            return

        try:
            brightness = self._clamp_channel(self.brightness_var.get())
            payload = bytes([brightness]) + bytes(self._serialize_pixels())
            pathlib.Path(filename).write_bytes(payload)
        except Exception as exc:
            self._set_device_status(f"❌ Export error: {exc}")
            messagebox.showerror("Export error", f"Failed to export LED pattern:\n{exc}")
            return

        self._set_device_status(f"📁 Exported to {filename}")

    def import_from_file(self):
        """Import LED pattern from file."""
        filename = filedialog.askopenfilename(
            title="Import LED Pattern",
            filetypes=[("LED Pattern", "*.led"), ("All Files", "*.*")],
        )
        if not filename:
            return

        try:
            data = pathlib.Path(filename).read_bytes()
        except Exception as exc:
            self._set_device_status(f"❌ Import error: {exc}")
            messagebox.showerror("Import error", f"Failed to read LED pattern:\n{exc}")
            return

        if len(data) < 193:
            self._set_device_status("❌ Invalid file format")
            messagebox.showerror("Import error", "Invalid LED pattern file.\nExpected 193 bytes.")
            return

        try:
            brightness = data[0]
            self.brightness_var.set(brightness)
            self.brightness_label.config(text=str(brightness))
            self.device.led_set_brightness(brightness)

            self._load_pixel_blob(data[1:193])
            self.update_led_display()

            pixel_data = list(data[1:193])
            if not self.device.led_upload_all(pixel_data):
                raise RuntimeError("device rejected the LED payload")
        except Exception as exc:
            self._set_device_status(f"❌ Import error: {exc}")
            messagebox.showerror("Import error", f"Failed to import LED pattern:\n{exc}")
            return

        self._set_device_status(f"📂 Imported from {filename} - LIVE (not saved)")

    def on_led_enabled_change(self):
        """Handle LED enabled checkbox change."""
        enabled = self.led_enabled_var.get()
        try:
            if not self.device.led_set_enabled(enabled):
                raise RuntimeError("device rejected the LED interface change")
        except Exception as exc:
            self.led_enabled_var.set(not enabled)
            self._set_device_status(f"❌ Failed to update LED interface: {exc}")
            return

        self._set_device_status(f"💡 LED {'enabled' if enabled else 'disabled'} - live only until Save to Flash")

    def on_keyboard_enabled_change(self):
        """Handle keyboard enabled checkbox change."""
        enabled = self.keyboard_enabled_var.get()
        try:
            if not self.device.set_keyboard_enabled(enabled):
                raise RuntimeError("device rejected the keyboard HID change")
        except Exception as exc:
            self.keyboard_enabled_var.set(not enabled)
            self._set_device_status(f"❌ Failed to update keyboard HID: {exc}")
            return

        self._set_device_status(f"⌨️ Keyboard {'enabled' if enabled else 'disabled'} - saved immediately")

    def on_gamepad_enabled_change(self):
        """Handle gamepad enabled checkbox change."""
        enabled = self.gamepad_enabled_var.get()
        try:
            if not self.device.set_gamepad_enabled(enabled):
                raise RuntimeError("device rejected the gamepad HID change")
        except Exception as exc:
            self.gamepad_enabled_var.set(not enabled)
            self._set_device_status(f"❌ Failed to update gamepad HID: {exc}")
            return

        self._set_device_status(f"🎮 Gamepad {'enabled' if enabled else 'disabled'} - saved immediately")

    def on_nkro_enabled_change(self):
        """Handle keyboard mode checkbox change (Auto NKRO vs 6KRO only)."""
        enabled = self.nkro_enabled_var.get()
        try:
            if not self.device.set_nkro_enabled(enabled):
                raise RuntimeError("device rejected the NKRO change")
        except Exception as exc:
            self.nkro_enabled_var.set(not enabled)
            self._set_device_status(f"❌ Failed to update NKRO mode: {exc}")
            return

        self._set_device_status(
            "🔠 Keyboard mode: Auto NKRO" if enabled else "🔠 Keyboard mode: 6KRO only"
        )

    def factory_reset(self):
        """Reset to factory defaults."""
        if not messagebox.askyesno(
            "Factory Reset",
            "Are you sure you want to reset ALL settings to factory defaults?\n\n"
            "This will:\n"
            "- Clear all LED patterns\n"
            "- Reset brightness to default\n"
            "- Reset all interface settings\n\n"
            "This cannot be undone!",
        ):
            return

        try:
            if not self.device.factory_reset():
                self._set_device_status("❌ Factory reset failed")
                messagebox.showerror("Error", "Factory reset failed")
                return
        except Exception as exc:
            self._set_device_status(f"❌ Factory reset failed: {exc}")
            messagebox.showerror("Error", f"Factory reset failed:\n{exc}")
            return

        self._set_device_status("🔧 Factory reset complete. Reloading device state...")
        try:
            if hasattr(self, "refresh_from_device"):
                self.refresh_from_device()
            else:
                self.load_from_device()
        except Exception as exc:
            self._set_device_status(f"🔧 Factory reset complete, but reload failed: {exc}")
            messagebox.showwarning("Factory reset", f"Factory reset completed, but reload failed:\n{exc}")
            return

        messagebox.showinfo("Success", "Factory reset complete!\n\nReloading settings...")
