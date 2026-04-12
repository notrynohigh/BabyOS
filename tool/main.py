#!/usr/bin/env python3
"""
BabyOS_Protocol — Python upper-computer (host PC software).
Entry point.
"""

import sys
from PyQt5.QtWidgets import QApplication
from mainwindow import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("BabyOS_Protocol")
    w = MainWindow()
    w.show()
    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
