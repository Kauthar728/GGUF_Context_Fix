-- Continuum knowledge engine schema
-- Implements the 17-layer spec: raw chats -> turns -> terms -> meanings ->
-- evidence -> relationships -> concept evolution -> training pairs -> model versions.
-- The database is the source of truth. The embedding model is the finder.

PRAGMA foreign_keys = ON;

-- LAYER 01: Raw chat storage. Never modified. Permanent source evidence.
CREATE TABLE IF NOT EXISTS chat (
    chat_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    source        TEXT,            -- e.g. ChatGPT, Cascade, Devin, unknown
    file_path     TEXT,
    title         TEXT,
    created_date  TEXT,            -- file mtime / parsed date if any
    import_date   TEXT NOT NULL,
    raw_markdown  TEXT NOT NULL,
    raw_text      TEXT,
    hash          TEXT UNIQUE      -- dedupe identical files
);

-- LAYER 02: Turn storage. One row per message.
CREATE TABLE IF NOT EXISTS turn (
    turn_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_id     INTEGER NOT NULL REFERENCES chat(chat_id) ON DELETE CASCADE,
    sequence    INTEGER NOT NULL,
    speaker     TEXT NOT NULL,     -- user | assistant | system
    message     TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_turn_chat ON turn(chat_id);
CREATE INDEX IF NOT EXISTS idx_turn_speaker ON turn(speaker);

-- LAYER 03: Term extraction. Unique meaning-bearing concepts only.
CREATE TABLE IF NOT EXISTS term (
    term_id          INTEGER PRIMARY KEY AUTOINCREMENT,
    term             TEXT NOT NULL,
    normalized_term  TEXT NOT NULL UNIQUE,
    first_seen       TEXT,
    last_seen        TEXT,
    occurrence_count INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_term_norm ON term(normalized_term);

-- LAYER 04: Meaning storage. Meaning is not the word.
CREATE TABLE IF NOT EXISTS meaning (
    meaning_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    term_id           INTEGER NOT NULL REFERENCES term(term_id) ON DELETE CASCADE,
    meaning_text      TEXT,
    understanding_text TEXT,
    summary_text      TEXT,
    created_date      TEXT NOT NULL,
    confidence_score  REAL NOT NULL DEFAULT 0.0,
    is_current        INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_meaning_term ON meaning(term_id);

-- LAYER 05: Evidence storage. Why a meaning exists -> traceable to source.
CREATE TABLE IF NOT EXISTS evidence (
    evidence_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    meaning_id    INTEGER REFERENCES meaning(meaning_id) ON DELETE CASCADE,
    term_id       INTEGER REFERENCES term(term_id) ON DELETE CASCADE,
    turn_id       INTEGER REFERENCES turn(turn_id) ON DELETE SET NULL,
    chat_id       INTEGER REFERENCES chat(chat_id) ON DELETE SET NULL,
    evidence_text TEXT,
    evidence_type TEXT,            -- DirectDefinition | UserDefinition | AssistantDefinition | RepeatedUsage | CodeUsage | ArchitectureUsage
    weight        REAL NOT NULL DEFAULT 1.0
);
CREATE INDEX IF NOT EXISTS idx_evidence_term ON evidence(term_id);
CREATE INDEX IF NOT EXISTS idx_evidence_meaning ON evidence(meaning_id);

-- LAYER 06: Relationships. Connect concepts -> graph edges.
CREATE TABLE IF NOT EXISTS term_relationship (
    relationship_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    term_a            INTEGER NOT NULL REFERENCES term(term_id) ON DELETE CASCADE,
    term_b            INTEGER NOT NULL REFERENCES term(term_id) ON DELETE CASCADE,
    relationship_type TEXT NOT NULL DEFAULT 'RelatedTo',
    strength          REAL NOT NULL DEFAULT 0.0,  -- co-occurrence count / normalized
    confidence        REAL NOT NULL DEFAULT 0.0,
    UNIQUE(term_a, term_b, relationship_type)
);
CREATE INDEX IF NOT EXISTS idx_rel_a ON term_relationship(term_a);
CREATE INDEX IF NOT EXISTS idx_rel_b ON term_relationship(term_b);

-- LAYER 09: Concept evolution. Never overwrite history.
CREATE TABLE IF NOT EXISTS concept_version (
    version_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    term_id      INTEGER NOT NULL REFERENCES term(term_id) ON DELETE CASCADE,
    version      INTEGER NOT NULL,
    meaning      TEXT,
    confidence   REAL NOT NULL DEFAULT 0.0,
    created_date TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_cversion_term ON concept_version(term_id);

-- LAYER 10: Training dataset. Anchor / positive / negative pairs.
CREATE TABLE IF NOT EXISTS training_pair (
    pair_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    anchor    TEXT NOT NULL,
    positive  TEXT NOT NULL,
    negative  TEXT,
    source    TEXT,
    dataset_version INTEGER NOT NULL DEFAULT 1,
    created_date TEXT NOT NULL
);

-- LAYER 13: Embedding model versioning. Models are immutable.
CREATE TABLE IF NOT EXISTS embedding_model_version (
    version_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    version_name   TEXT NOT NULL,
    base_model     TEXT,
    training_date  TEXT NOT NULL,
    dataset_version INTEGER,
    pair_count     INTEGER,
    accuracy       REAL,
    model_path     TEXT,
    is_active      INTEGER NOT NULL DEFAULT 0,
    notes          TEXT
);

-- Term embeddings (vectors) for semantic retrieval.
CREATE TABLE IF NOT EXISTS term_embedding (
    term_id        INTEGER NOT NULL REFERENCES term(term_id) ON DELETE CASCADE,
    model_version  TEXT NOT NULL,
    dim            INTEGER NOT NULL,
    vector         BLOB NOT NULL,
    PRIMARY KEY (term_id, model_version)
);

-- Application settings (themes, config, GUI state).
CREATE TABLE IF NOT EXISTS setting (
    key   TEXT PRIMARY KEY,
    value TEXT
);

-- Lightweight event log (LAYER: node_events analogue) for auditability.
CREATE TABLE IF NOT EXISTS event_log (
    event_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    ts         TEXT NOT NULL,
    kind       TEXT NOT NULL,
    detail     TEXT
);
