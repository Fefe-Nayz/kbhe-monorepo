try:
    from .qtgui import HAS_GUI, KBHEQtMainWindow, launch_gui
except ImportError as ex:
    if getattr(ex, "name", None) != "PySide6":
        raise

    HAS_GUI = False
    KBHEQtMainWindow = None

    def launch_gui(_device):
        raise RuntimeError("PySide6 is not available on this system")

KBHEConfiguratorApp = KBHEQtMainWindow
LEDMatrixEditor = KBHEQtMainWindow

__all__ = ["HAS_GUI", "KBHEConfiguratorApp", "LEDMatrixEditor", "launch_gui"]
