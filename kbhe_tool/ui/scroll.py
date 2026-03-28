from .common import tk, ttk


class ScrollableFrame(ttk.Frame if ttk is not None else object):
    """Reusable vertically scrollable frame for large settings pages."""

    def __init__(self, parent, **kwargs):
        if ttk is None or tk is None:
            raise RuntimeError("tkinter is not available on this system")
        super().__init__(parent, **kwargs)
        self.canvas = tk.Canvas(self, highlightthickness=0)
        self.scrollbar = ttk.Scrollbar(self, orient='vertical', command=self.canvas.yview)
        self.content = ttk.Frame(self.canvas)
        self.window_id = self.canvas.create_window((0, 0), window=self.content, anchor='nw')

        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        self.content.bind('<Configure>', self._on_content_configure)
        self.canvas.bind('<Configure>', self._on_canvas_configure)
        self.content.bind('<Enter>', self._bind_mousewheel)
        self.content.bind('<Leave>', self._unbind_mousewheel)

    def _on_content_configure(self, event):
        del event
        self.canvas.configure(scrollregion=self.canvas.bbox('all'))

    def _on_canvas_configure(self, event):
        self.canvas.itemconfigure(self.window_id, width=event.width)

    def _bind_mousewheel(self, event):
        del event
        self.canvas.bind_all('<MouseWheel>', self._on_mousewheel)
        self.canvas.bind_all('<Button-4>', self._on_mousewheel_linux)
        self.canvas.bind_all('<Button-5>', self._on_mousewheel_linux)

    def _unbind_mousewheel(self, event):
        del event
        self.canvas.unbind_all('<MouseWheel>')
        self.canvas.unbind_all('<Button-4>')
        self.canvas.unbind_all('<Button-5>')

    def _on_mousewheel(self, event):
        delta = int(-1 * (event.delta / 120))
        self.canvas.yview_scroll(delta, 'units')

    def _on_mousewheel_linux(self, event):
        if event.num == 4:
            self.canvas.yview_scroll(-1, 'units')
        elif event.num == 5:
            self.canvas.yview_scroll(1, 'units')
