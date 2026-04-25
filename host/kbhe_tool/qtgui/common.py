from __future__ import annotations

import pathlib
import sys
import threading
import time

try:
    from PySide6.QtCharts import QChart, QChartView, QLineSeries, QValueAxis
    from PySide6.QtCore import QEasingCurve, QObject, QPointF, QRectF, QSize, Qt, QThread, QTimer, Signal
    from PySide6.QtGui import QAction, QColor, QFont, QPainter, QPainterPath, QPen, QTextCursor
    from PySide6.QtWidgets import (
        QApplication,
        QAbstractItemView,
        QButtonGroup,
        QCheckBox,
        QColorDialog,
        QComboBox,
        QFileDialog,
        QFormLayout,
        QFrame,
        QGridLayout,
        QGroupBox,
        QHBoxLayout,
        QHeaderView,
        QLabel,
        QLayout,
        QLineEdit,
        QListWidget,
        QListWidgetItem,
        QMainWindow,
        QMessageBox,
        QPushButton,
        QPlainTextEdit,
        QProgressBar,
        QRadioButton,
        QScrollArea,
        QSizePolicy,
        QSlider,
        QSpinBox,
        QDoubleSpinBox,
        QSplitter,
        QStackedWidget,
        QStatusBar,
        QTabWidget,
        QTableWidget,
        QTableWidgetItem,
        QTextEdit,
        QToolButton,
        QVBoxLayout,
        QWidget,
    )

    HAS_GUI = True
except ImportError:
    HAS_GUI = False
    QApplication = object
    QAction = object
    QAbstractItemView = object
    QButtonGroup = object
    QChart = object
    QChartView = object
    QCheckBox = object
    QColor = object
    QColorDialog = object
    QComboBox = object
    QDoubleSpinBox = object
    QEasingCurve = object
    QFileDialog = object
    QFont = object
    QFormLayout = object
    QFrame = object
    QGridLayout = object
    QGroupBox = object
    QHBoxLayout = object
    QHeaderView = object
    QLabel = object
    QLayout = object
    QLineEdit = object
    QLineSeries = object
    QListWidget = object
    QListWidgetItem = object
    QMainWindow = object
    QMessageBox = object
    QObject = object
    QPainter = object
    QPainterPath = object
    QPen = object
    QPlainTextEdit = object
    QPointF = object
    QProgressBar = object
    QPushButton = object
    QRadioButton = object
    QRectF = object
    QScrollArea = object
    QSize = object
    QSizePolicy = object
    QSlider = object
    QSpinBox = object
    QSplitter = object
    QStackedWidget = object
    QStatusBar = object
    QTabWidget = object
    QTableWidget = object
    QTableWidgetItem = object
    QTextCursor = object
    QTextEdit = object
    QThread = object
    QTimer = object
    QToolButton = object
    QValueAxis = object
    QVBoxLayout = object
    QWidget = object
    Signal = object
    Qt = object

from ..firmware import perform_firmware_update, reconnect_device
from ..protocol import (
    GAMEPAD_AXES,
    GAMEPAD_AXIS_NAMES,
    GAMEPAD_BUTTONS,
    GAMEPAD_BUTTON_NAMES,
    GAMEPAD_DIRECTIONS,
    GAMEPAD_DIRECTION_NAMES,
    HID_KEYCODES,
    HID_KEYCODE_NAMES,
    LED_EFFECT_NAMES,
)

APP_ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD_RELEASE_DIR = APP_ROOT / "build" / "Release"

__all__ = [
    "HAS_GUI",
    "APP_ROOT",
    "BUILD_RELEASE_DIR",
    "QApplication",
    "QAction",
    "QAbstractItemView",
    "QButtonGroup",
    "QChart",
    "QChartView",
    "QCheckBox",
    "QColor",
    "QColorDialog",
    "QComboBox",
    "QDoubleSpinBox",
    "QEasingCurve",
    "QFileDialog",
    "QFont",
    "QFormLayout",
    "QFrame",
    "QGridLayout",
    "QGroupBox",
    "QHBoxLayout",
    "QHeaderView",
    "QLabel",
    "QLayout",
    "QLineEdit",
    "QLineSeries",
    "QListWidget",
    "QListWidgetItem",
    "QMainWindow",
    "QMessageBox",
    "QObject",
    "QPainter",
    "QPainterPath",
    "QPen",
    "QPlainTextEdit",
    "QPointF",
    "QProgressBar",
    "QPushButton",
    "QRadioButton",
    "QRectF",
    "QScrollArea",
    "QSize",
    "QSizePolicy",
    "QSlider",
    "QSpinBox",
    "QSplitter",
    "QStackedWidget",
    "QStatusBar",
    "QTabWidget",
    "QTableWidget",
    "QTableWidgetItem",
    "QTextCursor",
    "QTextEdit",
    "QThread",
    "QTimer",
    "QToolButton",
    "QValueAxis",
    "QVBoxLayout",
    "QWidget",
    "Qt",
    "Signal",
    "pathlib",
    "sys",
    "threading",
    "time",
    "perform_firmware_update",
    "reconnect_device",
    "GAMEPAD_AXES",
    "GAMEPAD_AXIS_NAMES",
    "GAMEPAD_BUTTONS",
    "GAMEPAD_BUTTON_NAMES",
    "GAMEPAD_DIRECTIONS",
    "GAMEPAD_DIRECTION_NAMES",
    "HID_KEYCODES",
    "HID_KEYCODE_NAMES",
    "LED_EFFECT_NAMES",
]
