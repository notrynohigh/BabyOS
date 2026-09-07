#!/usr/bin/env python3
"""
BabyOS_Protocol — Python upper-computer (host PC software).
Entry point.

Pure stdlib GUI (tkinter). No PyQt5 / PySide / etc.
"""

from mainwindow import MainWindow


def main():
    w = MainWindow()
    w.mainloop()


if __name__ == '__main__':
    main()
