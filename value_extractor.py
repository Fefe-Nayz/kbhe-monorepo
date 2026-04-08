#!/usr/bin/env python3

"""GUI tool to capture ADC filtered values and export calibration points.

Workflow:
- User clicks "Save point".
- Tool captures 32 filtered ADC samples for the selected key.
- Tool computes the median of those 32 samples.
- User provides the matching key travel distance in mm.
- Tool stores one point: (median ADC, distance mm).
- User clicks "Export CSV" to write a timestamped CSV file.
"""

from __future__ import annotations

import csv
import datetime as dt
import queue
import re
import statistics
import threading
import time
from pathlib import Path
from typing import Dict, List

import tkinter as tk
from tkinter import messagebox, simpledialog, ttk

SAMPLES_PER_POINT = 32
MIN_KEY_INDEX = 1
MAX_KEY_INDEX = 6
SAMPLE_DELAY_S = 0.01


class ValueExtractorApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Value Extractor")
        self.root.geometry("760x520")
        self.root.minsize(720, 500)

        self.device = None
        self.points: List[Dict[str, float]] = []

        self.capture_thread: threading.Thread | None = None
        self.capture_queue: queue.Queue = queue.Queue()
        self.capture_cancel = threading.Event()

        self.status_var = tk.StringVar(value="Disconnected")
        self.key_var = tk.IntVar(value=1)
        self.progress_var = tk.IntVar(value=0)
        self.progress_text_var = tk.StringVar(value=f"0 / {SAMPLES_PER_POINT}")

        self._build_ui()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self) -> None:
        container = ttk.Frame(self.root, padding=14)
        container.pack(fill="both", expand=True)
        container.columnconfigure(0, weight=1)
        container.rowconfigure(3, weight=1)

        title = ttk.Label(
            container,
            text="ADC Value Extractor",
            font=("Segoe UI", 15, "bold"),
        )
        title.grid(row=0, column=0, sticky="w")

        controls = ttk.LabelFrame(container, text="Acquisition", padding=10)
        controls.grid(row=1, column=0, sticky="ew", pady=(10, 8))
        controls.columnconfigure(7, weight=1)

        ttk.Label(controls, text="Status:").grid(row=0, column=0, sticky="w")
        self.status_label = ttk.Label(controls, textvariable=self.status_var)
        self.status_label.grid(row=0, column=1, sticky="w", padx=(6, 16))

        self.connect_btn = ttk.Button(controls, text="Connect", command=self._connect_device)
        self.connect_btn.grid(row=0, column=2, sticky="w")

        self.disconnect_btn = ttk.Button(controls, text="Disconnect", command=self._disconnect_device)
        self.disconnect_btn.grid(row=0, column=3, sticky="w", padx=(6, 16))

        ttk.Label(controls, text="Key:").grid(row=0, column=4, sticky="e")
        self.key_spin = ttk.Spinbox(
            controls,
            from_=MIN_KEY_INDEX,
            to=MAX_KEY_INDEX,
            width=6,
            textvariable=self.key_var,
        )
        self.key_spin.grid(row=0, column=5, sticky="w", padx=(6, 16))

        self.save_point_btn = ttk.Button(
            controls,
            text="Save point (32 samples)",
            command=self._start_point_capture,
        )
        self.save_point_btn.grid(row=0, column=6, sticky="w")

        progress_row = ttk.Frame(controls)
        progress_row.grid(row=1, column=0, columnspan=8, sticky="ew", pady=(10, 0))
        progress_row.columnconfigure(0, weight=1)

        self.progress = ttk.Progressbar(
            progress_row,
            orient="horizontal",
            mode="determinate",
            maximum=SAMPLES_PER_POINT,
            variable=self.progress_var,
        )
        self.progress.grid(row=0, column=0, sticky="ew")

        ttk.Label(progress_row, textvariable=self.progress_text_var, width=11).grid(
            row=0,
            column=1,
            sticky="e",
            padx=(8, 0),
        )

        table_frame = ttk.LabelFrame(container, text="Saved points", padding=10)
        table_frame.grid(row=2, column=0, sticky="nsew", pady=(0, 8))
        table_frame.columnconfigure(0, weight=1)
        table_frame.rowconfigure(0, weight=1)

        self.tree = ttk.Treeview(
            table_frame,
            columns=("adc", "distance"),
            show="headings",
            height=9,
        )
        self.tree.heading("adc", text="Tension (PTS ADC)")
        self.tree.heading("distance", text="Distance (mm)")
        self.tree.column("adc", width=220, anchor="center")
        self.tree.column("distance", width=220, anchor="center")
        self.tree.grid(row=0, column=0, sticky="nsew")
        self.tree.bind("<Double-1>", self._edit_selected_distance)

        tree_scroll = ttk.Scrollbar(table_frame, orient="vertical", command=self.tree.yview)
        tree_scroll.grid(row=0, column=1, sticky="ns")
        self.tree.configure(yscrollcommand=tree_scroll.set)

        actions = ttk.Frame(container)
        actions.grid(row=3, column=0, sticky="ew")

        self.export_btn = ttk.Button(actions, text="Finish and export CSV", command=self._export_csv)
        self.export_btn.pack(side="left")

        self.edit_distance_btn = ttk.Button(
            actions,
            text="Edit selected distance",
            command=self._edit_selected_distance,
        )
        self.edit_distance_btn.pack(side="left", padx=(8, 0))

        self.clear_btn = ttk.Button(actions, text="Clear points", command=self._clear_points)
        self.clear_btn.pack(side="left", padx=(8, 0))

        self.log_text = tk.Text(container, height=8, wrap="word", state="disabled")
        self.log_text.grid(row=4, column=0, sticky="nsew", pady=(8, 0))

        self._set_controls_for_connection(connected=False)

    def _log(self, message: str) -> None:
        timestamp = dt.datetime.now().strftime("%H:%M:%S")
        self.log_text.configure(state="normal")
        self.log_text.insert("end", f"[{timestamp}] {message}\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _set_controls_for_connection(self, connected: bool) -> None:
        self.connect_btn.configure(state="disabled" if connected else "normal")
        self.disconnect_btn.configure(state="normal" if connected else "disabled")
        self.save_point_btn.configure(state="normal" if connected else "disabled")

    def _set_capture_busy(self, busy: bool) -> None:
        if busy:
            self.save_point_btn.configure(state="disabled")
            self.export_btn.configure(state="disabled")
            self.edit_distance_btn.configure(state="disabled")
            self.clear_btn.configure(state="disabled")
            self.key_spin.configure(state="disabled")
        else:
            connected = self.device is not None
            self.save_point_btn.configure(state="normal" if connected else "disabled")
            self.export_btn.configure(state="normal")
            self.edit_distance_btn.configure(state="normal")
            self.clear_btn.configure(state="normal")
            self.key_spin.configure(state="normal")

    def _connect_device(self) -> None:
        if self.device is not None:
            return

        try:
            from kbhe_tool.device import KBHEDevice
        except ModuleNotFoundError as ex:
            if ex.name == "hid":
                messagebox.showerror(
                    "Missing dependency",
                    "Python package 'hid' is missing. Install with: pip install hidapi",
                )
                return
            raise

        try:
            device = KBHEDevice()
            device.connect(logger=None)
        except Exception as exc:
            messagebox.showerror("Connection error", f"Could not connect to device:\n{exc}")
            return

        self.device = device
        self.status_var.set("Connected")
        self._set_controls_for_connection(connected=True)
        self._log("Device connected.")

    def _disconnect_device(self) -> None:
        if self.capture_thread and self.capture_thread.is_alive():
            messagebox.showwarning("Capture running", "Wait for current capture to finish.")
            return

        if self.device is not None:
            try:
                self.device.disconnect()
            except Exception:
                pass

        self.device = None
        self.status_var.set("Disconnected")
        self._set_controls_for_connection(connected=False)
        self._log("Device disconnected.")

    def _start_point_capture(self) -> None:
        if self.device is None:
            messagebox.showwarning("No device", "Connect to the device first.")
            return

        if self.capture_thread and self.capture_thread.is_alive():
            messagebox.showwarning("Capture running", "A capture is already running.")
            return

        key_number = self.key_var.get()
        if key_number < MIN_KEY_INDEX or key_number > MAX_KEY_INDEX:
            messagebox.showwarning("Invalid key", f"Key must be between {MIN_KEY_INDEX} and {MAX_KEY_INDEX}.")
            return

        self.progress_var.set(0)
        self.progress_text_var.set(f"0 / {SAMPLES_PER_POINT}")
        self._set_capture_busy(True)
        self.capture_cancel.clear()

        key_index = key_number - 1
        self.capture_thread = threading.Thread(
            target=self._capture_worker,
            args=(key_index,),
            daemon=True,
        )
        self.capture_thread.start()

        self._log(f"Capture started for key {key_number}.")
        self.root.after(25, self._drain_capture_queue)

    def _capture_worker(self, key_index: int) -> None:
        try:
            samples: List[int] = []
            for index in range(SAMPLES_PER_POINT):
                if self.capture_cancel.is_set():
                    self.capture_queue.put(("cancelled", None))
                    return

                adc_data = self.device.get_adc_values() if self.device is not None else None
                if not adc_data:
                    self.capture_queue.put(("error", "Failed to read ADC data from device."))
                    return

                filtered = list(adc_data.get("adc_filtered") or adc_data.get("adc") or [])
                if key_index >= len(filtered):
                    self.capture_queue.put(
                        (
                            "error",
                            f"Selected key index {key_index + 1} is out of range for current ADC payload.",
                        )
                    )
                    return

                samples.append(int(filtered[key_index]))
                self.capture_queue.put(("progress", index + 1))
                time.sleep(SAMPLE_DELAY_S)

            adc_median = float(statistics.median(samples))
            self.capture_queue.put(("median_ready", adc_median))
        except Exception as exc:
            self.capture_queue.put(("error", f"Unexpected error: {exc}"))
        finally:
            self.capture_queue.put(("done", None))

    def _drain_capture_queue(self) -> None:
        keep_polling = True

        while True:
            try:
                event, payload = self.capture_queue.get_nowait()
            except queue.Empty:
                break

            if event == "progress":
                current = int(payload)
                self.progress_var.set(current)
                self.progress_text_var.set(f"{current} / {SAMPLES_PER_POINT}")
            elif event == "median_ready":
                adc_median = float(payload)
                distance_mm = simpledialog.askfloat(
                    "Distance input",
                    "Enter distance in mm (float):",
                    minvalue=0.0,
                    parent=self.root,
                )
                if distance_mm is None:
                    self._log("Distance input cancelled. Point not saved.")
                    continue

                point = {
                    "adc_pts": adc_median,
                    "distance_mm": float(distance_mm),
                }
                self.points.append(point)
                self.tree.insert(
                    "",
                    "end",
                    values=(f"{point['adc_pts']:.3f}", f"{point['distance_mm']:.3f}"),
                )
                self._log(
                    "Point saved: "
                    f"median ADC={point['adc_pts']:.3f} pts, "
                    f"distance={point['distance_mm']:.3f} mm."
                )
            elif event == "error":
                keep_polling = False
                self._log(str(payload))
                messagebox.showerror("Capture error", str(payload))
            elif event == "cancelled":
                keep_polling = False
                self._log("Capture cancelled.")
            elif event == "done":
                keep_polling = False

        if keep_polling and self.capture_thread and self.capture_thread.is_alive():
            self.root.after(25, self._drain_capture_queue)
            return

        self._set_capture_busy(False)

    def _clear_points(self) -> None:
        if self.capture_thread and self.capture_thread.is_alive():
            messagebox.showwarning("Capture running", "Wait for current capture to finish.")
            return

        if not self.points:
            return

        if not messagebox.askyesno("Clear", "Remove all saved points?"):
            return

        self.points.clear()
        for item in self.tree.get_children():
            self.tree.delete(item)
        self._log("All points cleared.")

    def _edit_selected_distance(self, _event=None) -> None:
        if self.capture_thread and self.capture_thread.is_alive():
            messagebox.showwarning("Capture running", "Wait for current capture to finish.")
            return

        if not self.points:
            messagebox.showwarning("No data", "No saved points to edit.")
            return

        selected = self.tree.selection()
        if len(selected) != 1:
            messagebox.showwarning("Selection", "Select one row to edit its distance.")
            return

        item_id = selected[0]
        row_index = self.tree.index(item_id)
        if row_index < 0 or row_index >= len(self.points):
            messagebox.showerror("Selection error", "Could not map selected row to data point.")
            return

        point = self.points[row_index]
        current_distance = float(point["distance_mm"])
        new_distance = simpledialog.askfloat(
            "Edit distance",
            "Enter corrected distance in mm (float):",
            initialvalue=current_distance,
            minvalue=0.0,
            parent=self.root,
        )
        if new_distance is None:
            return

        point["distance_mm"] = float(new_distance)
        self.tree.item(
            item_id,
            values=(f"{float(point['adc_pts']):.3f}", f"{float(point['distance_mm']):.3f}"),
        )
        self._log(
            f"Distance updated on row {row_index + 1}: "
            f"{current_distance:.3f} -> {float(new_distance):.3f} mm."
        )

    def _export_csv(self) -> None:
        if self.capture_thread and self.capture_thread.is_alive():
            messagebox.showwarning("Capture running", "Wait for current capture to finish before export.")
            return

        if not self.points:
            messagebox.showwarning("No data", "No points to export.")
            return

        output_dir = Path("data") / "value_extractor"
        output_dir.mkdir(parents=True, exist_ok=True)
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")

        requested_name = simpledialog.askstring(
            "File name",
            "Enter file name (without date/time):",
            initialvalue="capture",
            parent=self.root,
        )
        if requested_name is None:
            self._log("Export cancelled by user.")
            return

        requested_name = requested_name.strip()
        if not requested_name:
            requested_name = "capture"

        safe_name = re.sub(r"[^A-Za-z0-9_-]+", "_", requested_name).strip("_")
        if not safe_name:
            safe_name = "capture"

        output_path = output_dir / f"{stamp}_{safe_name}.csv"

        try:
            with output_path.open("w", newline="", encoding="utf-8") as csv_file:
                writer = csv.writer(csv_file)
                writer.writerow(["tension_adc_pts", "distance_mm"])
                for point in self.points:
                    writer.writerow([point["adc_pts"], point["distance_mm"]])
        except Exception as exc:
            messagebox.showerror("Export error", f"Failed to write CSV:\n{exc}")
            return

        self._log(f"CSV exported: {output_path}")
        messagebox.showinfo("Export complete", f"CSV saved:\n{output_path}")

    def _on_close(self) -> None:
        if self.capture_thread and self.capture_thread.is_alive():
            if not messagebox.askyesno("Quit", "A capture is running. Quit anyway?"):
                return
            self.capture_cancel.set()

        if self.device is not None:
            try:
                self.device.disconnect()
            except Exception:
                pass

        self.root.destroy()


def main() -> int:
    root = tk.Tk()
    app = ValueExtractorApp(root)
    app._log("Ready. Connect device, then save points and export CSV.")
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
