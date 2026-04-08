#!/usr/bin/env python3

"""LUT extractor and validator GUI.

Inputs:
- Polynomial model TXT exported by regression.py
- Experimental CSV (tension_adc_pts, distance_mm)

Features:
- Generate LUT from polynomial using range and step.
- Optional integer extraction with configurable precision in um.
- Convert real distance to press distance using model zero reference.
- Plot LUT curve press_distance=f(tension).
- Save LUT to CSV.
- Save LUT as C array text file.
- Validate LUT against experimental data:
  - Scatter plot (predicted vs experimental) with x=y reference
  - Histogram of absolute errors
  - Summary page with min/max/median/p99 absolute error
"""

from __future__ import annotations

import csv
import datetime as dt
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import numpy as np
except ModuleNotFoundError as exc:
    raise SystemExit("Missing dependency 'numpy'. Install with: pip install numpy") from exc

try:
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
except ModuleNotFoundError as exc:
    raise SystemExit("Missing dependency 'matplotlib'. Install with: pip install matplotlib") from exc

PERCENTILE_LEVELS = [50.0, 75.0, 90.0, 95.0, 99.0, 99.5, 99.9]


@dataclass
class ModelInfo:
    model_name: str
    equation: str
    x_column: str
    y_column: str
    coefficients: np.ndarray


def _split_signed_terms(expression: str) -> List[str]:
    terms: List[str] = []
    current = []

    for idx, char in enumerate(expression):
        if char in "+-" and idx > 0 and expression[idx - 1] not in "eE":
            terms.append("".join(current))
            current = [char]
        else:
            current.append(char)

    if current:
        terms.append("".join(current))

    return [term for term in terms if term]


def _parse_coeff(coeff_text: str) -> float:
    if coeff_text in ("", "+"):
        return 1.0
    if coeff_text == "-":
        return -1.0
    return float(coeff_text)


def parse_polynomial_equation(equation_line: str) -> np.ndarray:
    rhs = equation_line
    if ":" in rhs:
        rhs = rhs.split(":", 1)[1]
    if "=" in rhs:
        rhs = rhs.split("=", 1)[1]

    expr = rhs.strip().replace(" ", "")
    if not expr:
        raise ValueError("Empty equation")

    terms = _split_signed_terms(expr)
    coeff_by_power: Dict[int, float] = {}

    for term in terms:
        if "x" in term:
            coeff_part, power_part = term.split("x", 1)
            coeff = _parse_coeff(coeff_part)

            if power_part.startswith("^"):
                power = int(power_part[1:])
            elif power_part == "":
                power = 1
            else:
                raise ValueError(f"Unsupported polynomial token: {term}")
        else:
            coeff = float(term)
            power = 0

        coeff_by_power[power] = coeff_by_power.get(power, 0.0) + coeff

    if not coeff_by_power:
        raise ValueError("Could not parse polynomial coefficients")

    max_power = max(coeff_by_power.keys())
    coeffs = [coeff_by_power.get(p, 0.0) for p in range(max_power, -1, -1)]
    return np.asarray(coeffs, dtype=float)


def load_model_file(file_path: Path) -> ModelInfo:
    text = file_path.read_text(encoding="utf-8", errors="replace")

    model_name = ""
    equation = ""
    x_column = "tension_adc_pts"
    y_column = "distance_mm"
    coefficients: np.ndarray | None = None

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line.lower().startswith("model:"):
            model_name = line.split(":", 1)[1].strip()
        elif line.lower().startswith("equation:"):
            equation = line.split(":", 1)[1].strip()
        elif line.lower().startswith("coefficients:"):
            rhs = line.split(":", 1)[1]
            values = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", rhs)
            if values:
                coefficients = np.asarray([float(v) for v in values], dtype=float)
        elif line.lower().startswith("x column:"):
            x_column = line.split(":", 1)[1].strip() or x_column
        elif line.lower().startswith("y column:"):
            y_column = line.split(":", 1)[1].strip() or y_column

    if coefficients is None and not equation:
        raise ValueError("No 'Equation:' line found in model file")

    if coefficients is not None:
        coeffs = coefficients
    else:
        coeffs = parse_polynomial_equation(equation)

    if not model_name:
        model_name = "Polynomial"

    return ModelInfo(
        model_name=model_name,
        equation=equation,
        x_column=x_column,
        y_column=y_column,
        coefficients=coeffs,
    )


def parse_float(value: str) -> float | None:
    if value is None:
        return None

    text = str(value).strip()
    if not text:
        return None

    if "," in text and "." not in text:
        text = text.replace(",", ".")
    elif "," in text and "." in text:
        text = text.replace(",", "")

    try:
        val = float(text)
    except ValueError:
        return None

    if not math.isfinite(val):
        return None
    return val


def read_experimental_xy(file_path: Path) -> Tuple[np.ndarray, np.ndarray]:
    with file_path.open("r", newline="", encoding="utf-8-sig") as fh:
        sample = fh.read(4096)
        fh.seek(0)
        try:
            dialect = csv.Sniffer().sniff(sample, delimiters=",;\t")
        except csv.Error:
            dialect = csv.excel

        reader = csv.DictReader(fh, dialect=dialect)
        if not reader.fieldnames:
            raise ValueError("No CSV header found")

        headers = [h.strip() for h in reader.fieldnames]
        lookup = {h.lower(): h for h in headers}

        x_header = lookup.get("tension_adc_pts")
        y_header = lookup.get("distance_mm")

        if x_header is None or y_header is None:
            numeric_counts: Dict[str, int] = {h: 0 for h in headers}
            rows_cache: List[Dict[str, str]] = []
            for row in reader:
                rows_cache.append(row)
                for h in headers:
                    if parse_float(row.get(h, "")) is not None:
                        numeric_counts[h] += 1

            best = sorted(headers, key=lambda h: numeric_counts[h], reverse=True)
            if len(best) < 2:
                raise ValueError("Could not infer two numeric columns in experimental CSV")

            x_header, y_header = best[0], best[1]
            x_values: List[float] = []
            y_values: List[float] = []
            for row in rows_cache:
                xv = parse_float(row.get(x_header, ""))
                yv = parse_float(row.get(y_header, ""))
                if xv is None or yv is None:
                    continue
                x_values.append(xv)
                y_values.append(yv)
        else:
            x_values = []
            y_values = []
            for row in reader:
                xv = parse_float(row.get(x_header, ""))
                yv = parse_float(row.get(y_header, ""))
                if xv is None or yv is None:
                    continue
                x_values.append(xv)
                y_values.append(yv)

    if len(x_values) < 2:
        raise ValueError("Not enough numeric points in experimental CSV")

    return np.asarray(x_values, dtype=float), np.asarray(y_values, dtype=float)


class LUTExtractorApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("LUT Extractor")
        self.root.geometry("1360x900")
        self.root.minsize(1180, 780)

        self.model_path_var = tk.StringVar()
        self.exp_path_var = tk.StringVar()
        self.range_min_var = tk.StringVar(value="2180")
        self.range_max_var = tk.StringVar(value="2850")
        self.step_var = tk.StringVar(value="1")
        self.integer_mode_var = tk.BooleanVar(value=True)
        self.precision_um_var = tk.StringVar(value="1")
        self.status_var = tk.StringVar(value="Charge un modele polynomial TXT pour commencer.")
        self.model_info_var = tk.StringVar(value="Modele: --")

        self.summary_vars = {
            "zero_ref": tk.StringVar(value="--"),
            "min_abs": tk.StringVar(value="--"),
            "max_abs": tk.StringVar(value="--"),
            "mean_abs": tk.StringVar(value="--"),
            "std_abs": tk.StringVar(value="--"),
            "rmse": tk.StringVar(value="--"),
            "median_abs": tk.StringVar(value="--"),
            "p99_abs": tk.StringVar(value="--"),
            "n_points": tk.StringVar(value="--"),
        }

        self.model_info: ModelInfo | None = None
        self.lut_x: np.ndarray | None = None
        self.lut_y_real_mm: np.ndarray | None = None
        self.lut_y_press_mm: np.ndarray | None = None
        self.lut_y_export: np.ndarray | None = None
        self.lut_export_header: str = "press_distance_mm"
        self.zero_ref_real_mm: float | None = None
        self.zero_ref_adc_pts: float | None = None

        self.lut_canvas: FigureCanvasTkAgg | None = None
        self.validation_canvas: FigureCanvasTkAgg | None = None
        self.validation_data: Dict[str, object] | None = None
        self.percentiles_tree: ttk.Treeview | None = None

        self._build_ui()

    def _build_ui(self) -> None:
        self.root.configure(bg="#f4efe6")

        wrapper = ttk.Frame(self.root, padding=14)
        wrapper.pack(fill="both", expand=True)
        wrapper.columnconfigure(0, weight=1)
        wrapper.rowconfigure(2, weight=1)

        title = ttk.Label(wrapper, text="LUT Extractor and Validator", font=("Georgia", 18, "bold"))
        title.grid(row=0, column=0, sticky="w")

        controls = ttk.LabelFrame(wrapper, text="Inputs and extraction settings", padding=12)
        controls.grid(row=1, column=0, sticky="ew", pady=(10, 10))
        for col in range(7):
            controls.columnconfigure(col, weight=1 if col in (1, 4) else 0)

        ttk.Label(controls, text="Model TXT:").grid(row=0, column=0, sticky="w")
        ttk.Entry(controls, textvariable=self.model_path_var).grid(row=0, column=1, columnspan=3, sticky="ew", padx=(8, 8))
        ttk.Button(controls, text="Browse", command=self._browse_model).grid(row=0, column=4, sticky="ew")
        ttk.Button(controls, text="Load model", command=self._load_model).grid(row=0, column=5, sticky="ew", padx=(8, 0))

        ttk.Label(controls, text="Experimental CSV:").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(controls, textvariable=self.exp_path_var).grid(row=1, column=1, columnspan=3, sticky="ew", padx=(8, 8), pady=(8, 0))
        ttk.Button(controls, text="Browse", command=self._browse_experimental).grid(row=1, column=4, sticky="ew", pady=(8, 0))
        ttk.Button(controls, text="Run validation", command=self._run_validation).grid(row=1, column=5, sticky="ew", padx=(8, 0), pady=(8, 0))

        ttk.Label(controls, text="Range min:").grid(row=2, column=0, sticky="w", pady=(10, 0))
        ttk.Entry(controls, textvariable=self.range_min_var, width=12).grid(row=2, column=1, sticky="w", padx=(8, 8), pady=(10, 0))
        ttk.Label(controls, text="Range max:").grid(row=2, column=2, sticky="w", pady=(10, 0))
        ttk.Entry(controls, textvariable=self.range_max_var, width=12).grid(row=2, column=3, sticky="w", padx=(8, 8), pady=(10, 0))
        ttk.Label(controls, text="Step:").grid(row=2, column=4, sticky="e", pady=(10, 0))
        ttk.Entry(controls, textvariable=self.step_var, width=10).grid(row=2, column=5, sticky="w", pady=(10, 0))

        int_frame = ttk.Frame(controls)
        int_frame.grid(row=3, column=0, columnspan=6, sticky="w", pady=(10, 0))
        ttk.Checkbutton(
            int_frame,
            text="Extract integer LUT",
            variable=self.integer_mode_var,
        ).pack(side="left")
        ttk.Label(int_frame, text="Precision (um, integer step):").pack(side="left", padx=(14, 6))
        ttk.Entry(int_frame, textvariable=self.precision_um_var, width=10).pack(side="left")

        action_frame = ttk.Frame(controls)
        action_frame.grid(row=4, column=0, columnspan=6, sticky="w", pady=(10, 0))
        ttk.Button(action_frame, text="Generate LUT curve", command=self._generate_lut).pack(side="left")
        ttk.Button(action_frame, text="Save LUT CSV", command=self._save_lut_csv).pack(side="left", padx=(8, 0))
        ttk.Button(action_frame, text="Save LUT TXT (C array)", command=self._save_lut_txt).pack(side="left", padx=(8, 0))
        ttk.Button(action_frame, text="Save validation report", command=self._save_validation_report).pack(side="left", padx=(8, 0))

        ttk.Label(controls, textvariable=self.model_info_var).grid(row=5, column=0, columnspan=6, sticky="w", pady=(10, 0))

        self.notebook = ttk.Notebook(wrapper)
        self.notebook.grid(row=2, column=0, sticky="nsew")

        self.tab_lut = ttk.Frame(self.notebook)
        self.tab_validation = ttk.Frame(self.notebook)
        self.tab_summary = ttk.Frame(self.notebook)

        self.notebook.add(self.tab_lut, text="LUT curve")
        self.notebook.add(self.tab_validation, text="Validation")
        self.notebook.add(self.tab_summary, text="Summary")

        self._build_summary_tab()

        status = ttk.Label(wrapper, textvariable=self.status_var, anchor="w")
        status.grid(row=3, column=0, sticky="ew", pady=(10, 0))

    def _build_summary_tab(self) -> None:
        frame = ttk.Frame(self.tab_summary, padding=16)
        frame.pack(fill="both", expand=True)

        frame.columnconfigure(1, weight=1)

        ttk.Label(frame, text="Validation summary", font=("Georgia", 15, "bold")).grid(row=0, column=0, columnspan=3, sticky="w")

        ttk.Label(frame, text="Zero reference (real distance):").grid(row=1, column=0, sticky="w", pady=(14, 6))
        ttk.Label(frame, textvariable=self.summary_vars["zero_ref"]).grid(row=1, column=1, sticky="w", pady=(14, 6))

        ttk.Label(frame, text="Points compared:").grid(row=2, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["n_points"]).grid(row=2, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="Absolute error min (press):").grid(row=3, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["min_abs"]).grid(row=3, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="Absolute error max (press):").grid(row=4, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["max_abs"]).grid(row=4, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="Absolute error mean (press):").grid(row=5, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["mean_abs"]).grid(row=5, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="Absolute error std (press):").grid(row=6, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["std_abs"]).grid(row=6, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="RMSE (press):").grid(row=7, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["rmse"]).grid(row=7, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="Absolute error median (press):").grid(row=8, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["median_abs"]).grid(row=8, column=1, sticky="w", pady=6)

        ttk.Label(frame, text="Absolute error P99 (press):").grid(row=9, column=0, sticky="w", pady=6)
        ttk.Label(frame, textvariable=self.summary_vars["p99_abs"]).grid(row=9, column=1, sticky="w", pady=6)

        percentiles_box = ttk.LabelFrame(frame, text="Percentiles (absolute error)", padding=10)
        percentiles_box.grid(row=1, column=2, rowspan=9, sticky="nsew", padx=(24, 0))
        percentiles_box.columnconfigure(0, weight=1)
        percentiles_box.rowconfigure(0, weight=1)

        self.percentiles_tree = ttk.Treeview(
            percentiles_box,
            columns=("p", "mm", "um"),
            show="headings",
            height=10,
        )
        self.percentiles_tree.heading("p", text="Percentile")
        self.percentiles_tree.heading("mm", text="Error (mm)")
        self.percentiles_tree.heading("um", text="Error (um)")
        self.percentiles_tree.column("p", width=100, anchor="center")
        self.percentiles_tree.column("mm", width=120, anchor="center")
        self.percentiles_tree.column("um", width=120, anchor="center")
        self.percentiles_tree.grid(row=0, column=0, sticky="nsew")

        tree_scroll = ttk.Scrollbar(percentiles_box, orient="vertical", command=self.percentiles_tree.yview)
        tree_scroll.grid(row=0, column=1, sticky="ns")
        self.percentiles_tree.configure(yscrollcommand=tree_scroll.set)

    def _browse_model(self) -> None:
        path = filedialog.askopenfilename(
            title="Select polynomial model TXT",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if path:
            self.model_path_var.set(path)

    def _load_model(self) -> None:
        path = Path(self.model_path_var.get().strip())
        if not path.exists() or not path.is_file():
            messagebox.showerror("Invalid model", "Select a valid model TXT file.")
            return

        try:
            model_info = load_model_file(path)
        except Exception as exc:
            messagebox.showerror("Model error", f"Could not load model:\n{exc}")
            return

        self.model_info = model_info
        self.model_info_var.set(
            f"Modele charge: {model_info.model_name} | y = f(x): {model_info.equation}"
        )
        self.status_var.set(f"Model loaded from {path.name}.")

    def _browse_experimental(self) -> None:
        path = filedialog.askopenfilename(
            title="Select experimental CSV",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if path:
            self.exp_path_var.set(path)

    def _get_generation_params(self) -> Tuple[float, float, float, bool, int]:
        try:
            start = float(self.range_min_var.get().strip())
            stop = float(self.range_max_var.get().strip())
            step = float(self.step_var.get().strip())
        except ValueError as exc:
            raise ValueError("Range min/max/step must be numeric") from exc

        if step <= 0:
            raise ValueError("Step must be > 0")
        if stop <= start:
            raise ValueError("Range max must be greater than range min")

        integer_mode = bool(self.integer_mode_var.get())
        precision_um = 1
        if integer_mode:
            try:
                precision_um = int(self.precision_um_var.get().strip())
            except ValueError as exc:
                raise ValueError("Precision um must be an integer") from exc
            if precision_um <= 0:
                raise ValueError("Precision um must be > 0")

        return start, stop, step, integer_mode, precision_um

    def _generate_lut(self) -> None:
        if self.model_info is None:
            messagebox.showwarning("No model", "Load a polynomial model TXT first.")
            return

        try:
            start, stop, step, integer_mode, precision_um = self._get_generation_params()
        except Exception as exc:
            messagebox.showerror("Parameters", str(exc))
            return

        x = np.arange(start, stop + (step * 0.5), step, dtype=float)
        y_real_mm = np.polyval(self.model_info.coefficients, x)

        zero_index = int(np.argmax(y_real_mm))
        zero_ref_real_mm = float(y_real_mm[zero_index])
        zero_ref_adc = float(x[zero_index])

        y_press_mm_raw = np.maximum(0.0, zero_ref_real_mm - y_real_mm)

        if integer_mode:
            y_um_quant = np.rint((y_press_mm_raw * 1000.0) / precision_um) * precision_um
            y_press_mm = y_um_quant / 1000.0
            y_export = y_um_quant.astype(np.int64)
            header = f"press_distance_um_int_p{precision_um}"
        else:
            y_press_mm = y_press_mm_raw
            y_export = y_press_mm_raw
            header = "press_distance_mm"

        self.lut_x = x
        self.lut_y_real_mm = y_real_mm
        self.lut_y_press_mm = y_press_mm
        self.lut_y_export = y_export
        self.lut_export_header = header
        self.zero_ref_real_mm = zero_ref_real_mm
        self.zero_ref_adc_pts = zero_ref_adc
        self.validation_data = None

        self._render_lut_plot(x, y_press_mm)

        self.status_var.set(
            f"LUT generated with {len(x)} points. Zero reference={zero_ref_real_mm:.6f} mm at ADC {zero_ref_adc:.2f}."
        )
        self.notebook.select(self.tab_lut)

    def _render_lut_plot(self, x: np.ndarray, y_mm: np.ndarray) -> None:
        for widget in self.tab_lut.winfo_children():
            widget.destroy()

        fig = Figure(figsize=(9.2, 6.8), dpi=100)
        ax = fig.add_subplot(111)
        ax.plot(x, y_mm, color="#1d3557", linewidth=2.2)
        ax.set_title("Press Distance vs Tension (LUT)")
        ax.set_xlabel("Tension (ADC pts)")
        ax.set_ylabel("Press distance (mm)")
        ax.grid(True, alpha=0.28)

        canvas = FigureCanvasTkAgg(fig, master=self.tab_lut)
        canvas.draw()
        canvas.get_tk_widget().pack(fill="both", expand=True, padx=10, pady=10)
        self.lut_canvas = canvas

    def _save_lut_csv(self) -> None:
        if (
            self.lut_x is None
            or self.lut_y_real_mm is None
            or self.lut_y_press_mm is None
            or self.lut_y_export is None
            or self.zero_ref_real_mm is None
            or self.zero_ref_adc_pts is None
        ):
            messagebox.showwarning("No LUT", "Generate LUT first.")
            return

        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        default_name = f"{stamp}_lut.csv"
        output_path = filedialog.asksaveasfilename(
            title="Save LUT CSV",
            defaultextension=".csv",
            initialfile=default_name,
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if not output_path:
            return

        path = Path(output_path)

        try:
            with path.open("w", newline="", encoding="utf-8") as fh:
                writer = csv.writer(fh)
                writer.writerow([
                    "tension_adc_pts",
                    self.lut_export_header,
                    "press_distance_mm",
                    "distance_real_mm",
                    "zero_ref_real_mm",
                    "zero_ref_adc_pts",
                ])
                for xv, yv_export, y_press_mm, y_real_mm in zip(
                    self.lut_x,
                    self.lut_y_export,
                    self.lut_y_press_mm,
                    self.lut_y_real_mm,
                ):
                    writer.writerow(
                        [
                            f"{xv:.6f}",
                            yv_export,
                            f"{y_press_mm:.6f}",
                            f"{y_real_mm:.6f}",
                            f"{self.zero_ref_real_mm:.6f}",
                            f"{self.zero_ref_adc_pts:.6f}",
                        ]
                    )
        except Exception as exc:
            messagebox.showerror("Save error", f"Could not save LUT CSV:\n{exc}")
            return

        self.status_var.set(f"LUT CSV saved to {path}")
        messagebox.showinfo("Saved", f"LUT CSV saved:\n{path}")

    def _save_lut_txt(self) -> None:
        if (
            self.lut_x is None
            or self.lut_y_press_mm is None
            or self.lut_y_export is None
            or self.zero_ref_real_mm is None
            or self.zero_ref_adc_pts is None
        ):
            messagebox.showwarning("No LUT", "Generate LUT first.")
            return

        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        output_path = filedialog.asksaveasfilename(
            title="Save LUT TXT",
            defaultextension=".txt",
            initialfile=f"{stamp}_lut.txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if not output_path:
            return

        is_integer_lut = np.issubdtype(np.asarray(self.lut_y_export).dtype, np.integer)
        array_name = "lut_values"
        if is_integer_lut:
            c_type = "uint32_t"
            value_formatter = lambda v: str(int(v))
        else:
            c_type = "float"
            value_formatter = lambda v: f"{float(v):.6f}f"

        values_per_line = 12
        formatted_values = [value_formatter(v) for v in np.asarray(self.lut_y_export)]
        value_lines: List[str] = []
        for idx in range(0, len(formatted_values), values_per_line):
            chunk = ", ".join(formatted_values[idx : idx + values_per_line])
            value_lines.append(f"    {chunk}")

        txt_lines = [
            "// Auto-generated LUT (press distance vs ADC)",
            "#include <stdint.h>",
            "",
            f"static const uint32_t LUT_COUNT = {len(self.lut_x)}u;",
            f"static const float LUT_START_ADC = {float(self.lut_x[0]):.6f}f;",
            f"static const float LUT_END_ADC = {float(self.lut_x[-1]):.6f}f;",
            f"static const float LUT_STEP_ADC = {float(self.lut_x[1] - self.lut_x[0]) if len(self.lut_x) > 1 else 0.0:.6f}f;",
            f"static const float LUT_ZERO_REAL_MM = {self.zero_ref_real_mm:.6f}f;",
            f"static const float LUT_ZERO_ADC = {self.zero_ref_adc_pts:.6f}f;",
            "",
            f"static const {c_type} {array_name}[LUT_COUNT] = {{",
            *value_lines,
            "};",
            "",
            "// Press distance conversion:",
            "// press_distance_mm = LUT_ZERO_REAL_MM - real_distance_mm",
        ]

        try:
            Path(output_path).write_text("\n".join(txt_lines) + "\n", encoding="utf-8")
        except Exception as exc:
            messagebox.showerror("Save error", f"Could not save LUT TXT:\n{exc}")
            return

        self.status_var.set(f"LUT TXT saved to {output_path}")
        messagebox.showinfo("Saved", f"LUT TXT saved:\n{output_path}")

    def _run_validation(self) -> None:
        if self.lut_x is None or self.lut_y_press_mm is None or self.zero_ref_real_mm is None:
            messagebox.showwarning("No LUT", "Generate LUT before validation.")
            return

        exp_path = Path(self.exp_path_var.get().strip())
        if not exp_path.exists() or not exp_path.is_file():
            messagebox.showerror("Experimental file", "Select a valid experimental CSV file.")
            return

        try:
            x_exp, y_exp_real = read_experimental_xy(exp_path)
        except Exception as exc:
            messagebox.showerror("Experimental CSV", f"Could not load experimental data:\n{exc}")
            return

        y_exp_press = np.maximum(0.0, self.zero_ref_real_mm - y_exp_real)

        y_pred = np.interp(
            x_exp,
            self.lut_x,
            self.lut_y_press_mm,
            left=float(self.lut_y_press_mm[0]),
            right=float(self.lut_y_press_mm[-1]),
        )

        errors = y_pred - y_exp_press
        abs_errors = np.abs(errors)
        abs_um = abs_errors * 1000.0

        self._render_validation_plot(y_exp_press, y_pred, abs_um)
        summary = self._update_summary(abs_errors, errors, len(y_exp_press))

        self.validation_data = {
            "x_exp": x_exp,
            "y_exp_real_mm": y_exp_real,
            "y_exp_press_mm": y_exp_press,
            "y_pred": y_pred,
            "errors_mm": errors,
            "abs_errors_mm": abs_errors,
            "summary": summary,
            "exp_path": str(exp_path),
            "zero_ref_real_mm": float(self.zero_ref_real_mm),
        }

        self.status_var.set(
            f"Validation completed on {len(y_exp_press)} points from {exp_path.name}."
        )
        self.notebook.select(self.tab_validation)

    def _render_validation_plot(self, y_true: np.ndarray, y_pred: np.ndarray, abs_err_um: np.ndarray) -> None:
        for widget in self.tab_validation.winfo_children():
            widget.destroy()

        fig = Figure(figsize=(10.2, 6.8), dpi=100)
        ax1 = fig.add_subplot(121)
        ax2 = fig.add_subplot(122)

        ax1.scatter(y_true, y_pred, s=22, alpha=0.8, color="#2a9d8f")
        low = min(float(np.min(y_true)), float(np.min(y_pred)))
        high = max(float(np.max(y_true)), float(np.max(y_pred)))
        ax1.plot([low, high], [low, high], color="#e63946", linewidth=2.0, label="x = y")
        ax1.set_title("Predicted vs Experimental (Press Distance)")
        ax1.set_xlabel("Experimental press distance (mm)")
        ax1.set_ylabel("LUT press distance (mm)")
        ax1.grid(True, alpha=0.25)
        ax1.legend()

        ax2.hist(abs_err_um, bins=30, color="#457b9d", alpha=0.85, edgecolor="#1d3557")
        ax2.set_title("Absolute error frequency")
        ax2.set_xlabel("Absolute error (um)")
        ax2.set_ylabel("Frequency")
        ax2.grid(True, alpha=0.25)

        fig.tight_layout()

        canvas = FigureCanvasTkAgg(fig, master=self.tab_validation)
        canvas.draw()
        canvas.get_tk_widget().pack(fill="both", expand=True, padx=10, pady=10)
        self.validation_canvas = canvas

    @staticmethod
    def _format_mm_um(value_mm: float) -> str:
        return f"{value_mm:.6f} mm  |  {value_mm * 1000.0:.1f} um"

    @staticmethod
    def _percentile_label(level: float) -> str:
        if float(level).is_integer():
            return f"P{int(level)}"
        return f"P{level}"

    def _update_summary(self, abs_errors_mm: np.ndarray, errors_mm: np.ndarray, n_points: int) -> Dict[str, object]:
        min_abs = float(np.min(abs_errors_mm))
        max_abs = float(np.max(abs_errors_mm))
        mean_abs = float(np.mean(abs_errors_mm))
        std_abs = float(np.std(abs_errors_mm))
        rmse = float(np.sqrt(np.mean(errors_mm ** 2)))
        median_abs = float(np.median(abs_errors_mm))
        p99_abs = float(np.percentile(abs_errors_mm, 99))
        percentile_values = {float(p): float(np.percentile(abs_errors_mm, p)) for p in PERCENTILE_LEVELS}

        if self.zero_ref_real_mm is not None and self.zero_ref_adc_pts is not None:
            self.summary_vars["zero_ref"].set(
                f"{self.zero_ref_real_mm:.6f} mm at ADC {self.zero_ref_adc_pts:.3f}"
            )
        else:
            self.summary_vars["zero_ref"].set("--")

        self.summary_vars["n_points"].set(str(n_points))
        self.summary_vars["min_abs"].set(self._format_mm_um(min_abs))
        self.summary_vars["max_abs"].set(self._format_mm_um(max_abs))
        self.summary_vars["mean_abs"].set(self._format_mm_um(mean_abs))
        self.summary_vars["std_abs"].set(self._format_mm_um(std_abs))
        self.summary_vars["rmse"].set(self._format_mm_um(rmse))
        self.summary_vars["median_abs"].set(self._format_mm_um(median_abs))
        self.summary_vars["p99_abs"].set(self._format_mm_um(p99_abs))

        if self.percentiles_tree is not None:
            for item in self.percentiles_tree.get_children():
                self.percentiles_tree.delete(item)
            for level in PERCENTILE_LEVELS:
                value_mm = percentile_values[level]
                self.percentiles_tree.insert(
                    "",
                    "end",
                    values=(
                        self._percentile_label(level),
                        f"{value_mm:.6f}",
                        f"{value_mm * 1000.0:.1f}",
                    ),
                )

        return {
            "n_points": n_points,
            "min_abs_mm": min_abs,
            "max_abs_mm": max_abs,
            "mean_abs_mm": mean_abs,
            "std_abs_mm": std_abs,
            "rmse_mm": rmse,
            "median_abs_mm": median_abs,
            "p99_abs_mm": p99_abs,
            "percentiles_mm": percentile_values,
        }

    def _save_validation_report(self) -> None:
        if self.validation_data is None:
            messagebox.showwarning("No validation", "Run validation before saving a report.")
            return

        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        default_name = f"{stamp}_validation_report.txt"
        out_path = filedialog.asksaveasfilename(
            title="Save validation report",
            defaultextension=".txt",
            initialfile=default_name,
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if not out_path:
            return

        summary = self.validation_data["summary"]
        assert isinstance(summary, dict)
        percentiles_mm = summary.get("percentiles_mm", {})
        assert isinstance(percentiles_mm, dict)

        report_lines = [
            "Validation report",
            "=================",
            f"Timestamp: {stamp}",
            f"Model file: {self.model_path_var.get().strip()}",
            f"Experimental CSV: {self.validation_data.get('exp_path', '')}",
            f"Model name: {self.model_info.model_name if self.model_info else '--'}",
            f"Equation: {self.model_info.equation if self.model_info else '--'}",
            f"Zero reference (real): {float(self.validation_data.get('zero_ref_real_mm', float('nan'))):.6f} mm",
            "",
            "LUT extraction settings",
            f"Range min: {self.range_min_var.get().strip()}",
            f"Range max: {self.range_max_var.get().strip()}",
            f"Step: {self.step_var.get().strip()}",
            f"Integer mode: {bool(self.integer_mode_var.get())}",
            f"Integer precision um: {self.precision_um_var.get().strip()}",
            "",
            "Error summary (absolute, press distance)",
            f"Points compared: {summary.get('n_points', '--')}",
            f"Min: {self._format_mm_um(float(summary.get('min_abs_mm', float('nan'))))}",
            f"Max: {self._format_mm_um(float(summary.get('max_abs_mm', float('nan'))))}",
            f"Mean: {self._format_mm_um(float(summary.get('mean_abs_mm', float('nan'))))}",
            f"Std: {self._format_mm_um(float(summary.get('std_abs_mm', float('nan'))))}",
            f"RMSE: {self._format_mm_um(float(summary.get('rmse_mm', float('nan'))))}",
            f"Median: {self._format_mm_um(float(summary.get('median_abs_mm', float('nan'))))}",
            f"P99: {self._format_mm_um(float(summary.get('p99_abs_mm', float('nan'))))}",
            "",
            "Percentiles (absolute error, press distance)",
        ]

        for level in PERCENTILE_LEVELS:
            value_mm = float(percentiles_mm.get(level, float("nan")))
            report_lines.append(f"{self._percentile_label(level)}: {self._format_mm_um(value_mm)}")

        report_path = Path(out_path)
        errors_csv_path = report_path.with_name(f"{report_path.stem}_errors.csv")

        try:
            report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")

            x_exp = np.asarray(self.validation_data["x_exp"], dtype=float)
            y_exp_real = np.asarray(self.validation_data["y_exp_real_mm"], dtype=float)
            y_exp_press = np.asarray(self.validation_data["y_exp_press_mm"], dtype=float)
            y_pred_press = np.asarray(self.validation_data["y_pred"], dtype=float)
            errors = np.asarray(self.validation_data["errors_mm"], dtype=float)
            abs_errors = np.asarray(self.validation_data["abs_errors_mm"], dtype=float)

            with errors_csv_path.open("w", newline="", encoding="utf-8") as fh:
                writer = csv.writer(fh)
                writer.writerow([
                    "index",
                    "tension_adc_pts",
                    "distance_real_exp_mm",
                    "distance_press_exp_mm",
                    "distance_press_pred_mm",
                    "error_press_mm",
                    "abs_error_mm",
                    "abs_error_um",
                ])
                for idx, (xv, y_real, y_true_press, y_hat_press, err, abs_err) in enumerate(
                    zip(x_exp, y_exp_real, y_exp_press, y_pred_press, errors, abs_errors)
                ):
                    writer.writerow([
                        idx,
                        f"{xv:.6f}",
                        f"{y_real:.6f}",
                        f"{y_true_press:.6f}",
                        f"{y_hat_press:.6f}",
                        f"{err:.6f}",
                        f"{abs_err:.6f}",
                        f"{abs_err * 1000.0:.3f}",
                    ])
        except Exception as exc:
            messagebox.showerror("Save report", f"Could not save report:\n{exc}")
            return

        self.status_var.set(f"Validation report saved: {report_path}")
        messagebox.showinfo(
            "Report saved",
            f"Report TXT:\n{report_path}\n\nDetailed errors CSV:\n{errors_csv_path}",
        )


def main() -> int:
    root = tk.Tk()
    LUTExtractorApp(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
