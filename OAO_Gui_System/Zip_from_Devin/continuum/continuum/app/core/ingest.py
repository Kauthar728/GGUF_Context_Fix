"""Chat ingestion: raw markdown -> chat + turns.

Handles the messy reality of exported chat logs. There is no single standard
for how a markdown chat marks who is speaking, so we try several heuristics in
order and fall back to treating the whole document as a single block split on
blank lines if nothing else matches.

Recognised speaker markers (case-insensitive), at the start of a line:
    ## User / ### User / **User** / User: / You said: / Prompt:
    ## Assistant / ChatGPT / ChatGPT said: / Cascade / Devin / AI: / Response:
"""
from __future__ import annotations

import re
import hashlib
import datetime as _dt
from pathlib import Path
from typing import Optional

from .db import DB, now_iso

USER_LABELS = [
    "user", "you", "you said", "human", "prompt", "me", "wayne", "question",
]
ASSISTANT_LABELS = [
    "assistant", "chatgpt", "chatgpt said", "gpt", "ai", "cascade", "devin",
    "claude", "model", "response", "answer", "system",
]

# Build a regex that matches a heading-ish speaker marker at the start of a line.
_LABEL_ALT = "|".join(sorted(set(USER_LABELS + ASSISTANT_LABELS), key=len, reverse=True))
_SPEAKER_RE = re.compile(
    r"^\s*(?:#{1,6}\s*)?(?:\*\*|__)?\s*(" + _LABEL_ALT + r")\s*(?:\*\*|__)?\s*[:\-\u2014]?\s*$",
    re.IGNORECASE,
)
# Inline form like "User: hello" on one line.
_INLINE_RE = re.compile(
    r"^\s*(?:#{1,6}\s*)?(?:\*\*|__)?\s*(" + _LABEL_ALT + r")\s*(?:\*\*|__)?\s*[:\-\u2014]\s+(.*)$",
    re.IGNORECASE,
)


def _classify(label: str) -> str:
    l = label.strip().lower()
    if l in USER_LABELS:
        return "user"
    if l in ASSISTANT_LABELS:
        return "system" if l == "system" else "assistant"
    return "assistant"


def detect_source(text: str, file_path: str = "") -> str:
    low = (text[:4000] + " " + file_path).lower()
    for name, key in [
        ("Cascade", "cascade"), ("ChatGPT", "chatgpt"), ("Devin", "devin"),
        ("Claude", "claude"), ("GPT", "gpt"),
    ]:
        if key in low:
            return name
    return "unknown"


def _hash(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8", "ignore")).hexdigest()


def split_turns(markdown: str) -> list[tuple[str, str]]:
    """Return list of (speaker, message). Best-effort across formats."""
    lines = markdown.splitlines()
    turns: list[tuple[str, str]] = []
    cur_speaker: Optional[str] = None
    buf: list[str] = []

    def flush():
        nonlocal buf, cur_speaker
        if cur_speaker is not None:
            msg = "\n".join(buf).strip()
            if msg:
                turns.append((cur_speaker, msg))
        buf = []

    found_any_marker = False
    for line in lines:
        m_inline = _INLINE_RE.match(line)
        m_block = _SPEAKER_RE.match(line)
        if m_block:
            found_any_marker = True
            flush()
            cur_speaker = _classify(m_block.group(1))
            continue
        if m_inline:
            found_any_marker = True
            flush()
            cur_speaker = _classify(m_inline.group(1))
            buf = [m_inline.group(2)]
            continue
        buf.append(line)
    flush()

    if found_any_marker and turns:
        return turns

    # Fallback: no speaker markers at all. Treat the document as alternating
    # blocks separated by blank-line gaps, assuming it starts with the user.
    blocks = [b.strip() for b in re.split(r"\n\s*\n\s*\n+", markdown) if b.strip()]
    if len(blocks) <= 1:
        # Single blob -> one assistant turn carrying everything (still searchable).
        blob = markdown.strip()
        return [("assistant", blob)] if blob else []
    out: list[tuple[str, str]] = []
    for i, b in enumerate(blocks):
        out.append(("user" if i % 2 == 0 else "assistant", b))
    return out


def _plain_text(markdown: str) -> str:
    txt = re.sub(r"```.*?```", " ", markdown, flags=re.DOTALL)
    txt = re.sub(r"`[^`]*`", " ", txt)
    txt = re.sub(r"[#>*_~\-]{1,}", " ", txt)
    txt = re.sub(r"\s+", " ", txt)
    return txt.strip()


def ingest_text(db: DB, markdown: str, *, title: str = "", file_path: str = "",
                created_date: str = "", source: str = "") -> Optional[int]:
    """Ingest a single markdown chat. Returns chat_id, or None if duplicate."""
    h = _hash(markdown)
    if db.one("SELECT chat_id FROM chat WHERE hash=?", (h,)):
        return None
    src = source or detect_source(markdown, file_path)
    title = title or (Path(file_path).stem if file_path else "Untitled chat")
    cur = db.execute(
        "INSERT INTO chat(source, file_path, title, created_date, import_date, raw_markdown, raw_text, hash)"
        " VALUES (?,?,?,?,?,?,?,?)",
        (src, file_path, title, created_date or "", now_iso(), markdown, _plain_text(markdown), h),
    )
    chat_id = int(cur.lastrowid)
    turns = split_turns(markdown)
    for seq, (speaker, message) in enumerate(turns):
        db.execute(
            "INSERT INTO turn(chat_id, sequence, speaker, message) VALUES (?,?,?,?)",
            (chat_id, seq, speaker, message),
        )
    db.commit()
    db.log("ingest", f"chat_id={chat_id} title={title!r} turns={len(turns)} source={src}")
    return chat_id


def ingest_file(db: DB, path: str | Path) -> Optional[int]:
    p = Path(path)
    text = p.read_text(encoding="utf-8", errors="ignore")
    try:
        mtime = _dt.datetime.fromtimestamp(p.stat().st_mtime).isoformat(timespec="seconds")
    except OSError:
        mtime = ""
    return ingest_text(db, text, title=p.stem, file_path=str(p), created_date=mtime)


def ingest_folder(db: DB, folder: str | Path, patterns=(".md", ".markdown", ".txt")) -> dict:
    folder = Path(folder)
    files = [p for p in folder.rglob("*") if p.suffix.lower() in patterns and p.is_file()]
    imported, skipped = 0, 0
    for f in sorted(files):
        cid = ingest_file(db, f)
        if cid is None:
            skipped += 1
        else:
            imported += 1
    return {"files": len(files), "imported": imported, "skipped": skipped}
