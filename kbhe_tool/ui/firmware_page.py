from .common import *


class FirmwarePageMixin:
    def create_firmware_widgets(self, parent):
        """Create firmware update widgets with file picker and live logs."""
        default_bin = pathlib.Path(__file__).resolve().parents[2] / "build" / "Release" / "kbhe.bin"

        intro = ttk.LabelFrame(parent, text="Firmware Update", padding="10")
        intro.pack(fill=tk.X, pady=5)
        ttk.Label(
            intro,
            text="This flashes only the application slot over the custom HS HID updater. The bootloader stays resident.",
            foreground="gray",
        ).pack(anchor=tk.W)

        file_frame = ttk.LabelFrame(parent, text="Image Selection", padding="10")
        file_frame.pack(fill=tk.X, pady=5)

        self.firmware_path_var = tk.StringVar(value=str(default_bin) if default_bin.exists() else "")
        path_row = ttk.Frame(file_frame)
        path_row.pack(fill=tk.X, pady=4)
        ttk.Label(path_row, text="Firmware .bin:", width=16).pack(side=tk.LEFT)
        ttk.Entry(path_row, textvariable=self.firmware_path_var).pack(
            side=tk.LEFT, fill=tk.X, expand=True, padx=5
        )
        ttk.Button(path_row, text="Browse...", command=self.browse_firmware_file).pack(side=tk.LEFT)

        options_row = ttk.Frame(file_frame)
        options_row.pack(fill=tk.X, pady=4)

        ttk.Label(options_row, text="FW Version:", width=16).grid(row=0, column=0, sticky="w")
        self.firmware_version_var = tk.StringVar(value="")
        ttk.Entry(options_row, textvariable=self.firmware_version_var, width=16).grid(
            row=0, column=1, sticky="w", padx=(0, 10)
        )
        ttk.Label(
            options_row,
            text="Leave blank to auto-read Core/Src/settings.c",
            foreground="gray",
        ).grid(row=0, column=2, sticky="w")

        ttk.Label(options_row, text="Timeout (s):", width=16).grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.firmware_timeout_var = tk.DoubleVar(value=5.0)
        ttk.Spinbox(options_row, from_=1.0, to=30.0, increment=0.5, textvariable=self.firmware_timeout_var, width=10).grid(
            row=1, column=1, sticky="w", padx=(0, 10), pady=(8, 0)
        )

        ttk.Label(options_row, text="Retries:", width=16).grid(row=1, column=2, sticky="w", pady=(8, 0))
        self.firmware_retries_var = tk.IntVar(value=5)
        ttk.Spinbox(options_row, from_=1, to=20, textvariable=self.firmware_retries_var, width=10).grid(
            row=1, column=3, sticky="w", pady=(8, 0)
        )

        action_row = ttk.Frame(file_frame)
        action_row.pack(fill=tk.X, pady=(10, 0))
        self.firmware_flash_button = ttk.Button(
            action_row,
            text="Flash Firmware",
            command=self.start_firmware_flash,
        )
        self.firmware_flash_button.pack(side=tk.LEFT)
        ttk.Button(action_row, text="Clear Log", command=self.clear_firmware_log).pack(side=tk.LEFT, padx=6)

        log_frame = ttk.LabelFrame(parent, text="Updater Log", padding="10")
        log_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        self.firmware_log = scrolledtext.ScrolledText(
            log_frame,
            height=18,
            wrap=tk.WORD,
            state=tk.DISABLED,
            font=("Consolas", 10),
        )
        self.firmware_log.pack(fill=tk.BOTH, expand=True)
        self.append_firmware_log("Ready. Select a .bin image and click Flash Firmware.")

    def browse_firmware_file(self):
        """Open a file picker for the firmware image."""
        filename = filedialog.askopenfilename(
            title="Select firmware image",
            filetypes=[("Firmware binaries", "*.bin"), ("All files", "*.*")],
        )
        if filename:
            self.firmware_path_var.set(filename)
            self.status_var.set(f"Selected firmware image: {filename}")

    def append_firmware_log(self, message):
        """Append a line to the firmware log widget."""
        self.firmware_log.configure(state=tk.NORMAL)
        self.firmware_log.insert(tk.END, f"{message}\n")
        self.firmware_log.see(tk.END)
        self.firmware_log.configure(state=tk.DISABLED)

    def clear_firmware_log(self):
        """Clear the firmware log widget."""
        self.firmware_log.configure(state=tk.NORMAL)
        self.firmware_log.delete("1.0", tk.END)
        self.firmware_log.configure(state=tk.DISABLED)

    def log_firmware_message(self, message):
        """Queue a firmware log line from any thread."""
        if message:
            self.firmware_log_queue.put(("log", message))

    def start_firmware_flash(self):
        """Start the firmware update in a background thread."""
        firmware_path = self.firmware_path_var.get().strip().strip('"')
        if self.firmware_busy:
            return
        if not firmware_path:
            messagebox.showerror("Missing firmware", "Select a firmware .bin file first.")
            return
        if not pathlib.Path(firmware_path).exists():
            messagebox.showerror("Missing firmware", f"File does not exist:\n{firmware_path}")
            return

        version_text = self.firmware_version_var.get().strip()
        try:
            firmware_version = int(version_text, 0) if version_text else None
            timeout_s = float(self.firmware_timeout_var.get())
            retries = int(self.firmware_retries_var.get())
        except ValueError as exc:
            messagebox.showerror("Invalid firmware options", str(exc))
            return

        self.firmware_busy = True
        self.firmware_flash_button.state(["disabled"])
        self.append_firmware_log("-" * 72)
        self.append_firmware_log(f"Starting flash for {firmware_path}")
        self.status_var.set("⏳ Flashing firmware...")

        self.firmware_thread = threading.Thread(
            target=self._firmware_flash_worker,
            args=(firmware_path, firmware_version, timeout_s, retries),
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
        self.firmware_busy = False
        self.firmware_flash_button.state(["!disabled"])

        if success:
            self.append_firmware_log("Firmware update completed successfully.")
            self.refresh_from_device()
            self.status_var.set("✅ Firmware update complete")
        else:
            self.append_firmware_log(f"Firmware update failed: {error_message}")
            self.status_var.set(f"❌ Firmware update failed: {error_message}")
            messagebox.showerror("Firmware update failed", error_message)
