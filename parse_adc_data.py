import argparse
import csv
import re
from pathlib import Path


PATTERN = re.compile(r"\[(\d+)\]:\s*(\d+)")


def parse_txt_to_rows(txt_path: Path):
    rows = []
    try:
        lines = txt_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception as e:
        print(f"Error reading file '{txt_path}': {e}")
        return rows

    for line in lines:
        match = PATTERN.search(line)
        if match:
            rows.append((match.group(1), match.group(2)))

    return rows


def convert_one(txt_path: Path, overwrite: bool) -> bool:
    csv_path = txt_path.with_suffix(".csv")

    if csv_path.exists() and not overwrite:
        print(f"Skip (exists): {csv_path}")
        return False

    rows = parse_txt_to_rows(txt_path)
    if not rows:
        print(f"Skip (no matches): {txt_path}")
        return False

    try:
        with csv_path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["Index", "Value"])
            writer.writerows(rows)
        print(f"Converted {len(rows)} rows: {txt_path.name} -> {csv_path.name}")
        return True
    except Exception as e:
        print(f"Error writing CSV '{csv_path}': {e}")
        return False


def find_txt_files(root: Path, recursive: bool):
    if recursive:
        return sorted(p for p in root.rglob("*.txt") if p.is_file())
    return sorted(p for p in root.glob("*.txt") if p.is_file())


def main():
    parser = argparse.ArgumentParser(description="Convert ADC .txt dumps into .csv files")
    parser.add_argument("path", nargs="?", default=".", help="Directory to scan (default: current)")
    parser.add_argument("--recursive", action="store_true", help="Scan recursively")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing .csv")
    args = parser.parse_args()

    root = Path(args.path).resolve()
    if not root.exists():
        print(f"Error: '{root}' does not exist")
        return 2
    if not root.is_dir():
        # If a file is provided, just convert that file.
        txt_path = root
        if txt_path.suffix.lower() != ".txt":
            print(f"Error: expected a .txt file, got '{txt_path.name}'")
            return 2
        convert_one(txt_path, overwrite=args.overwrite)
        return 0

    txt_files = find_txt_files(root, recursive=args.recursive)
    if not txt_files:
        print(f"No .txt files found in {root}")
        return 0

    converted = 0
    for txt_path in txt_files:
        if convert_one(txt_path, overwrite=args.overwrite):
            converted += 1

    print(f"Done. Converted {converted}/{len(txt_files)} files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
