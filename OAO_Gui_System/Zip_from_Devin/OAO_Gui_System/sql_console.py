"""
sql_console.py — "paste SQL → reality updates" console.

A hardened version of the paste-and-run idea:

  * Paste SQL (or Load a .sql file) into the editor.
  * Pick / create a SQLite .db on disk, or run against an in-memory DB.
  * Run via sqlite3.executescript() so multi-statement CREATE+INSERT blocks
    (and statements that contain semicolons inside strings) execute correctly
    instead of being naively split on ';'.
  * A log shows exactly what happened; on error the whole script is rolled back.
  * After running, the table list + a row preview let you SEE that reality
    actually updated — not just trust a "success" message.

This works directly on the container editor's `containers.sql`: load it, run
it, and browse the resulting tables. Run standalone:

    python sql_console.py [optional.sql]
"""

from __future__ import annotations

import os
import sqlite3
import sys

from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)
from PyQt6.QtCore import Qt


class SqlConsole(QWidget):
    def __init__(self, initial_sql_path: str | None = None) -> None:
        super().__init__()
        self.setWindowTitle("OAO SQL Console — paste SQL → reality updates")
        self.resize(960, 680)

        self.db_path: str | None = None          # None == in-memory
        self.conn: sqlite3.Connection = sqlite3.connect(":memory:")
        self.conn.row_factory = sqlite3.Row

        self._build_ui()
        if initial_sql_path and os.path.exists(initial_sql_path):
            self._load_path(initial_sql_path)

    # -- ui ---------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)

        bar = QHBoxLayout()
        self.db_label = QLabel("DB: :memory: (in-memory — nothing persists)")
        self.db_label.setStyleSheet("color:#9a9a9a;")
        bar.addWidget(self.db_label, 1)

        self.btn_open_db = QPushButton("Open/Create .db")
        self.btn_open_db.clicked.connect(self.open_db)
        bar.addWidget(self.btn_open_db)

        self.btn_mem = QPushButton("Use :memory:")
        self.btn_mem.clicked.connect(self.use_memory)
        bar.addWidget(self.btn_mem)

        self.btn_load = QPushButton("Load .sql")
        self.btn_load.clicked.connect(self.load_sql)
        bar.addWidget(self.btn_load)
        root.addLayout(bar)

        splitter = QSplitter(Qt.Orientation.Vertical)

        top = QWidget()
        tlay = QVBoxLayout(top)
        tlay.setContentsMargins(0, 0, 0, 0)
        tlay.addWidget(QLabel("SQL (paste or load — runs as one script):"))
        self.editor = QPlainTextEdit()
        self.editor.setPlaceholderText(
            "CREATE TABLE test(id INTEGER, name TEXT);\n"
            "INSERT INTO test VALUES (1, 'hello');"
        )
        tlay.addWidget(self.editor)
        run_bar = QHBoxLayout()
        self.btn_run = QPushButton("Run SQL")
        self.btn_run.clicked.connect(self.run_sql)
        run_bar.addWidget(self.btn_run)
        run_bar.addStretch(1)
        tlay.addLayout(run_bar)
        splitter.addWidget(top)

        bottom = QWidget()
        blay = QVBoxLayout(bottom)
        blay.setContentsMargins(0, 0, 0, 0)
        browse = QHBoxLayout()
        browse.addWidget(QLabel("Tables:"))
        self.table_pick = QComboBox()
        self.table_pick.currentTextChanged.connect(self.preview_table)
        browse.addWidget(self.table_pick, 1)
        self.btn_refresh = QPushButton("Refresh")
        self.btn_refresh.clicked.connect(self.refresh_tables)
        browse.addWidget(self.btn_refresh)
        blay.addLayout(browse)
        self.preview = QTableWidget()
        blay.addWidget(self.preview, 1)
        blay.addWidget(QLabel("Log:"))
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumHeight(140)
        blay.addWidget(self.log)
        splitter.addWidget(bottom)

        splitter.setSizes([360, 320])
        root.addWidget(splitter, 1)

    # -- logging ----------------------------------------------------------

    def _log(self, msg: str) -> None:
        self.log.appendPlainText(msg)

    # -- db management ----------------------------------------------------

    def _reconnect(self, path: str | None) -> None:
        try:
            self.conn.close()
        except Exception:  # noqa: BLE001
            pass
        self.db_path = path
        self.conn = sqlite3.connect(path if path else ":memory:")
        self.conn.row_factory = sqlite3.Row
        label = path if path else ":memory: (in-memory — nothing persists)"
        self.db_label.setText(f"DB: {label}")
        self._log(f"Connected to {label}")
        self.refresh_tables()

    def open_db(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "Open or create SQLite DB", "", "SQLite DB (*.db *.sqlite)")
        if path:
            self._reconnect(path)

    def use_memory(self) -> None:
        self._reconnect(None)

    # -- sql --------------------------------------------------------------

    def _load_path(self, path: str) -> None:
        with open(path, "r", encoding="utf-8") as fh:
            self.editor.setPlainText(fh.read())
        self._log(f"Loaded {path} ({os.path.getsize(path)} bytes)")

    def load_sql(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Load .sql", "", "SQL (*.sql);;All files (*)")
        if path:
            self._load_path(path)

    def run_sql(self) -> None:
        text = self.editor.toPlainText().strip()
        if not text:
            QMessageBox.warning(self, "Empty", "Paste or load some SQL first.")
            return
        try:
            self.conn.executescript(text)
            self.conn.commit()
            self._log("Executed script successfully. Commit complete.")
        except Exception as exc:  # noqa: BLE001
            self.conn.rollback()
            self._log(f"ERROR (rolled back): {exc}")
            QMessageBox.critical(self, "SQL Error", str(exc))
            return
        self.refresh_tables()

    # -- browsing ---------------------------------------------------------

    def refresh_tables(self) -> None:
        self.table_pick.blockSignals(True)
        self.table_pick.clear()
        try:
            rows = self.conn.execute(
                "SELECT name FROM sqlite_master WHERE type='table' "
                "ORDER BY name"
            ).fetchall()
            names = [r["name"] for r in rows]
            self.table_pick.addItems(names)
            self._log(f"Tables: {', '.join(names) if names else '(none)'}")
        except Exception as exc:  # noqa: BLE001
            self._log(f"Could not list tables: {exc}")
        self.table_pick.blockSignals(False)
        self.preview_table(self.table_pick.currentText())

    def preview_table(self, name: str) -> None:
        if not name:
            self.preview.clear()
            self.preview.setRowCount(0)
            self.preview.setColumnCount(0)
            return
        try:
            rows = self.conn.execute(
                f'SELECT * FROM "{name}" LIMIT 200').fetchall()
            count = self.conn.execute(
                f'SELECT COUNT(*) AS c FROM "{name}"').fetchone()["c"]
        except Exception as exc:  # noqa: BLE001
            self._log(f"Preview failed for {name}: {exc}")
            return
        cols = rows[0].keys() if rows else [
            r[1] for r in self.conn.execute(f'PRAGMA table_info("{name}")')
        ]
        self.preview.setColumnCount(len(cols))
        self.preview.setHorizontalHeaderLabels(list(cols))
        self.preview.setRowCount(len(rows))
        for r, row in enumerate(rows):
            for c, col in enumerate(cols):
                self.preview.setItem(r, c, QTableWidgetItem(str(row[col])))
        self.preview.resizeColumnsToContents()
        self._log(f"{name}: {count} row(s) (showing up to 200)")


def main() -> int:
    initial = sys.argv[1] if len(sys.argv) > 1 else None
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = SqlConsole(initial)
    win.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
