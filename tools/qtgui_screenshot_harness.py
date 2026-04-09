from __future__ import annotations

from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication

from kbhe_tool.demo import DemoDevice
from kbhe_tool.qtgui.app import KBHEQtMainWindow


def capture_all(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    app = QApplication.instance() or QApplication([])
    window = KBHEQtMainWindow(DemoDevice())
    window.resize(1500, 980)
    window.show()
    app.processEvents()

    for page_id in list(window.pages.keys()):
        window.show_page(page_id)
        app.processEvents()
        QTimer.singleShot(50, lambda: None)
        app.processEvents()
        window.grab().save(str(output_dir / f"{page_id}.png"))

    window.close()
    app.processEvents()


if __name__ == "__main__":
    capture_all(REPO_ROOT / "build" / "ui-screenshots")
