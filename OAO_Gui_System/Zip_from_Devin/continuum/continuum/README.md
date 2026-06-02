# Continuum — a local knowledge & meaning engine

Continuum turns years of accumulated chats (Cascade, ChatGPT, Devin, anything in
markdown/text) into a **structured, evidence-backed knowledge surface**.

The idea in one breath: your chats become a database of **concepts**, their
**meanings**, the **evidence** behind each meaning, how concepts **relate**, and
how a meaning **evolved** over time. A small embedding model is the *finder*; the
database is the *truth*; a larger model is the *thinker* on top. When you ask
about a term like `MemUnit`, Continuum hands back a **briefing packet** so a
bigger model never has to guess what you mean.

**Everything runs locally. Nothing leaves your machine.**

---

## The pipeline

```
Chats → Turns → Concepts → Meanings → Evidence → Relationships → Embeddings → Briefing
```

1. **Ingest** — markdown/text chats are split into user / assistant turns.
2. **Extract** — concepts, meanings, evidence and relationships are mined from the turns (no model required).
3. **Embed / Train** *(optional)* — a small MiniLM model is fine-tuned on your own vocabulary for semantic search.
4. **Briefing** — any query resolves to an evidence-backed packet for a larger model.

---

## Quick start

You need **Python 3.10+**.

```bash
cd continuum

# 1. create a virtual environment
python3 -m venv .venv
source .venv/bin/activate        # Windows: .venv\Scripts\activate

# 2. install dependencies
pip install -r requirements.txt

# 3. run
python run.py
```

Your browser opens at **http://127.0.0.1:8000**.

> **Minimal install:** the embedding/training bits (`sentence-transformers`,
> `torch`) are large. If you don't want them yet, comment out that section in
> `requirements.txt`. The app still ingests, extracts, builds relationships,
> searches and produces briefings — it just disables semantic search and
> training until those libraries are present.

---

## First run, step by step

1. **Load sample chats** — `File → Load sample chats` (or the Dashboard button).
2. **Run extraction** — `Edit → Run concept extraction`. Watch the pipeline counters fill in.
3. **Browse Concepts** — click any concept to see its meaning, confidence, evidence and related terms.
4. **Search** — type `MemUnit` and hit Resolve. The right pane shows the **briefing packet** you can copy into a larger model.
5. **Graph** — see concepts connected by their relationships. Click a node to search it.
6. **Model** *(optional)* — Generate pairs → Train → Embed to fine-tune the finder on your vocabulary.

Then point it at your real data: `File → Import from folder…` and paste the
absolute path to your chat folder (e.g. `/Users/you/chats`), or drag files onto
the **Ingest** panel.

---

## The GUI

A polished single-page app with a full menu bar (**File / Edit / View / Settings
/ Help**), light & dark themes, four accent colors, and panels for:

- **Dashboard** — pipeline overview and live counts
- **Ingest** — drag-drop files, import a folder, or paste a chat
- **Concepts** — searchable list with meaning, confidence and evidence
- **Search** — query → evidence-backed briefing packet (copyable)
- **Graph** — interactive relationship map
- **Chats** — raw source split into turns
- **Model** — control panel for embedding, training pairs, fine-tuning and versions
- **Events** — auditable log of everything that happened
- **Settings** — theme, accent, extraction threshold, database path

---

## Where your data lives

A single SQLite file:

```
continuum/data/continuum.db
```

That file *is* your knowledge base — back it up, copy it, move it. To start
fresh use `File → Reset database`. To put it elsewhere set `CONTINUUM_DB`:

```bash
CONTINUUM_DB=/path/to/my.db python run.py
```

---

## Project layout

```
continuum/
├── run.py                  # launcher
├── requirements.txt
├── README.md
├── sample_chats/           # bundled example chats
├── data/                   # your SQLite db + trained models (created on first run)
└── app/
    ├── server.py           # FastAPI app + JSON API
    ├── static/             # index.html, styles.css, app.js  (the GUI)
    └── core/
        ├── schema.sql      # the knowledge schema
        ├── db.py           # sqlite wrapper
        ├── ingest.py       # markdown → chats + turns
        ├── extract.py      # concepts, meanings, evidence, relationships
        ├── embeddings.py   # embedding + semantic search
        ├── training.py     # pair generation + fine-tuning
        └── retrieval.py    # the briefing-packet builder
```

---

## Notes

- The system reports **evidence, not absolute truth** — every meaning carries a
  confidence score and the snippets that justify it.
- Extraction is deterministic and offline; the optional model only makes search
  smarter, it never invents meanings.
