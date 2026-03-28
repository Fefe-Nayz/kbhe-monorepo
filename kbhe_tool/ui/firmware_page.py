from .common import *


class FirmwarePageMixin:
    def _set_firmware_status(self, message):
        if hasattr(self, "status_var"):
            self.status_var.set(message)

    @staticmethod
    def _format_bytes(size):
        if size < 1024:
            return f"{size} B"
        if size < 1024 * 1024:
            return f"{size / 1024:.1f} KB"
        return f"{size / (1024 * 1024):.1f} MB"

    @staticmethod
    def _format_timestamp(timestamp):
        return time.strftime("%Y-%m-%d %H:%M", time.localtime(timestamp))

    def _update_firmware_selection_summary(self):
        if not hasattr(self, "firmware_selection_var") or not hasattr(self, "firmware_details_var"):
            return

        firmware_path = self.firmware_path_var.get().strip().strip('"')
        if not firmware_path:
            self.firmware_selection_var.set("No firmware image selected")
            self.firmware_details_var.set("Use Browse or the default build button to pick a .bin file.")
            return

        path = pathlib.Path(firmware_path)
        if not path.exists():
            self.firmware_selection_var.set("Selected firmware file is missing")
            self.firmware_details_var.set(str(path))
            return

        try:
            stat = path.stat()
            size_text = self._format_bytes(stat.st_size)
            modified_text = self._format_timestamp(stat.st_mtime)
        except Exception:
            self.firmware_selection_var.set(f"{path.name} selected")
            self.firmware_details_var.set(str(path))
            return

        self.firmware_selection_var.set(f"{path.name} ready to flash")
        self.firmware_details_var.set(f"{path} • {size_text} • modified {modified_text}")

    def _set_firmware_busy(self, busy):
        self.firmware_busy = busy
        if hasattr(self, "firmware_flash_button"):
            self.firmware_flash_button.state(["disabled"] if busy else ["!disabled"])
            self.firmware_flash_button.config(text="Flashing..." if busy else "Flash Firmware")
        for attr in ("firmware_browse_button", "firmware_default_button"):
            button = getattr(self, attr, None)
            if button is not None:
                button.state(["disabled"] if busy else ["!disabled"])

    def create_firmware_widgets(self, parent):
        """Create firmware update widgets with file picker and live logs."""
        default_bin = pathlib.Path(__file__).resolve().parents[2] / "build" / "Release" / "kbhe.bin"

        self.firmware_busy = getattr(self, "firmware_busy", False)
        self.firmware_log_queue = getattr(self, "firmware_log_queue", queue.Queue())
        self.firmware_log_job = getattr(self, "firmware_log_job", None)
        self.firmware_thread = getattr(self, "firmware_thread", None)
        self.firmware_default_path = default_bin

        intro = ttk.LabelFrame(parent, text="Firmware Update", padding=10, style="Card.TLabelframe")
        intro.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            intro,
            text="This flashes only the application slot over the custom HS HID updater. The bootloader stays resident.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)
        ttk.Label(
            intro,
            text="Pick a .bin image, verify the options below, then start the flash. The log panel shows the updater stream in real time.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(6, 0))

        selection_card = ttk.LabelFrame(parent, text="Image Selection", padding=10, style="Card.TLabelframe")
        selection_card.pack(fill=tk.X, pady=(0, 10))

        self.firmware_path_var = tk.StringVar(value=str(default_bin) if default_bin.exists() else "")
        self.firmware_path_var.trace_add("write", lambda *args: self._update_firmware_selection_summary())
        self.firmware_selection_var = tk.StringVar(value="No firmware image selected")
        self.firmware_details_var = tk.StringVar(value="Use Browse or the default build button to pick a .bin file.")

        ttk.Label(selection_card, textvariable=self.firmware_selection_var, style="SectionTitle.TLabel").pack(anchor=tk.W)
        ttk.Label(
            selection_card,
            textvariable=self.firmware_details_var,
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(2, 8))

        path_row = ttk.Frame(selection_card, style="Surface.TFrame")
        path_row.pack(fill=tk.X)
        ttk.Label(path_row, text="Firmware .bin:", width=16).pack(side=tk.LEFT)
        self.firmware_path_entry = ttk.Entry(path_row, textvariable=self.firmware_path_var)
        self.firmware_path_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))
        self.firmware_browse_button = ttk.Button(path_row, text="Browse...", command=self.browse_firmware_file, style="Ghost.TButton")
        self.firmware_browse_button.pack(side=tk.LEFT, padx=(0, 6))
        self.firmware_default_button = ttk.Button(path_row, text="Use Default Build", command=self.use_default_firmware, style="Ghost.TButton")
        self.firmware_default_button.pack(side=tk.LEFT)
        if not default_bin.exists():
            self.firmware_default_button.state(["disabled"])

        options_card = ttk.LabelFrame(parent, text="Flash Options", padding=10, style="Card.TLabelframe")
        options_card.pack(fill=tk.X, pady=(0, 10))
        options_intro = ttk.Frame(options_card, style="Surface.TFrame")
        options_intro.pack(fill=tk.X, pady=(0, 8))
        ttk.Label(
            options_intro,
            text="Leave firmware version blank to auto-read Core/Src/settings.c.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W)

        options_grid = ttk.Frame(options_card, style="Surface.TFrame")
        options_grid.pack(fill=tk.X)
        options_grid.columnconfigure(1, weight=1)
        self.firmware_version_var = tk.StringVar(value="")
        ttk.Label(options_grid, text="FW Version:", width=16).grid(row=0, column=0, sticky="w")
        ttk.Entry(options_grid, textvariable=self.firmware_version_var, width=18).grid(row=0, column=1, sticky="w", padx=(0, 12))

        ttk.Label(options_grid, text="Timeout (s):", width=16).grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.firmware_timeout_var = tk.DoubleVar(value=5.0)
        ttk.Spinbox(options_grid, from_=1.0, to=30.0, increment=0.5, textvariable=self.firmware_timeout_var, width=10).grid(
            row=1, column=1, sticky="w", padx=(0, 12), pady=(8, 0)
        )

        ttk.Label(options_grid, text="Retries:", width=16).grid(row=1, column=2, sticky="w", pady=(8, 0))
        self.firmware_retries_var = tk.IntVar(value=5)
        ttk.Spinbox(options_grid, from_=1, to=20, textvariable=self.firmware_retries_var, width=10).grid(
            row=1, column=3, sticky="w", pady=(8, 0)
        )

        actions_card = ttk.LabelFrame(parent, text="Updater Controls", padding=10, style="Card.TLabelframe")
        actions_card.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(
            actions_card,
            text="The flash button is the only destructive action on this page. The log can be cleared at any time.",
            style="SurfaceSubtle.TLabel",
            wraplength=980,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(0, 8))

        self.firmware_flash_button = ttk.Button(
            actions_card,
            text="Flash Firmware",
            command=self.start_firmware_flash,
            style="Primary.TButton",
        )
        self.firmware_flash_button.pack(fill=tk.X)

        action_row = ttk.Frame(actions_card, style="Surface.TFrame")
        action_row.pack(fill=tk.X, pady=(8, 0))
        self.firmware_clear_button = ttk.Button(action_row, text="Clear Log", command=self.clear_firmware_log, style="Ghost.TButton")
        self.firmware_clear_button.pack(side=tk.LEFT)

        log_frame = ttk.LabelFrame(parent, text="Updater Log", padding=10, style="Card.TLabelframe")
        log_frame.pack(fill=tk.BOTH, expand=True)
        self.firmware_log = scrolledtext.ScrolledText(
            log_frame,
            height=18,
            wrap=tk.WORD,
            state=tk.DISABLED,
            font=("Consolas", 10),
        )
        self.firmware_log.pack(fill=tk.BOTH, expand=True)

        self._update_firmware_selection_summary()
        self.append_firmware_log("Ready. Select a .bin image and click Flash Firmware.")

    def browse_firmware_file(self):
        """Open a file picker for the firmware image."""
        filename = filedialog.askopenfilename(
            title="Select firmware image",
            filetypes=[("Firmware binaries", "*.bin"), ("All files", "*.*")],
        )
        if not filename:
            return

        self.firmware_path_var.set(filename)
        self._update_firmware_selection_summary()
        self._set_firmware_status(f"Selected firmware image: {filename}")

    def use_default_firmware(self):
        """Populate the firmware path with the default build output if it exists."""
        default_bin = getattr(self, "firmware_default_path", None)
        if default_bin is None or not pathlib.Path(default_bin).exists():
            self._set_firmware_status("❌ Default build output not found")
            messagebox.showerror("Missing firmware", "Default build output not found.\n\nBuild kbhe.bin first or browse to a different file.")
            return

        self.firmware_path_var.set(str(default_bin))
        self._update_firmware_selection_summary()
        self._set_firmware_status(f"Selected default build output: {default_bin}")

    def append_firmware_log(self, message):
        """Append a line to the firmware log widget."""
        if not message:
            return
        if not hasattr(self, "firmware_log"):
            return

        try:
            if not self.firmware_log.winfo_exists():
                return
            self.firmware_log.configure(state=tk.NORMAL)
            self.firmware_log.insert(tk.END, f"{message}\n")
            self.firmware_log.see(tk.END)
            self.firmware_log.configure(state=tk.DISABLED)
        except tk.TclError:
            return

    def clear_firmware_log(self):
        """Clear the firmware log widget."""
        if not hasattr(self, "firmware_log"):
            return

        try:
            if not self.firmware_log.winfo_exists():
                return
            self.firmware_log.configure(state=tk.NORMAL)
            self.firmware_log.delete("1.0", tk.END)
            self.firmware_log.configure(state=tk.DISABLED)
        except tk.TclError:
            return

    def log_firmware_message(self, message):
        """Queue a firmware log line from any thread."""
        if message and hasattr(self, "firmware_log_queue"):
            self.firmware_log_queue.put(("log", message))

    def start_firmware_flash(self):
        """Start the firmware update in a background thread."""
        firmware_path = self.firmware_path_var.get().strip().strip('"')
        if self.firmware_busy:
            self._set_firmware_status("⏳ Firmware flash already in progress")
            return
        if not firmware_path:
            messagebox.showerror("Missing firmware", "Select a firmware .bin file first.")
            return

        path = pathlib.Path(firmware_path)
        if not path.exists():
            messagebox.showerror("Missing firmware", f"File does not exist:\n{firmware_path}")
            self._set_firmware_status(f"❌ Missing firmware image: {firmware_path}")
            return
        if path.stat().st_size <= 0:
            messagebox.showerror("Missing firmware", f"File is empty:\n{firmware_path}")
            self._set_firmware_status(f"❌ Firmware image is empty: {firmware_path}")
            return

        version_text = self.firmware_version_var.get().strip()
        try:
            firmware_version = int(version_text, 0) if version_text else None
            timeout_s = max(1.0, float(self.firmware_timeout_var.get()))
            retries = max(1, int(self.firmware_retries_var.get()))
        except ValueError as exc:
            messagebox.showerror("Invalid firmware options", str(exc))
            return

        version_label = version_text if version_text else "auto-detect from Core/Src/settings.c"
        confirm = messagebox.askyesno(
            "Confirm firmware flash",
            f"Flash the selected firmware image?\n\n"
            f"File: {path}\n"
            f"Version: {version_label}\n"
            f"Timeout: {timeout_s:.1f}s\n"
            f"Retries: {retries}\n\n"
            "Only the application slot will be updated.",
        )
        if not confirm:
            return

        self._set_firmware_busy(True)
        self.append_firmware_log("-" * 72)
        self.append_firmware_log(f"Starting flash for {path}")
        self.append_firmware_log(f"Version: {version_label}")
        self.append_firmware_log(f"Timeout: {timeout_s:.1f}s | Retries: {retries}")
        self._set_firmware_status("⏳ Flashing firmware...")

        self.firmware_thread = threading.Thread(
            target=self._firmware_flash_worker,
            args=(str(path), firmware_version, timeout_s, retries),
            daemon=True,
        )
        self.firmware_thread.start()

        if self.firmware_log_job is None:
            self._process_firmware_log_queue()

    def _firmware_flash_worker(self, firmware_path, firmware_version, timeout_s, retries):
        """Background worker for firmware updates."""
        try:
            perform_firmware_update(
                self.device,
                firmware_path,
                firmware_version=firmware_version,
                timeout_s=timeout_s,
                retries=retries,
                reconnect_after=False,
                logger=self.log_firmware_message,
            )
            reconnect_device(self.device, timeout_s=timeout_s, logger=self.log_firmware_message)
            self.firmware_log_queue.put(("done", None))
        except Exception as exc:
            self.firmware_log_queue.put(("error", str(exc)))

    def _process_firmware_log_queue(self):
        """Process queued firmware log events on the GUI thread."""
        self.firmware_log_job = None

        while not self.firmware_log_queue.empty():
            event, payload = self.firmware_log_queue.get_nowait()
            if event == "log":
                self.append_firmware_log(payload)
            elif event == "done":
                self._finish_firmware_flash(True)
            elif event == "error":
                self._finish_firmware_flash(False, payload)

        if self.firmware_busy or not self.firmware_log_queue.empty():
            self.firmware_log_job = self.after(100, self._process_firmware_log_queue)

    def _finish_firmware_flash(self, success, error_message=None):
        """Finalize GUI state after the firmware worker stops."""
        self._set_firmware_busy(False)

        if success:
            self.append_firmware_log("Firmware update completed successfully.")
            try:
                self.refresh_from_device()
            except Exception as exc:
                self._set_firmware_status(f"✅ Firmware update complete, but refresh failed: {exc}")
                self.append_firmware_log(f"Refresh after firmware update failed: {exc}")
                messagebox.showwarning("Firmware update", f"Firmware update completed, but refresh failed:\n{exc}")
                return

            self._set_firmware_status("✅ Firmware update complete")
            messagebox.showinfo("Firmware update", "Firmware update completed successfully.")
            return

        self.append_firmware_log(f"Firmware update failed: {error_message}")
        self._set_firmware_status(f"❌ Firmware update failed: {error_message}")
        if error_message:
            messagebox.showerror("Firmware update failed", error_message)
