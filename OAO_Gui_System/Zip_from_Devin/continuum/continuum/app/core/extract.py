"""Concept extraction: turns -> terms -> meanings -> evidence -> relationships.

Pure-Python, no model required. The goal is not perfect NLP; it is to convert
raw conversation into a structured, traceable knowledge surface that a model can
later reason over. Everything produced here points back at the turn/chat it came
from (evidence), so nothing is a black box.

Pipeline:
  1. discover meaning-bearing terms (CamelCase, Capitalised phrases, quoted/code
     identifiers, ALLCAPS), ignoring common filler words.
  2. attach definitional meanings via patterns ("X is ...", "X means ...",
     "X = ...", "X: ...").
  3. record evidence (the exact turn text) for every meaning and usage.
  4. derive relationships from co-occurrence within turns + explicit phrases
     ("X connects to Y", "X is part of Y").
  5. score confidence from frequency, evidence count and definition presence.
  6. snapshot a concept_version row so meaning evolution is preserved.
"""
from __future__ import annotations

import re
import math
from collections import defaultdict, Counter
from typing import Iterable

from .db import DB, now_iso

# Words we never want to treat as concepts.
STOPWORDS = set("""
a an the and or but if then else when while for to of in on at by with from into
over under again further is am are was were be been being do does did doing have
has had having i you he she it we they them his her its our your their this that
these those there here what which who whom whose why how all any both each few more
most other some such no nor not only own same so than too very can will just don
should now about above below up down out off as well also like get got make made
use used using one two three first second new old way thing things something anything
everything nothing maybe really actually basically literally okay ok yeah yes no
because would could may might must shall let lets going gonna want need know think
mean means said say says talk talking chat model data code user file table database
""".split())

# Patterns that introduce a definition. group 'term' then group 'def'.
_DEF_PATTERNS = [
    re.compile(r"\b(?P<term>[A-Za-z][\w .\-]{1,40}?)\s+(?:is|are)\s+(?:a|an|the)?\s*(?P<def>[^.!?\n]{3,200})", re.I),
    re.compile(r"\b(?P<term>[A-Za-z][\w .\-]{1,40}?)\s+means\s+(?P<def>[^.!?\n]{3,200})", re.I),
    re.compile(r"\b(?P<term>[A-Za-z][\w .\-]{1,40}?)\s*[:=]\s*(?P<def>[^.!?\n]{3,200})"),
    re.compile(r"\b(?P<term>[A-Za-z][\w .\-]{1,40}?)\s*[\u2014\-]\s*(?P<def>[^.!?\n]{6,200})"),
]

# Explicit relationship phrases.
_REL_PATTERNS = [
    (re.compile(r"\bconnect(?:s|ed)?\s+to\b", re.I), "ConnectsTo"),
    (re.compile(r"\bpart of\b", re.I), "PartOf"),
    (re.compile(r"\bcontains?\b", re.I), "Contains"),
    (re.compile(r"\bdepends? on\b", re.I), "DependsOn"),
    (re.compile(r"\buses?\b", re.I), "Uses"),
    (re.compile(r"\bcreates?\b", re.I), "Creates"),
    (re.compile(r"\bextends?\b", re.I), "Extends"),
    (re.compile(r"\bsame as\b|\bequals?\b|\baka\b", re.I), "Equals"),
]

# Term candidate patterns.
_CAMEL = re.compile(r"\b([A-Z][a-z0-9]+(?:[A-Z][a-z0-9]+)+)\b")          # MemUnit, TruthSurface
_ALLCAPS = re.compile(r"\b([A-Z]{2,}(?:[A-Z0-9]{0,})\b)")                  # CASOS, VBSTYLE, AST
_SNAKE = re.compile(r"\b([a-z][a-z0-9]+(?:_[a-z0-9]+)+)\b")                # config_setup, load_config
_QUOTED = re.compile(r"[\"\u201c\u2018`]([A-Za-z][\w .\-]{1,40}?)[\"\u201d\u2019`]")
_CAP_PHRASE = re.compile(r"\b((?:[A-Z][a-z]+)(?:\s+[A-Z][a-z]+){0,2})\b")  # Truth Surface, Memory Bus


def normalize(term: str) -> str:
    return re.sub(r"\s+", " ", term.strip()).lower()


def _is_good_term(t: str) -> bool:
    t = t.strip()
    if len(t) < 3 or len(t) > 48:
        return False
    low = t.lower()
    if low in STOPWORDS:
        return False
    # reject phrases that are entirely stopwords
    words = [w for w in re.split(r"[\s_]+", low) if w]
    if words and all(w in STOPWORDS for w in words):
        return False
    if not re.search(r"[A-Za-z]", t):
        return False
    return True


def candidate_terms(text: str) -> Counter:
    c: Counter = Counter()
    for rx, weight in [(_CAMEL, 3), (_ALLCAPS, 3), (_SNAKE, 2), (_QUOTED, 2)]:
        for m in rx.finditer(text):
            cand = m.group(1)
            if _is_good_term(cand):
                c[cand] += weight
    # Capitalised multi-word phrases (lower weight, noisier).
    for m in _CAP_PHRASE.finditer(text):
        cand = _strip_article(m.group(1))
        if " " in cand and _is_good_term(cand):
            c[cand] += 1
    return c


def _strip_article(phrase: str) -> str:
    return re.sub(r"^(?:the|a|an)\s+", "", phrase.strip(), flags=re.IGNORECASE)


def _clean_def(d: str) -> str:
    d = d.strip(" \t-:\u2014\u2013")
    d = re.sub(r"\s+", " ", d)
    return d[:240]


class Extractor:
    def __init__(self, db: DB):
        self.db = db

    # -- main entrypoint --------------------------------------------------
    def run(self, min_occurrences: int = 2) -> dict:
        turns = self.db.query("SELECT turn_id, chat_id, speaker, message FROM turn ORDER BY turn_id")
        if not turns:
            return {"terms": 0, "meanings": 0, "relationships": 0, "evidence": 0}

        global_counts: Counter = Counter()
        term_turns: dict[str, list] = defaultdict(list)  # display_term -> [(turn_id, chat_id, speaker, msg)]
        # First pass: count candidate terms across all turns.
        for row in turns:
            msg = row["message"]
            cands = candidate_terms(msg)
            seen = set()
            for cand in cands:
                global_counts[cand] += cands[cand]
                key = normalize(cand)
                if key not in seen:
                    term_turns[cand].append((row["turn_id"], row["chat_id"], row["speaker"], msg))
                    seen.add(key)

        # Merge display variants that normalize to the same key; keep the most
        # frequent surface form as canonical.
        canon: dict[str, str] = {}
        by_norm: dict[str, list[str]] = defaultdict(list)
        for cand in global_counts:
            by_norm[normalize(cand)].append(cand)
        for norm, variants in by_norm.items():
            variants.sort(key=lambda v: (-global_counts[v], v))
            canon[norm] = variants[0]

        # Filter by occurrence threshold (sum of variant counts).
        norm_total: Counter = Counter()
        for cand, n in global_counts.items():
            norm_total[normalize(cand)] += n
        keep = {norm for norm, n in norm_total.items() if n >= min_occurrences}

        n_terms = n_meanings = n_rel = n_evidence = 0
        term_id_by_norm: dict[str, int] = {}

        for norm in keep:
            disp = canon[norm]
            # gather all turns for any variant of this norm
            turns_for: list = []
            for variant in by_norm[norm]:
                turns_for.extend(term_turns.get(variant, []))
            # dedupe by turn_id
            uniq = {}
            for t in turns_for:
                uniq[t[0]] = t
            turns_for = list(uniq.values())
            occ = norm_total[norm]
            tid = self._upsert_term(disp, norm, occ)
            term_id_by_norm[norm] = tid
            n_terms += 1

            # meanings from definitional patterns within those turns
            best_meanings = self._find_meanings(disp, turns_for)
            meaning_id = None
            if best_meanings:
                meaning_id, conf = self._store_meaning(tid, disp, best_meanings)
                n_meanings += 1
                self._snapshot_version(tid, best_meanings[0][0], conf)
            else:
                conf = self._confidence(occ, 0, len(turns_for))
                self._snapshot_version(tid, None, conf)

            # evidence: link representative turns
            for (turn_id, chat_id, speaker, msg) in turns_for[:8]:
                etype = "UserDefinition" if speaker == "user" else "AssistantDefinition"
                snippet = self._snippet(msg, disp)
                self.db.execute(
                    "INSERT INTO evidence(meaning_id, term_id, turn_id, chat_id, evidence_text, evidence_type, weight)"
                    " VALUES (?,?,?,?,?,?,?)",
                    (meaning_id, tid, turn_id, chat_id, snippet, etype, 1.0),
                )
                n_evidence += 1
        self.db.commit()

        # relationships via co-occurrence within turns
        n_rel = self._build_relationships(term_id_by_norm)
        self.db.commit()
        self.db.log("extract", f"terms={n_terms} meanings={n_meanings} rel={n_rel} evidence={n_evidence}")
        return {"terms": n_terms, "meanings": n_meanings, "relationships": n_rel, "evidence": n_evidence}

    # -- helpers ----------------------------------------------------------
    def _upsert_term(self, disp: str, norm: str, occ: int) -> int:
        row = self.db.one("SELECT term_id FROM term WHERE normalized_term=?", (norm,))
        ts = now_iso()
        if row:
            tid = int(row["term_id"])
            self.db.execute(
                "UPDATE term SET occurrence_count=?, last_seen=?, term=? WHERE term_id=?",
                (occ, ts, disp, tid),
            )
            return tid
        cur = self.db.execute(
            "INSERT INTO term(term, normalized_term, first_seen, last_seen, occurrence_count)"
            " VALUES (?,?,?,?,?)",
            (disp, norm, ts, ts, occ),
        )
        return int(cur.lastrowid)

    def _find_meanings(self, disp: str, turns_for: list) -> list[tuple[str, str]]:
        """Return [(definition, source_speaker)] sorted by quality.

        Searches specifically for '<term> is/are/means/=/: <definition>' so we
        anchor on the actual term rather than letting a greedy regex grab the
        whole sentence.
        """
        term_rx = re.compile(
            r"\b" + re.escape(disp) + r"\b\s*(?:is|are|means|refers to|=|:|\u2014|\u2013|-)\s+"
            r"(?:a|an|the|basically|essentially|just|like)?\s*(?P<def>[^.!?\n]{4,220})",
            re.IGNORECASE,
        )
        found: list[tuple[str, str, int]] = []  # def, speaker, score
        for (turn_id, chat_id, speaker, msg) in turns_for:
            for m in term_rx.finditer(msg):
                d = _clean_def(m.group("def"))
                if len(d) < 4 or normalize(d) == normalize(disp):
                    continue
                score = len(d)
                if speaker == "user":
                    score += 50  # user definitions are authoritative
                found.append((d, speaker, score))
        found.sort(key=lambda x: -x[2])
        # dedupe similar
        out: list[tuple[str, str]] = []
        seen: set[str] = set()
        for d, sp, _ in found:
            k = d.lower()[:60]
            if k in seen:
                continue
            seen.add(k)
            out.append((d, sp))
            if len(out) >= 5:
                break
        return out

    def _store_meaning(self, tid: int, disp: str, meanings: list[tuple[str, str]]):
        best_def, speaker = meanings[0]
        understanding = " | ".join(d for d, _ in meanings[1:4])
        occ_row = self.db.one("SELECT occurrence_count FROM term WHERE term_id=?", (tid,))
        occ = int(occ_row["occurrence_count"]) if occ_row else 0
        conf = self._confidence(occ, len(meanings), occ, has_user_def=any(s == "user" for _, s in meanings))
        # mark older meanings non-current
        self.db.execute("UPDATE meaning SET is_current=0 WHERE term_id=?", (tid,))
        cur = self.db.execute(
            "INSERT INTO meaning(term_id, meaning_text, understanding_text, summary_text, created_date, confidence_score, is_current)"
            " VALUES (?,?,?,?,?,?,1)",
            (tid, best_def, understanding, best_def[:120], now_iso(), conf),
        )
        return int(cur.lastrowid), conf

    def _snapshot_version(self, tid: int, meaning: str | None, conf: float) -> None:
        row = self.db.one("SELECT COALESCE(MAX(version),0) AS v FROM concept_version WHERE term_id=?", (tid,))
        nextv = int(row["v"]) + 1 if row else 1
        self.db.execute(
            "INSERT INTO concept_version(term_id, version, meaning, confidence, created_date) VALUES (?,?,?,?,?)",
            (tid, nextv, meaning, conf, now_iso()),
        )

    @staticmethod
    def _confidence(occ: int, n_meanings: int, n_evidence: int, has_user_def: bool = False) -> float:
        # logistic-ish blend, capped 0..1
        base = 1 - math.exp(-occ / 8.0)          # frequency saturates
        defs = min(0.25, 0.08 * n_meanings)       # definitions add certainty
        user = 0.15 if has_user_def else 0.0
        ev = min(0.15, 0.02 * n_evidence)
        return round(min(0.99, 0.2 + 0.45 * base + defs + user + ev), 3)

    @staticmethod
    def _snippet(msg: str, term: str, width: int = 160) -> str:
        idx = msg.lower().find(term.lower())
        if idx < 0:
            return msg[:width].strip()
        start = max(0, idx - width // 3)
        end = min(len(msg), idx + len(term) + width)
        s = msg[start:end].strip()
        return ("\u2026" if start > 0 else "") + re.sub(r"\s+", " ", s) + ("\u2026" if end < len(msg) else "")

    def _build_relationships(self, term_id_by_norm: dict[str, int]) -> int:
        norms = list(term_id_by_norm.keys())
        if not norms:
            return 0
        # precompile term matchers
        pair_counts: Counter = Counter()
        pair_type: dict[tuple[int, int], str] = {}
        turns = self.db.query("SELECT message FROM turn ORDER BY turn_id")
        # Build quick lookup of which terms appear in a message.
        for row in turns:
            msg = row["message"]
            low = msg.lower()
            present = [n for n in norms if n in low]
            if len(present) < 2:
                continue
            # limit combinatorial blowup
            present = present[:25]
            for i in range(len(present)):
                for j in range(i + 1, len(present)):
                    a, b = sorted((term_id_by_norm[present[i]], term_id_by_norm[present[j]]))
                    pair_counts[(a, b)] += 1
                    if (a, b) not in pair_type:
                        rtype = "RelatedTo"
                        for rx, name in _REL_PATTERNS:
                            if rx.search(msg):
                                rtype = name
                                break
                        pair_type[(a, b)] = rtype
        n = 0
        if not pair_counts:
            return 0
        maxc = max(pair_counts.values())
        for (a, b), c in pair_counts.items():
            if c < 2:  # require co-occurrence at least twice
                continue
            strength = round(c / maxc, 3)
            conf = round(min(0.95, 0.3 + 0.6 * strength), 3)
            self.db.execute(
                "INSERT INTO term_relationship(term_a, term_b, relationship_type, strength, confidence)"
                " VALUES (?,?,?,?,?) ON CONFLICT(term_a, term_b, relationship_type)"
                " DO UPDATE SET strength=excluded.strength, confidence=excluded.confidence",
                (a, b, pair_type.get((a, b), "RelatedTo"), strength, conf),
            )
            n += 1
        return n
