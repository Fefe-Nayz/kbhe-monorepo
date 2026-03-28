from __future__ import annotations

from .common import QFrame, QScrollArea, QVBoxLayout, QWidget, Qt


class PageBase(QWidget):
    page_title = ""
    page_subtitle = ""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.session = session

    def reload(self):
        return None

    def on_selected_key_changed(self, key_index: int):
        del key_index

    def on_page_activated(self):
        return None

    def on_page_deactivated(self):
        return None


class ScrollPage(PageBase):
    def __init__(self, session, parent=None):
        super().__init__(session, parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.scroll_area.setFrameShape(QFrame.Shape.NoFrame)
        root.addWidget(self.scroll_area)

        self.content = QWidget()
        self.scroll_area.setWidget(self.content)
        self.content_layout = QVBoxLayout(self.content)
        self.content_layout.setContentsMargins(0, 0, 0, 0)
        self.content_layout.setSpacing(16)
