from .qtgui import HAS_GUI, KBHEQtMainWindow, launch_gui

KBHEConfiguratorApp = KBHEQtMainWindow
LEDMatrixEditor = KBHEQtMainWindow

__all__ = ["HAS_GUI", "KBHEConfiguratorApp", "LEDMatrixEditor", "launch_gui"]
