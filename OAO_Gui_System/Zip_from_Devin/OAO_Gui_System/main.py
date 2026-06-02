"""
main.py — entry point for the OAO Container Editor v1.1.

    python main.py [path-to.sql]

Defaults to ./containers.sql, creating a starter file (schema + seed) if it
does not exist.
"""

from __future__ import annotations

import sys

from PyQt6.QtWidgets import QApplication

from editor_window import EditorWindow

DEFAULT_SQL = "containers.sql"


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SQL
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = EditorWindow(path)
    win.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
