"""Database access layer for Continuum.

Thin wrapper around sqlite3. The database is the source of truth; everything
else (embeddings, training) is derived from it and can be rebuilt.
"""
from __future__ import annotations

import os
import sqlite3
import datetime as _dt
from pathlib import Path
from typing import Any, Iterable, Optional

_HERE = Path(__file__).resolve().parent
_SCHEMA_PATH = _HERE / "schema.sql"
_DEFAULT_DB = Path(os.environ.get("CONTINUUM_DB", _HERE.parent.parent / "data" / "continuum.db"))


def now_iso() -> str:
    return _dt.datetime.now().isoformat(timespec="seconds")


class DB:
    """Connection holder with schema bootstrap and small helpers."""

    def __init__(self, path: Optional[os.PathLike | str] = None):
        self.path = Path(path) if path else _DEFAULT_DB
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._conn = sqlite3.connect(str(self.path), check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA foreign_keys = ON")
        self._conn.execute("PRAGMA journal_mode = WAL")
        self.init_schema()

    # -- core -------------------------------------------------------------
    @property
    def conn(self) -> sqlite3.Connection:
        return self._conn

    def init_schema(self) -> None:
        sql = _SCHEMA_PATH.read_text(encoding="utf-8")
        self._conn.executescript(sql)
        self._conn.commit()

    def execute(self, sql: str, params: Iterable[Any] = ()) -> sqlite3.Cursor:
        return self._conn.execute(sql, tuple(params))

    def query(self, sql: str, params: Iterable[Any] = ()) -> list[sqlite3.Row]:
        return list(self._conn.execute(sql, tuple(params)).fetchall())

    def one(self, sql: str, params: Iterable[Any] = ()) -> Optional[sqlite3.Row]:
        return self._conn.execute(sql, tuple(params)).fetchone()

    def commit(self) -> None:
        self._conn.commit()

    def close(self) -> None:
        self._conn.close()

    # -- helpers ----------------------------------------------------------
    def log(self, kind: str, detail: str = "") -> None:
        self._conn.execute(
            "INSERT INTO event_log(ts, kind, detail) VALUES (?,?,?)",
            (now_iso(), kind, detail),
        )
        self._conn.commit()

    def get_setting(self, key: str, default: Optional[str] = None) -> Optional[str]:
        row = self.one("SELECT value FROM setting WHERE key=?", (key,))
        return row["value"] if row else default

    def set_setting(self, key: str, value: str) -> None:
        self._conn.execute(
            "INSERT INTO setting(key, value) VALUES (?,?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            (key, value),
        )
        self._conn.commit()

    def stats(self) -> dict[str, int]:
        tables = [
            "chat", "turn", "term", "meaning", "evidence",
            "term_relationship", "concept_version", "training_pair",
            "embedding_model_version", "term_embedding",
        ]
        out: dict[str, int] = {}
        for t in tables:
            row = self.one(f"SELECT COUNT(*) AS c FROM {t}")
            out[t] = int(row["c"]) if row else 0
        return out

    def reset(self) -> None:
        """Drop all data (keep schema). Used by File -> New / Reset."""
        for t in [
            "term_embedding", "embedding_model_version", "training_pair",
            "concept_version", "term_relationship", "evidence", "meaning",
            "term", "turn", "chat", "event_log",
        ]:
            self._conn.execute(f"DELETE FROM {t}")
        self._conn.commit()
