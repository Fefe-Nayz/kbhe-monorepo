#!/usr/bin/env python3

"""GUI regression tool for calibration CSV files.

Features:
- Load a CSV file and choose X/Y columns.
- Run multiple regressions:
  - Polynomial degree 1 to 10
  - Logarithmic
  - Exponential
  - Power
  - Inverse
- For each regression, display:
  - Equation
  - Pearson correlation coefficient r
  - R^2
  - Plot (scatter + fitted curve)
"""

from __future__ import annotations

import csv
import datetime as dt
import math
import re
import warnings
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


@dataclass
class RegressionResult:
    name: str
    equation: str
    r: float
    r2: float
    n_points: int
    x_used: np.ndarray
    y_used: np.ndarray
    x_curve: np.ndarray
    y_curve: np.ndarray
    coefficients: np.ndarray | None = None


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
        number = float(text)
    except ValueError:
        return None

    if not math.isfinite(number):
        return None
    return number


def pearson_r(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    if len(y_true) < 2 or len(y_pred) < 2:
        return float("nan")
    std_true = float(np.std(y_true))
    std_pred = float(np.std(y_pred))
    if std_true <= 0.0 or std_pred <= 0.0:
        return float("nan")
    return float(np.corrcoef(y_true, y_pred)[0, 1])


def r2_score(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    if len(y_true) == 0:
        return float("nan")
    ss_res = float(np.sum((y_true - y_pred) ** 2))
    ss_tot = float(np.sum((y_true - np.mean(y_true)) ** 2))
    if ss_tot == 0.0:
        return float("nan")
    return 1.0 - (ss_res / ss_tot)


def format_num(value: float) -> str:
    if abs(value) >= 1e4 or (abs(value) > 0 and abs(value) < 1e-3):
        return f"{value:.3e}"
    return f"{value:.6f}".rstrip("0").rstrip(".")


def format_signed(value: float) -> str:
    sign = "+" if value >= 0 else "-"
    return f" {sign} {format_num(abs(value))}"


def polynomial_equation(coeffs: np.ndarray) -> str:
    degree = len(coeffs) - 1
    parts: List[str] = []

    for index, coeff in enumerate(coeffs):
        power = degree - index
        if float(coeff) == 0.0:
            continue

        abs_coeff = abs(float(coeff))
        if power == 0:
            core = format_num(abs_coeff)
        elif power == 1:
            core = "x" if abs(abs_coeff - 1.0) < 1e-12 else f"{format_num(abs_coeff)}x"
        else:
            core = f"x^{power}" if abs(abs_coeff - 1.0) < 1e-12 else f"{format_num(abs_coeff)}x^{power}"

        if not parts:
            if coeff < 0:
                parts.append(f"-{core}")
            else:
                parts.append(core)
        else:
            parts.append(f"{'-' if coeff < 0 else '+'} {core}")

    if not parts:
        return "y = 0"
    return "y = " + " ".join(parts)


def fit_polynomial(x: np.ndarray, y: np.ndarray, degree: int) -> RegressionResult | None:
    if len(x) < degree + 1:
        return None

    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        coeffs = np.polyfit(x, y, degree)

    poly = np.poly1d(coeffs)
    y_pred = poly(x)

    x_curve = np.linspace(np.min(x), np.max(x), 400)
    y_curve = poly(x_curve)

    return RegressionResult(
        name=f"Polynomial d={degree}",
        equation=polynomial_equation(coeffs),
        r=pearson_r(y, y_pred),
        r2=r2_score(y, y_pred),
        n_points=len(x),
        x_used=x,
        y_used=y,
        x_curve=x_curve,
        y_curve=y_curve,
        coefficients=coeffs,
    )


def fit_logarithmic(x: np.ndarray, y: np.ndarray) -> RegressionResult | None:
    mask = x > 0
    x_valid = x[mask]
    y_valid = y[mask]
    if len(x_valid) < 2:
        return None

    X = np.column_stack((np.log(x_valid), np.ones_like(x_valid)))
    a, b = np.linalg.lstsq(X, y_valid, rcond=None)[0]

    y_pred = a * np.log(x_valid) + b
    x_curve = np.linspace(np.min(x_valid), np.max(x_valid), 400)
    y_curve = a * np.log(x_curve) + b

    return RegressionResult(
        name="Logarithmic",
        equation=f"y = {format_num(float(a))} ln(x){format_signed(float(b))}",
        r=pearson_r(y_valid, y_pred),
        r2=r2_score(y_valid, y_pred),
        n_points=len(x_valid),
        x_used=x_valid,
        y_used=y_valid,
        x_curve=x_curve,
        y_curve=y_curve,
        coefficients=None,
    )


def fit_exponential(x: np.ndarray, y: np.ndarray) -> RegressionResult | None:
    mask = y > 0
    x_valid = x[mask]
    y_valid = y[mask]
    if len(x_valid) < 2:
        return None

    X = np.column_stack((x_valid, np.ones_like(x_valid)))
    b, c = np.linalg.lstsq(X, np.log(y_valid), rcond=None)[0]
    a = float(np.exp(c))

    y_pred = a * np.exp(b * x_valid)
    x_curve = np.linspace(np.min(x_valid), np.max(x_valid), 400)
    y_curve = a * np.exp(b * x_curve)

    return RegressionResult(
        name="Exponential",
        equation=f"y = {format_num(a)} * exp({format_num(float(b))}x)",
        r=pearson_r(y_valid, y_pred),
        r2=r2_score(y_valid, y_pred),
        n_points=len(x_valid),
        x_used=x_valid,
        y_used=y_valid,
        x_curve=x_curve,
        y_curve=y_curve,
        coefficients=None,
    )


def fit_power(x: np.ndarray, y: np.ndarray) -> RegressionResult | None:
    mask = (x > 0) & (y > 0)
    x_valid = x[mask]
    y_valid = y[mask]
    if len(x_valid) < 2:
        return None

    X = np.column_stack((np.log(x_valid), np.ones_like(x_valid)))
    b, c = np.linalg.lstsq(X, np.log(y_valid), rcond=None)[0]
    a = float(np.exp(c))

    y_pred = a * (x_valid ** b)
    x_curve = np.linspace(np.min(x_valid), np.max(x_valid), 400)
    y_curve = a * (x_curve ** b)

    return RegressionResult(
        name="Power",
        equation=f"y = {format_num(a)} * x^{format_num(float(b))}",
        r=pearson_r(y_valid, y_pred),
        r2=r2_score(y_valid, y_pred),
        n_points=len(x_valid),
        x_used=x_valid,
        y_used=y_valid,
        x_curve=x_curve,
        y_curve=y_curve,
        coefficients=None,
    )


def fit_inverse(x: np.ndarray, y: np.ndarray) -> RegressionResult | None:
    mask = np.abs(x) > 1e-12
    x_valid = x[mask]
    y_valid = y[mask]
    if len(x_valid) < 2:
        return None

    inv_x = 1.0 / x_valid
    X = np.column_stack((inv_x, np.ones_like(inv_x)))
    b, a = np.linalg.lstsq(X, y_valid, rcond=None)[0]

    y_pred = a + (b / x_valid)
    x_curve = np.linspace(np.min(x_valid), np.max(x_valid), 500)
    x_curve = x_curve[np.abs(x_curve) > 1e-12]
    if len(x_curve) == 0:
        x_curve = np.sort(x_valid)
    y_curve = a + (b / x_curve)

    return RegressionResult(
        name="Inverse",
        equation=f"y = {format_num(float(a))}{format_signed(float(b))}/x",
        r=pearson_r(y_valid, y_pred),
        r2=r2_score(y_valid, y_pred),
        n_points=len(x_valid),
        x_used=x_valid,
        y_used=y_valid,
        x_curve=x_curve,
        y_curve=y_curve,
        coefficients=None,
    )


class RegressionApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Regression")
        self.root.geometry("1280x860")
        self.root.minsize(1100, 760)

        self.file_path_var = tk.StringVar(value="")
        self.x_column_var = tk.StringVar()
        self.y_column_var = tk.StringVar()
        self.status_var = tk.StringVar(value="Load a CSV file to begin.")

        self.raw_columns: Dict[str, List[str]] = {}
        self.results: List[RegressionResult] = []
        self._canvases: List[FigureCanvasTkAgg] = []

        self._build_ui()

    def _build_ui(self) -> None:
        self.root.configure(bg="#f3efe7")

        wrapper = ttk.Frame(self.root, padding=14)
        wrapper.pack(fill="both", expand=True)
        wrapper.columnconfigure(0, weight=1)
        wrapper.rowconfigure(2, weight=1)

        title = ttk.Label(wrapper, text="Regression Studio", font=("Georgia", 17, "bold"))
        title.grid(row=0, column=0, sticky="w")

        controls = ttk.LabelFrame(wrapper, text="Data source", padding=12)
        controls.grid(row=1, column=0, sticky="ew", pady=(10, 10))
        controls.columnconfigure(1, weight=1)

        ttk.Label(controls, text="CSV file:").grid(row=0, column=0, sticky="w")
        file_entry = ttk.Entry(controls, textvariable=self.file_path_var)
        file_entry.grid(row=0, column=1, sticky="ew", padx=(8, 8))

        browse_btn = ttk.Button(controls, text="Browse", command=self._browse_file)
        browse_btn.grid(row=0, column=2, sticky="ew")

        load_btn = ttk.Button(controls, text="Load", command=self._load_csv)
        load_btn.grid(row=0, column=3, sticky="ew", padx=(8, 0))

        ttk.Label(controls, text="X column:").grid(row=1, column=0, sticky="w", pady=(10, 0))
        self.x_column_combo = ttk.Combobox(controls, textvariable=self.x_column_var, state="readonly")
        self.x_column_combo.grid(row=1, column=1, sticky="ew", padx=(8, 8), pady=(10, 0))

        ttk.Label(controls, text="Y column:").grid(row=1, column=2, sticky="w", pady=(10, 0))
        self.y_column_combo = ttk.Combobox(controls, textvariable=self.y_column_var, state="readonly")
        self.y_column_combo.grid(row=1, column=3, sticky="ew", pady=(10, 0))

        run_btn = ttk.Button(controls, text="Run regressions", command=self._run_regressions)
        run_btn.grid(row=1, column=4, sticky="ew", padx=(10, 0), pady=(10, 0))

        save_btn = ttk.Button(controls, text="Save selected to TXT", command=self._save_selected_regression)
        save_btn.grid(row=1, column=5, sticky="ew", padx=(8, 0), pady=(10, 0))

        content = ttk.Frame(wrapper)
        content.grid(row=2, column=0, sticky="nsew")
        content.columnconfigure(0, weight=0)
        content.columnconfigure(1, weight=1)
        content.rowconfigure(0, weight=1)

        self.summary_tree = ttk.Treeview(
            content,
            columns=("model", "r", "r2", "n", "equation"),
            show="headings",
            height=24,
        )
        self.summary_tree.heading("model", text="Model")
        self.summary_tree.heading("r", text="r")
        self.summary_tree.heading("r2", text="R^2")
        self.summary_tree.heading("n", text="n")
        self.summary_tree.heading("equation", text="Equation")

        self.summary_tree.column("model", width=180, anchor="w")
        self.summary_tree.column("r", width=80, anchor="center")
        self.summary_tree.column("r2", width=80, anchor="center")
        self.summary_tree.column("n", width=70, anchor="center")
        self.summary_tree.column("equation", width=450, anchor="w")

        self.summary_tree.grid(row=0, column=0, sticky="ns")
        self.summary_tree.bind("<<TreeviewSelect>>", self._select_tab_from_tree)

        table_scroll = ttk.Scrollbar(content, orient="vertical", command=self.summary_tree.yview)
        table_scroll.grid(row=0, column=0, sticky="nse")
        self.summary_tree.configure(yscrollcommand=table_scroll.set)

        self.notebook = ttk.Notebook(content)
        self.notebook.grid(row=0, column=1, sticky="nsew", padx=(12, 0))

        status_bar = ttk.Label(wrapper, textvariable=self.status_var, anchor="w")
        status_bar.grid(row=3, column=0, sticky="ew", pady=(10, 0))

    def _browse_file(self) -> None:
        path = filedialog.askopenfilename(
            title="Select CSV file",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if path:
            self.file_path_var.set(path)
            self._load_csv()

    def _load_csv(self) -> None:
        file_path = Path(self.file_path_var.get().strip())
        if not file_path.exists() or not file_path.is_file():
            messagebox.showerror("Invalid file", "Please select a valid CSV file.")
            return

        try:
            columns = self._read_csv_columns(file_path)
        except Exception as exc:
            messagebox.showerror("CSV error", f"Failed to load CSV:\n{exc}")
            return

        if len(columns) < 2:
            messagebox.showerror("CSV error", "CSV must contain at least two columns.")
            return

        self.raw_columns = columns
        names = list(columns.keys())

        self.x_column_combo.configure(values=names)
        self.y_column_combo.configure(values=names)

        if not self.x_column_var.get() or self.x_column_var.get() not in columns:
            self.x_column_var.set(names[0])
        if not self.y_column_var.get() or self.y_column_var.get() not in columns:
            self.y_column_var.set(names[1] if len(names) > 1 else names[0])

        self.status_var.set(f"Loaded {file_path.name} with {len(names)} columns.")

    def _read_csv_columns(self, file_path: Path) -> Dict[str, List[str]]:
        with file_path.open("r", newline="", encoding="utf-8-sig") as fh:
            sample = fh.read(4096)
            fh.seek(0)
            try:
                dialect = csv.Sniffer().sniff(sample, delimiters=",;\t")
            except csv.Error:
                dialect = csv.excel

            reader = csv.DictReader(fh, dialect=dialect)
            if not reader.fieldnames:
                raise ValueError("No header row found")

            columns: Dict[str, List[str]] = {name: [] for name in reader.fieldnames}
            for row in reader:
                for name in reader.fieldnames:
                    columns[name].append(row.get(name, ""))

            return columns

    def _numeric_xy(self, x_name: str, y_name: str) -> Tuple[np.ndarray, np.ndarray, int]:
        x_raw = self.raw_columns.get(x_name)
        y_raw = self.raw_columns.get(y_name)
        if x_raw is None or y_raw is None:
            return np.array([]), np.array([]), 0

        x_values: List[float] = []
        y_values: List[float] = []
        skipped = 0

        for x_text, y_text in zip(x_raw, y_raw):
            x_val = parse_float(x_text)
            y_val = parse_float(y_text)
            if x_val is None or y_val is None:
                skipped += 1
                continue
            x_values.append(x_val)
            y_values.append(y_val)

        return np.asarray(x_values, dtype=float), np.asarray(y_values, dtype=float), skipped

    def _run_regressions(self) -> None:
        if not self.raw_columns:
            messagebox.showwarning("No data", "Load a CSV file first.")
            return

        x_name = self.x_column_var.get().strip()
        y_name = self.y_column_var.get().strip()
        if not x_name or not y_name:
            messagebox.showwarning("Columns", "Select X and Y columns.")
            return

        x, y, skipped = self._numeric_xy(x_name, y_name)
        if len(x) < 2:
            messagebox.showerror(
                "Not enough data",
                "Not enough numeric data pairs in selected columns.",
            )
            return

        self.results.clear()

        for degree in range(1, 11):
            model = fit_polynomial(x, y, degree)
            if model is not None:
                self.results.append(model)

        for fit_fn in (fit_logarithmic, fit_exponential, fit_power, fit_inverse):
            model = fit_fn(x, y)
            if model is not None:
                self.results.append(model)

        if not self.results:
            messagebox.showerror("Regression", "No model could be fitted with this data.")
            return

        self._refresh_summary()
        self._render_tabs()

        self.status_var.set(
            f"Computed {len(self.results)} regressions on {len(x)} points "
            f"({skipped} skipped rows)."
        )

    def _refresh_summary(self) -> None:
        for item in self.summary_tree.get_children():
            self.summary_tree.delete(item)

        for idx, result in enumerate(self.results):
            r_text = "nan" if math.isnan(result.r) else f"{result.r:.6f}"
            r2_text = "nan" if math.isnan(result.r2) else f"{result.r2:.6f}"
            self.summary_tree.insert(
                "",
                "end",
                iid=str(idx),
                values=(result.name, r_text, r2_text, result.n_points, result.equation),
            )

    def _render_tabs(self) -> None:
        for tab_id in self.notebook.tabs():
            self.notebook.forget(tab_id)
        self._canvases.clear()

        for idx, result in enumerate(self.results):
            tab = ttk.Frame(self.notebook)
            self.notebook.add(tab, text=result.name)

            info = ttk.Label(
                tab,
                text=(
                    f"Equation: {result.equation}\n"
                    f"r = {result.r:.6f}    R^2 = {result.r2:.6f}    n = {result.n_points}"
                ),
                justify="left",
            )
            info.pack(anchor="w", padx=10, pady=(10, 2))

            fig = Figure(figsize=(8.2, 5.6), dpi=100)
            ax = fig.add_subplot(111)

            ax.scatter(result.x_used, result.y_used, color="#274c77", s=22, alpha=0.8, label="Data")

            order = np.argsort(result.x_curve)
            curve_x = result.x_curve[order]
            curve_y = result.y_curve[order]
            ax.plot(curve_x, curve_y, color="#d7263d", linewidth=2.2, label="Fit")

            ax.set_title(result.name)
            ax.set_xlabel(self.x_column_var.get())
            ax.set_ylabel(self.y_column_var.get())
            ax.grid(True, alpha=0.25)
            ax.legend()

            canvas = FigureCanvasTkAgg(fig, master=tab)
            canvas.draw()
            canvas.get_tk_widget().pack(fill="both", expand=True, padx=10, pady=(0, 10))
            self._canvases.append(canvas)

        if self.results:
            self.notebook.select(0)

    def _select_tab_from_tree(self, _event=None) -> None:
        selected = self.summary_tree.selection()
        if not selected:
            return

        try:
            index = int(selected[0])
        except ValueError:
            return

        tabs = self.notebook.tabs()
        if 0 <= index < len(tabs):
            self.notebook.select(index)

    def _get_selected_result(self) -> RegressionResult | None:
        if not self.results:
            return None

        selected = self.summary_tree.selection()
        if selected:
            try:
                index = int(selected[0])
                if 0 <= index < len(self.results):
                    return self.results[index]
            except ValueError:
                pass

        current_tab = self.notebook.select()
        tabs = self.notebook.tabs()
        if current_tab in tabs:
            index = tabs.index(current_tab)
            if 0 <= index < len(self.results):
                return self.results[index]

        return None

    def _save_selected_regression(self) -> None:
        result = self._get_selected_result()
        if result is None:
            messagebox.showwarning("No selection", "Run regressions and select one model first.")
            return

        timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        safe_model = re.sub(r"[^A-Za-z0-9_-]+", "_", result.name).strip("_") or "model"
        default_name = f"{timestamp}_{safe_model}.txt"

        source_name = Path(self.file_path_var.get().strip()).name or "regression"
        output_path = filedialog.asksaveasfilename(
            title="Save selected regression",
            defaultextension=".txt",
            initialfile=default_name,
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if not output_path:
            return

        lines = [
            "Regression result",
            "=================",
            f"Timestamp: {timestamp}",
            f"Source CSV: {source_name}",
            f"X column: {self.x_column_var.get()}",
            f"Y column: {self.y_column_var.get()}",
            "",
            f"Model: {result.name}",
            f"Equation: {result.equation}",
            f"r: {result.r:.6f}",
            f"R^2: {result.r2:.6f}",
            f"Points used: {result.n_points}",
        ]

        if result.coefficients is not None:
            coeff_text = ", ".join(f"{float(c):.18e}" for c in result.coefficients)
            lines.append(f"Coefficients: [{coeff_text}]")

        try:
            Path(output_path).write_text("\n".join(lines) + "\n", encoding="utf-8")
        except Exception as exc:
            messagebox.showerror("Save error", f"Failed to write TXT file:\n{exc}")
            return

        self.status_var.set(f"Saved regression to {output_path}")
        messagebox.showinfo("Saved", f"Selected regression saved to:\n{output_path}")


def main() -> int:
    root = tk.Tk()
    RegressionApp(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
