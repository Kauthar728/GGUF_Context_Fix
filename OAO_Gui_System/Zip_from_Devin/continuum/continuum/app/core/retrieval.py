"""Retrieval: build the evidence-backed briefing packet for a query.

This is the 'context compiler' output. Given a query like "MemUnit" (or a fuzzy
phrase like "memory transport thing"), it assembles a structured packet:

    {
      term, meaning, understanding, confidence,
      related: [...], evidence: [...], source_chats: [...],
      evolution: [...], match: 'semantic'|'keyword'|'exact'
    }

The packet is meant to be handed to a larger model so it never has to guess what
the user means. The system reports *evidence*, not absolute truth.
"""
from __future__ import annotations

import re
from typing import Optional

from .db import DB
from .embeddings import EmbeddingEngine
from .extract import normalize


class Retriever:
    def __init__(self, db: DB, engine: Optional[EmbeddingEngine] = None):
        self.db = db
        self.engine = engine or EmbeddingEngine(db)

    def resolve_term(self, query: str) -> tuple[Optional[int], str]:
        """Return (term_id, match_kind). Tries exact, then keyword, then semantic."""
        norm = normalize(query)
        row = self.db.one("SELECT term_id FROM term WHERE normalized_term=?", (norm,))
        if row:
            return int(row["term_id"]), "exact"
        # keyword contains
        row = self.db.one(
            "SELECT term_id FROM term WHERE normalized_term LIKE ? ORDER BY occurrence_count DESC LIMIT 1",
            (f"%{norm}%",),
        )
        if row:
            return int(row["term_id"]), "keyword"
        # semantic
        hits = self.engine.search(query, top_k=1)
        if hits:
            return hits[0]["term_id"], "semantic"
        return None, "none"

    def candidates(self, query: str, top_k: int = 8) -> list[dict]:
        """Ranked candidate terms for the query (semantic + keyword union)."""
        out: dict[int, dict] = {}
        for h in self.engine.search(query, top_k=top_k):
            out[h["term_id"]] = {"term_id": h["term_id"], "term": h["term"], "score": h["score"], "via": "semantic"}
        norm = normalize(query)
        for r in self.db.query(
            "SELECT term_id, term, occurrence_count FROM term WHERE normalized_term LIKE ? "
            "ORDER BY occurrence_count DESC LIMIT ?", (f"%{norm}%", top_k)):
            if r["term_id"] not in out:
                out[r["term_id"]] = {"term_id": r["term_id"], "term": r["term"],
                                     "score": None, "via": "keyword"}
        ranked = list(out.values())
        ranked.sort(key=lambda d: (d["score"] is None, -(d["score"] or 0)))
        return ranked[:top_k]

    def packet(self, query: str) -> dict:
        term_id, match = self.resolve_term(query)
        if term_id is None:
            return {"query": query, "found": False, "match": "none",
                    "message": "No matching concept found.",
                    "candidates": self.candidates(query)}

        term = self.db.one("SELECT * FROM term WHERE term_id=?", (term_id,))
        meaning = self.db.one(
            "SELECT * FROM meaning WHERE term_id=? AND is_current=1 ORDER BY confidence_score DESC LIMIT 1",
            (term_id,),
        )
        related = self.db.query(
            "SELECT CASE WHEN r.term_a=? THEN r.term_b ELSE r.term_a END AS other_id, "
            " r.relationship_type, r.strength, r.confidence "
            "FROM term_relationship r WHERE r.term_a=? OR r.term_b=? "
            "ORDER BY r.strength DESC LIMIT 12",
            (term_id, term_id, term_id),
        )
        related_out = []
        for r in related:
            o = self.db.one("SELECT term FROM term WHERE term_id=?", (r["other_id"],))
            if o:
                related_out.append({
                    "term": o["term"], "type": r["relationship_type"],
                    "strength": r["strength"], "confidence": r["confidence"],
                })
        evidence = self.db.query(
            "SELECT e.evidence_text, e.evidence_type, e.weight, c.title, c.source, e.chat_id "
            "FROM evidence e LEFT JOIN chat c ON c.chat_id=e.chat_id "
            "WHERE e.term_id=? ORDER BY e.weight DESC LIMIT 10",
            (term_id,),
        )
        evidence_out = [dict(r) for r in evidence]
        chats = self.db.query(
            "SELECT DISTINCT c.chat_id, c.title, c.source FROM evidence e "
            "JOIN chat c ON c.chat_id=e.chat_id WHERE e.term_id=? LIMIT 12",
            (term_id,),
        )
        evolution = self.db.query(
            "SELECT version, meaning, confidence, created_date FROM concept_version "
            "WHERE term_id=? ORDER BY version", (term_id,),
        )
        return {
            "query": query,
            "found": True,
            "match": match,
            "term": term["term"],
            "term_id": term_id,
            "occurrences": term["occurrence_count"],
            "meaning": meaning["meaning_text"] if meaning else None,
            "understanding": meaning["understanding_text"] if meaning else None,
            "confidence": meaning["confidence_score"] if meaning else None,
            "related": related_out,
            "evidence": evidence_out,
            "source_chats": [dict(r) for r in chats],
            "evolution": [dict(r) for r in evolution],
            "candidates": self.candidates(query),
        }

    def briefing_text(self, query: str) -> str:
        """Render the packet as a plain-text briefing for a cloud model."""
        p = self.packet(query)
        if not p.get("found"):
            cands = ", ".join(c["term"] for c in p.get("candidates", [])[:6])
            return f"No authoritative concept found for '{query}'." + (f" Closest: {cands}." if cands else "")
        lines = []
        lines.append(f"CONCEPT: {p['term']}")
        if p.get("confidence") is not None:
            lines.append(f"CONFIDENCE: {p['confidence']} (evidence-based interpretation, not absolute truth)")
        lines.append(f"OCCURRENCES: {p['occurrences']} | matched via {p['match']}")
        if p.get("meaning"):
            lines.append(f"MEANING: {p['meaning']}")
        if p.get("understanding"):
            lines.append(f"UNDERSTANDING: {p['understanding']}")
        if p.get("related"):
            rel = ", ".join(f"{r['term']} ({r['type']})" for r in p["related"][:8])
            lines.append(f"RELATED: {rel}")
        if p.get("evolution") and len(p["evolution"]) > 1:
            ev = "; ".join(f"v{e['version']}: {(e['meaning'] or 'n/a')[:60]}" for e in p["evolution"])
            lines.append(f"EVOLUTION: {ev}")
        if p.get("evidence"):
            lines.append("EVIDENCE:")
            for e in p["evidence"][:6]:
                src = e.get("title") or e.get("source") or "chat"
                lines.append(f"  - [{src}] {e['evidence_text']}")
        return "\n".join(lines)
