#!/usr/bin/env python3
"""Verify and apply the pinned fork delta to a clean upstream checkout.

The maintained integration lives on the fork branch recorded in
``upstream.lock.json``. This helper is only the reproducible offline mirror.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parent
LOCK = json.loads((ROOT / "upstream.lock.json").read_text(encoding="utf-8"))
PATCH = ROOT / LOCK["patch"]["file"]


def run(command: list[str], cwd: pathlib.Path, *, capture: bool = False) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    return result.stdout.strip() if capture else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=pathlib.Path, help="clean pinned libhmk checkout")
    parser.add_argument("--check", action="store_true", help="verify without applying")
    parser.add_argument("--build", action="store_true", help="build kbhe-75he after apply")
    args = parser.parse_args()
    repo = args.repo.resolve()

    expected_hash = LOCK["patch"]["sha256"]
    actual_hash = hashlib.sha256(PATCH.read_bytes()).hexdigest()
    if actual_hash != expected_hash:
        parser.error(f"patch integrity mismatch: {actual_hash} != {expected_hash}")

    try:
        head = run(["git", "rev-parse", "HEAD"], repo, capture=True)
        expected_head = LOCK["upstream"]["commit"]
        if head != expected_head:
            parser.error(f"libhmk HEAD is {head}; expected pinned {expected_head}")
        if run(["git", "status", "--porcelain"], repo, capture=True):
            parser.error("libhmk checkout must be clean")

        run(["git", "apply", "--check", "--whitespace=error", str(PATCH)], repo)
        if args.check:
            print("patch, pin and clean checkout verified")
            return 0

        run(["git", "apply", "--whitespace=error", str(PATCH)], repo)
        print("KBHE/libhmk patch applied")
        if args.build:
            run([sys.executable, "setup.py", "-k", "kbhe-75he"], repo)
            run([sys.executable, "-m", "platformio", "run"], repo)
        return 0
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"apply_patch.py: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
