from __future__ import annotations

from ..common import QTabWidget, QVBoxLayout, QWidget, Signal


class WorkspacePage(QWidget):
    tabChanged = Signal(str)

    def __init__(self, tabs: list[tuple[str, str, QWidget]], parent=None):
        super().__init__(parent)
        self._page_active = False
        self._active_tab_id = tabs[0][0] if tabs else None
        self._tab_order = []
        self._pages = {}

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        self.tabs = QTabWidget()
        self.tabs.setDocumentMode(True)
        self.tabs.setMovable(False)
        self.tabs.setUsesScrollButtons(False)
        self.tabs.currentChanged.connect(self._on_tab_changed)
        root.addWidget(self.tabs, 1)

        for tab_id, title, page in tabs:
            self._tab_order.append(tab_id)
            self._pages[tab_id] = page
            self.tabs.addTab(page, title)

    def active_tab_id(self) -> str | None:
        return self._active_tab_id

    def set_current_tab(self, tab_id: str) -> None:
        if tab_id not in self._pages:
            return
        self.tabs.setCurrentIndex(self._tab_order.index(tab_id))

    def reload(self):
        page = self._current_page()
        if page is not None and hasattr(page, "reload"):
            page.reload()

    def reload_all_tabs(self):
        for page in self._pages.values():
            if hasattr(page, "reload"):
                page.reload()

    def apply_theme(self):
        for page in self._pages.values():
            if hasattr(page, "apply_theme"):
                page.apply_theme()

    def on_page_activated(self):
        self._page_active = True
        page = self._current_page()
        if page is not None and hasattr(page, "on_page_activated"):
            page.on_page_activated()

    def on_page_deactivated(self):
        page = self._current_page()
        if page is not None and hasattr(page, "on_page_deactivated"):
            page.on_page_deactivated()
        self._page_active = False

    def _current_page(self):
        widget = self.tabs.currentWidget()
        return widget

    def _on_tab_changed(self, index: int):
        previous_id = self._active_tab_id
        new_id = self._tab_order[index] if 0 <= index < len(self._tab_order) else None

        if self._page_active and previous_id in self._pages:
            previous = self._pages[previous_id]
            if hasattr(previous, "on_page_deactivated"):
                previous.on_page_deactivated()

        self._active_tab_id = new_id
        if new_id is not None:
            self.tabChanged.emit(new_id)

        if self._page_active and new_id in self._pages:
            current = self._pages[new_id]
            if hasattr(current, "on_page_activated"):
                current.on_page_activated()
