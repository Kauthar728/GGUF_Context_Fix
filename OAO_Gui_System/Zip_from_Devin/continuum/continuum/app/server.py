"""Continuum FastAPI server.

Serves the single-page GUI and a JSON API over the knowledge engine. Runs fully
locally; nothing leaves the machine.
"""
from __future__ import annotations

import os
import tempfile
import threading
from pathlib import Path

from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Body
from fastapi.responses import HTMLResponse, JSONResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles

from .core.db import DB
from .core import ingest as ingest_mod
from .core.extract import Extractor
from .core.embeddings import EmbeddingEngine, deps_available
from .core import training as training_mod
from .core.retrieval import Retriever

HERE = Path(__file__).resolve().parent
STATIC = HERE / "static"
SAMPLE = HERE.parent / "sample_chats"

app = FastAPI(title="Continuum", version="1.0")
db = DB()
engine = EmbeddingEngine(db)
retriever = Retriever(db, engine)

# Background job state (single slot; this is a local single-user app).
_job = {"running": False, "kind": None, "message": "", "result": None, "error": None}
_job_lock = threading.Lock()


def _run_job(kind: str, fn):
    def worker():
        try:
            res = fn()
            with _job_lock:
                _job.update(running=False, message=f"{kind} complete", result=res, error=None)
        except Exception as exc:  # noqa: BLE001
            with _job_lock:
                _job.update(running=False, message=f"{kind} failed", result=None, error=str(exc))
    with _job_lock:
        if _job["running"]:
            raise HTTPException(409, "another job is running")
        _job.update(running=True, kind=kind, message=f"{kind} started", result=None, error=None)
    threading.Thread(target=worker, daemon=True).start()


# ---------------------------------------------------------------- pages
@app.get("/", response_class=HTMLResponse)
def index():
    return (STATIC / "index.html").read_text(encoding="utf-8")


# ---------------------------------------------------------------- status
@app.get("/api/status")
def status():
    return {
        "stats": db.stats(),
        "embedding": engine.status(),
        "deps_available": deps_available(),
        "db_path": str(db.path),
        "job": _job,
    }


@app.get("/api/job")
def job():
    return _job


@app.get("/api/settings")
def get_settings():
    return {
        "theme": db.get_setting("theme", "dark"),
        "accent": db.get_setting("accent", "violet"),
        "min_occurrences": db.get_setting("min_occurrences", "2"),
    }


@app.post("/api/settings")
def set_settings(payload: dict = Body(...)):
    for k, v in payload.items():
        db.set_setting(k, str(v))
    return {"ok": True}


# ---------------------------------------------------------------- ingest
@app.post("/api/ingest/files")
async def ingest_files(files: list[UploadFile] = File(...)):
    imported, skipped = 0, 0
    for f in files:
        raw = (await f.read()).decode("utf-8", "ignore")
        cid = ingest_mod.ingest_text(db, raw, title=Path(f.filename).stem, file_path=f.filename)
        if cid is None:
            skipped += 1
        else:
            imported += 1
    return {"imported": imported, "skipped": skipped, "stats": db.stats()}


@app.post("/api/ingest/folder")
def ingest_folder(payload: dict = Body(...)):
    folder = payload.get("path", "")
    if not folder or not Path(folder).exists():
        raise HTTPException(400, f"folder not found: {folder}")
    res = ingest_mod.ingest_folder(db, folder)
    res["stats"] = db.stats()
    return res


@app.post("/api/ingest/sample")
def ingest_sample():
    if not SAMPLE.exists():
        raise HTTPException(404, "no sample chats bundled")
    res = ingest_mod.ingest_folder(db, SAMPLE)
    res["stats"] = db.stats()
    return res


@app.post("/api/ingest/paste")
def ingest_paste(payload: dict = Body(...)):
    text = payload.get("text", "")
    title = payload.get("title", "Pasted chat")
    if not text.strip():
        raise HTTPException(400, "empty text")
    cid = ingest_mod.ingest_text(db, text, title=title, file_path="(pasted)")
    return {"chat_id": cid, "stats": db.stats()}


# ---------------------------------------------------------------- extract
@app.post("/api/extract")
def extract():
    min_occ = int(db.get_setting("min_occurrences", "2") or 2)

    def fn():
        return Extractor(db).run(min_occurrences=min_occ)

    _run_job("extract", fn)
    return {"started": True}


# ---------------------------------------------------------------- concepts
@app.get("/api/concepts")
def concepts(limit: int = 200, offset: int = 0, q: str = ""):
    if q:
        rows = db.query(
            "SELECT term_id, term, occurrence_count FROM term WHERE normalized_term LIKE ? "
            "ORDER BY occurrence_count DESC LIMIT ? OFFSET ?",
            (f"%{q.lower()}%", limit, offset),
        )
    else:
        rows = db.query(
            "SELECT term_id, term, occurrence_count FROM term ORDER BY occurrence_count DESC LIMIT ? OFFSET ?",
            (limit, offset),
        )
    out = []
    for r in rows:
        m = db.one("SELECT meaning_text, confidence_score FROM meaning WHERE term_id=? AND is_current=1 ORDER BY confidence_score DESC LIMIT 1", (r["term_id"],))
        out.append({
            "term_id": r["term_id"], "term": r["term"], "occurrences": r["occurrence_count"],
            "meaning": m["meaning_text"] if m else None,
            "confidence": m["confidence_score"] if m else None,
        })
    total = db.one("SELECT COUNT(*) AS c FROM term")
    return {"concepts": out, "total": int(total["c"]) if total else 0}


@app.get("/api/concept/{term_id}")
def concept(term_id: int):
    term = db.one("SELECT term FROM term WHERE term_id=?", (term_id,))
    if not term:
        raise HTTPException(404, "term not found")
    return retriever.packet(term["term"])


# ---------------------------------------------------------------- search / briefing
@app.get("/api/search")
def search(q: str):
    if not q.strip():
        raise HTTPException(400, "empty query")
    return retriever.packet(q)


@app.get("/api/briefing", response_class=PlainTextResponse)
def briefing(q: str):
    if not q.strip():
        raise HTTPException(400, "empty query")
    return retriever.briefing_text(q)


# ---------------------------------------------------------------- graph
@app.get("/api/graph")
def graph(limit: int = 60):
    terms = db.query("SELECT term_id, term, occurrence_count FROM term ORDER BY occurrence_count DESC LIMIT ?", (limit,))
    ids = {t["term_id"] for t in terms}
    nodes = [{"id": t["term_id"], "label": t["term"], "size": t["occurrence_count"]} for t in terms]
    edges = []
    for r in db.query("SELECT term_a, term_b, relationship_type, strength FROM term_relationship ORDER BY strength DESC LIMIT 400"):
        if r["term_a"] in ids and r["term_b"] in ids:
            edges.append({"source": r["term_a"], "target": r["term_b"],
                          "type": r["relationship_type"], "strength": r["strength"]})
    return {"nodes": nodes, "edges": edges}


# ---------------------------------------------------------------- model control
@app.get("/api/model/status")
def model_status():
    versions = db.query("SELECT * FROM embedding_model_version ORDER BY version_id DESC")
    return {"status": engine.status(), "versions": [dict(v) for v in versions]}


@app.post("/api/model/embed")
def model_embed():
    if not deps_available():
        raise HTTPException(400, "sentence-transformers not installed")

    def fn():
        return {"embedded_terms": engine.embed_terms()}

    _run_job("embed", fn)
    return {"started": True}


@app.post("/api/model/generate-pairs")
def model_generate_pairs():
    n = training_mod.generate_pairs(db)
    return {"pairs": n}


@app.post("/api/model/train")
def model_train(payload: dict = Body(default={})):
    if not deps_available():
        raise HTTPException(400, "sentence-transformers not installed")
    epochs = int(payload.get("epochs", 1))
    name = payload.get("version_name") or None

    def fn():
        row = db.one("SELECT COUNT(*) AS c FROM training_pair")
        if not row or int(row["c"]) == 0:
            training_mod.generate_pairs(db)
        return training_mod.train(db, epochs=epochs, version_name=name)

    _run_job("train", fn)
    return {"started": True}


@app.post("/api/model/activate")
def model_activate(payload: dict = Body(...)):
    vid = int(payload["version_id"])
    training_mod.activate_version(db, vid)
    engine._model = None  # force reload on next use
    return {"ok": True}


# ---------------------------------------------------------------- chats
@app.get("/api/chats")
def chats(limit: int = 100):
    rows = db.query(
        "SELECT c.chat_id, c.title, c.source, c.created_date, "
        "(SELECT COUNT(*) FROM turn t WHERE t.chat_id=c.chat_id) AS turns "
        "FROM chat c ORDER BY c.chat_id DESC LIMIT ?", (limit,))
    return {"chats": [dict(r) for r in rows]}


@app.get("/api/chat/{chat_id}")
def chat(chat_id: int):
    c = db.one("SELECT * FROM chat WHERE chat_id=?", (chat_id,))
    if not c:
        raise HTTPException(404, "chat not found")
    turns = db.query("SELECT sequence, speaker, message FROM turn WHERE chat_id=? ORDER BY sequence", (chat_id,))
    return {"chat": dict(c), "turns": [dict(t) for t in turns]}


# ---------------------------------------------------------------- admin
@app.post("/api/reset")
def reset():
    db.reset()
    return {"ok": True, "stats": db.stats()}


@app.get("/api/events")
def events(limit: int = 100):
    rows = db.query("SELECT ts, kind, detail FROM event_log ORDER BY event_id DESC LIMIT ?", (limit,))
    return {"events": [dict(r) for r in rows]}


# static mount (after routes)
if STATIC.exists():
    app.mount("/static", StaticFiles(directory=str(STATIC)), name="static")
