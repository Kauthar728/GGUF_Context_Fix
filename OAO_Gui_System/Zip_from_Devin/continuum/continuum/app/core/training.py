"""Fine-tuning loop: specialise the embedding model on the user's own vocabulary.

The database -> training pairs -> fine-tuned MiniLM -> versioned model. Each run
produces a new immutable model version; the active one is used for retrieval.

Training pairs are (anchor, positive, negative):
  anchor   = a term
  positive = its meaning / a related term (should embed close)
  negative = an unrelated term (should embed far)

Uses MultipleNegativesRankingLoss when only positives are reliable, which is the
standard, robust choice for small contrastive datasets. Runs on CPU; kept small
so an 8 GB machine can complete it.
"""
from __future__ import annotations

import random
import datetime as _dt
from pathlib import Path

from .db import DB, now_iso
from .embeddings import EmbeddingEngine, MODELS_DIR, DEFAULT_BASE_MODEL, deps_available


def generate_pairs(db: DB) -> int:
    """Build training pairs from terms, meanings and relationships."""
    db.execute("DELETE FROM training_pair")
    ver_row = db.one("SELECT COALESCE(MAX(dataset_version),0) AS v FROM training_pair")
    dataset_version = (int(ver_row["v"]) if ver_row and ver_row["v"] else 0) + 1

    terms = db.query("SELECT term_id, term FROM term")
    term_by_id = {r["term_id"]: r["term"] for r in terms}
    all_terms = list(term_by_id.values())
    n = 0
    ts = now_iso()

    def add(anchor, positive, negative, source):
        nonlocal n
        if not anchor or not positive:
            return
        db.execute(
            "INSERT INTO training_pair(anchor, positive, negative, source, dataset_version, created_date)"
            " VALUES (?,?,?,?,?,?)",
            (anchor, positive, negative, source, dataset_version, ts),
        )
        n += 1

    # term <-> meaning
    for r in db.query("SELECT t.term, m.meaning_text FROM meaning m JOIN term t ON t.term_id=m.term_id WHERE m.is_current=1 AND m.meaning_text IS NOT NULL"):
        neg = random.choice(all_terms) if all_terms else None
        add(r["term"], r["meaning_text"], neg, "term_meaning")

    # related terms (from relationship graph)
    for r in db.query("SELECT term_a, term_b FROM term_relationship WHERE strength >= 0.3"):
        a = term_by_id.get(r["term_a"]); b = term_by_id.get(r["term_b"])
        if a and b:
            neg = random.choice(all_terms) if all_terms else None
            add(a, b, neg, "related_terms")

    db.commit()
    db.log("generate_pairs", f"count={n} dataset_version={dataset_version}")
    return n


def train(db: DB, epochs: int = 1, batch_size: int = 16, version_name: str | None = None) -> dict:
    if not deps_available():
        raise RuntimeError("sentence-transformers not installed; cannot train")

    from sentence_transformers import SentenceTransformer, InputExample, losses
    from torch.utils.data import DataLoader

    pairs = db.query("SELECT anchor, positive FROM training_pair")
    if len(pairs) < 4:
        raise RuntimeError(f"not enough training pairs ({len(pairs)}); ingest + extract more chats first")

    examples = [InputExample(texts=[p["anchor"], p["positive"]]) for p in pairs]
    model = SentenceTransformer(DEFAULT_BASE_MODEL)
    loader = DataLoader(examples, shuffle=True, batch_size=min(batch_size, len(examples)))
    loss = losses.MultipleNegativesRankingLoss(model)

    # sentence-transformers' fit() writes intermediate Trainer artifacts to a
    # "checkpoints/" dir in the cwd; train in a throwaway temp dir and clean up
    # so we never litter the user's project folder.
    import tempfile, shutil
    warmup = max(1, int(len(loader) * epochs * 0.1))
    tmp_ckpt = Path(tempfile.mkdtemp(prefix="continuum-train-"))
    try:
        model.fit(
            train_objectives=[(loader, loss)],
            epochs=epochs,
            warmup_steps=warmup,
            checkpoint_path=str(tmp_ckpt),
            checkpoint_save_total_limit=0,
            show_progress_bar=False,
        )
    finally:
        shutil.rmtree(tmp_ckpt, ignore_errors=True)
        shutil.rmtree(Path("checkpoints"), ignore_errors=True)

    ts = _dt.datetime.now()
    version_name = version_name or f"continuum-v{ts.strftime('%Y%m%d-%H%M%S')}"
    out_dir = MODELS_DIR / version_name
    out_dir.mkdir(parents=True, exist_ok=True)
    model.save(str(out_dir))

    ds_row = db.one("SELECT COALESCE(MAX(dataset_version),1) AS v FROM training_pair")
    dataset_version = int(ds_row["v"]) if ds_row and ds_row["v"] else 1

    db.execute("UPDATE embedding_model_version SET is_active=0")
    cur = db.execute(
        "INSERT INTO embedding_model_version(version_name, base_model, training_date, dataset_version, pair_count, accuracy, model_path, is_active, notes)"
        " VALUES (?,?,?,?,?,?,?,1,?)",
        (version_name, DEFAULT_BASE_MODEL, now_iso(), dataset_version, len(examples), None, str(out_dir),
         f"epochs={epochs} batch={batch_size}"),
    )
    db.commit()
    db.log("train", f"version={version_name} pairs={len(examples)} epochs={epochs}")

    # re-embed all terms with the new active model
    eng = EmbeddingEngine(db)
    embedded = eng.embed_terms()
    return {
        "version_id": int(cur.lastrowid),
        "version_name": version_name,
        "pairs": len(examples),
        "epochs": epochs,
        "model_path": str(out_dir),
        "embedded_terms": embedded,
    }


def activate_version(db: DB, version_id: int) -> None:
    db.execute("UPDATE embedding_model_version SET is_active=0")
    db.execute("UPDATE embedding_model_version SET is_active=1 WHERE version_id=?", (version_id,))
    db.commit()
    db.log("activate_model", f"version_id={version_id}")
