"""Embedding layer: the *finder*.

The database holds the truth; embeddings only help locate the right concept when
the user's words don't literally match a stored term ("memory transport thing"
-> MemBus).

This module degrades gracefully: if sentence-transformers / torch are not
installed, embedding + semantic search are disabled but the rest of the app keeps
working (keyword search is always available). The Model Control Panel surfaces
this state to the user.
"""
from __future__ import annotations

import io
import struct
from pathlib import Path
from typing import Optional

from .db import DB, now_iso

DEFAULT_BASE_MODEL = "sentence-transformers/all-MiniLM-L6-v2"
MODELS_DIR = Path(__file__).resolve().parent.parent.parent / "data" / "models"


def deps_available() -> bool:
    try:
        import sentence_transformers  # noqa: F401
        import numpy  # noqa: F401
        return True
    except Exception:
        return False


def _pack(vec) -> bytes:
    import numpy as np
    arr = np.asarray(vec, dtype="float32").ravel()
    return struct.pack("<I", arr.shape[0]) + arr.tobytes()


def _unpack(blob: bytes):
    import numpy as np
    (n,) = struct.unpack("<I", blob[:4])
    return np.frombuffer(blob[4:4 + n * 4], dtype="float32")


class EmbeddingEngine:
    """Loads a model, embeds terms, and runs cosine semantic search."""

    def __init__(self, db: DB):
        self.db = db
        self._model = None
        self._model_name = None

    # -- status -----------------------------------------------------------
    def status(self) -> dict:
        active = self.db.one(
            "SELECT * FROM embedding_model_version WHERE is_active=1 ORDER BY version_id DESC LIMIT 1"
        )
        n_emb = self.db.one("SELECT COUNT(*) AS c FROM term_embedding")
        return {
            "deps_available": deps_available(),
            "loaded": self._model is not None,
            "loaded_model": self._model_name,
            "active_model": dict(active) if active else None,
            "embedded_terms": int(n_emb["c"]) if n_emb else 0,
            "base_model": DEFAULT_BASE_MODEL,
        }

    def active_model_path(self) -> str:
        row = self.db.one(
            "SELECT model_path, version_name FROM embedding_model_version WHERE is_active=1 ORDER BY version_id DESC LIMIT 1"
        )
        if row and row["model_path"] and Path(row["model_path"]).exists():
            return row["model_path"]
        return DEFAULT_BASE_MODEL

    def active_model_label(self) -> str:
        row = self.db.one(
            "SELECT version_name FROM embedding_model_version WHERE is_active=1 ORDER BY version_id DESC LIMIT 1"
        )
        return row["version_name"] if row else "base"

    # -- model ------------------------------------------------------------
    def load(self, path: Optional[str] = None):
        if not deps_available():
            raise RuntimeError("sentence-transformers not installed")
        from sentence_transformers import SentenceTransformer
        path = path or self.active_model_path()
        if self._model is not None and self._model_name == path:
            return self._model
        self._model = SentenceTransformer(path)
        self._model_name = path
        self.db.log("model_load", path)
        return self._model

    # -- embedding --------------------------------------------------------
    def embed_terms(self, batch_size: int = 64) -> int:
        """Embed every term using term + best meaning as the text. Returns count."""
        model = self.load()
        label = self.active_model_label()
        rows = self.db.query(
            "SELECT t.term_id, t.term, "
            " (SELECT meaning_text FROM meaning m WHERE m.term_id=t.term_id AND m.is_current=1 LIMIT 1) AS meaning "
            "FROM term t"
        )
        if not rows:
            return 0
        texts = [(r["term"] + ". " + (r["meaning"] or "")).strip() for r in rows]
        vecs = model.encode(texts, batch_size=batch_size, show_progress_bar=False, normalize_embeddings=True)
        n = 0
        for r, v in zip(rows, vecs):
            self.db.execute(
                "INSERT INTO term_embedding(term_id, model_version, dim, vector) VALUES (?,?,?,?) "
                "ON CONFLICT(term_id, model_version) DO UPDATE SET dim=excluded.dim, vector=excluded.vector",
                (r["term_id"], label, len(v), _pack(v)),
            )
            n += 1
        self.db.commit()
        self.db.log("embed_terms", f"count={n} model={label}")
        return n

    def search(self, query: str, top_k: int = 8) -> list[dict]:
        """Semantic search over term embeddings. Empty list if unavailable."""
        if not deps_available():
            return []
        import numpy as np
        label = self.active_model_label()
        rows = self.db.query(
            "SELECT te.term_id, te.vector, t.term FROM term_embedding te JOIN term t ON t.term_id=te.term_id"
            " WHERE te.model_version=?",
            (label,),
        )
        if not rows:
            return []
        model = self.load()
        q = model.encode([query], normalize_embeddings=True)[0]
        q = np.asarray(q, dtype="float32")
        scored = []
        for r in rows:
            v = _unpack(r["vector"])
            if v.shape[0] != q.shape[0]:
                continue
            sim = float(np.dot(q, v))
            scored.append((sim, r["term_id"], r["term"]))
        scored.sort(reverse=True)
        return [{"term_id": tid, "term": term, "score": round(sim, 4)} for sim, tid, term in scored[:top_k]]
