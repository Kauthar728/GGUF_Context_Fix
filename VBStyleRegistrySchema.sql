-- VB-Style Registry Schema for C Build System
-- This schema implements a registry-based build system that treats C files as isolated units
-- with explicit dependencies, enabling incremental compilation similar to managed language runtimes.

PRAGMA foreign_keys = ON;

-- Units table: Each row represents a compilable unit (typically a .c file with its interface)
CREATE TABLE IF NOT EXISTS units (
    unit_id INTEGER PRIMARY KEY AUTOINCREMENT,
    unit_name TEXT NOT NULL UNIQUE,  -- e.g., "CASCADE_Coordinator", "CASCADE_BackendEngine"
    source_path TEXT NOT NULL,       -- Path to .c file
    header_path TEXT,                -- Path to .h file (if exists, for interface)
    description TEXT,                -- Human-readable description of unit responsibility
    created_at TEXT NOT NULL,        -- Timestamp when unit was registered
    updated_at TEXT NOT NULL,        -- Timestamp when unit metadata last changed
    is_active BOOLEAN NOT NULL DEFAULT 1  -- Whether unit is part of current build
);

-- UnitInterfaces table: Explicit interface definition (replaces header file guessing)
CREATE TABLE IF NOT EXISTS unit_interfaces (
    interface_id INTEGER PRIMARY KEY AUTOINCREMENT,
    unit_id INTEGER NOT NULL REFERENCES units(unit_id) ON DELETE CASCADE,
    interface_name TEXT NOT NULL,    -- e.g., "coordinator_create", "backend_search"
    interface_type TEXT NOT NULL,    -- "function", "struct", "enum", "typedef"
    return_type TEXT,                -- For functions: return type
    parameters TEXT,                 -- Serialized parameter list (JSON or simple string)
    is_public BOOLEAN NOT NULL DEFAULT 1,  -- Whether interface is visible to other units
    defined_in_header BOOLEAN NOT NULL DEFAULT 0,  -- Whether declared in .h file
    UNIQUE(unit_id, interface_name)
);

-- Dependencies table: Explicit dependency relationships between units
CREATE TABLE IF NOT EXISTS unit_dependencies (
    dep_id INTEGER PRIMARY KEY AUTOINCREMENT,
    dependent_unit_id INTEGER NOT NULL REFERENCES units(unit_id) ON DELETE CASCADE,
    dependency_unit_id INTEGER NOT NULL REFERENCES units(unit_id) ON DELETE CASCADE,
    dependency_type TEXT NOT NULL,   -- "header_include", "function_call", "type_usage", "extern_variable"
    dependency_strength TEXT NOT NULL, -- "strong", "weak" (for change propagation rules)
    detected_at TEXT NOT NULL,       -- When dependency was discovered
    is_explicit BOOLEAN NOT NULL DEFAULT 0,  -- Whether dependency was declared explicitly vs discovered
    UNIQUE(dependent_unit_id, dependency_unit_id, dependency_type)
);

-- CompileStates table: Tracks compilation state and artifacts for each unit
CREATE TABLE IF NOT EXISTS unit_compile_states (
    state_id INTEGER PRIMARY KEY AUTOINCREMENT,
    unit_id INTEGER NOT NULL REFERENCES units(unit_id) ON DELETE CASCADE,
    artifact_path TEXT NOT NULL,     -- Path to compiled .o file
    source_hash TEXT NOT NULL,       -- Hash of source file content (for change detection)
    interface_hash TEXT NOT NULL,    -- Hash of interface (headers + explicit interfaces)
    dependencies_hash TEXT NOT NULL, -- Hash of all dependency states (for transitive change detection)
    compiler_command TEXT NOT NULL,  -- Full command used to compile this unit
    compiler_version TEXT,           -- Version of compiler used
    compile_timestamp TEXT NOT NULL, -- When this compilation occurred
    compile_status TEXT NOT NULL,    -- "success", "failed", "pending"
    error_message TEXT,              -- If compilation failed, the error message
    is_current BOOLEAN NOT NULL DEFAULT 0,  -- Whether this is the latest successful compile
    FOREIGN KEY (unit_id) REFERENCES units(unit_id) ON DELETE CASCADE
);

-- BuildJobs table: Tracks build requests and execution
CREATE TABLE IF NOT EXISTS build_jobs (
    job_id INTEGER PRIMARY KEY AUTOINCREMENT,
    trigger_reason TEXT NOT NULL,    -- "manual", "file_change", "dependency_change"
    requested_at TEXT NOT NULL,      -- When build was requested
    started_at TEXT,                 -- When build actually started
    completed_at TEXT,               -- When build finished
    status TEXT NOT NULL,            -- "queued", "running", "success", "failed", "cancelled"
    trigger_details TEXT,            -- JSON details about what triggered the build
    units_compiled INTEGER DEFAULT 0,-- How many units were compiled in this job
    units_failed INTEGER DEFAULT 0   -- How many units failed compilation
);

-- BuildJobUnits table: Many-to-many relationship between jobs and units
CREATE TABLE IF NOT EXISTS build_job_units (
    job_unit_id INTEGER PRIMARY KEY AUTOINCREMENT,
    job_id INTEGER NOT NULL REFERENCES build_jobs(job_id) ON DELETE CASCADE,
    unit_id INTEGER NOT NULL REFERENCES units(unit_id) ON DELETE CASCADE,
    compile_state_id INTEGER REFERENCES unit_compile_states(state_id) ON DELETE SET NULL,
    status TEXT NOT NULL,            -- "pending", "compiling", "success", "failed"
    start_time TEXT,                 -- When compilation of this unit started
    end_time TEXT,                   -- When compilation of this unit ended
    UNIQUE(job_id, unit_id)
);

-- Configuration table: System-wide settings for the build system
CREATE TABLE IF NOT EXISTS build_configuration (
    config_key TEXT PRIMARY KEY,
    config_value TEXT NOT NULL,
    description TEXT,
    updated_at TEXT NOT NULL
);

-- Indexes for performance
CREATE INDEX IF NOT EXISTS idx_units_name ON units(unit_name);
CREATE INDEX IF NOT EXISTS idx_units_active ON units(is_active);
CREATE INDEX IF NOT EXISTS idx_unit_interfaces_unit ON unit_interfaces(unit_id);
CREATE INDEX IF NOT EXISTS idx_unit_interfaces_name ON unit_interfaces(interface_name);
CREATE INDEX IF NOT EXISTS idx_unit_dependencies_dependent ON unit_dependencies(dependent_unit_id);
CREATE INDEX IF NOT EXISTS idx_unit_dependencies_dependency ON unit_dependencies(dependency_unit_id);
CREATE INDEX IF NOT EXISTS idx_unit_compile_states_unit ON unit_compile_states(unit_id);
CREATE INDEX IF NOT EXISTS idx_unit_compile_states_current ON unit_compile_states(is_current);
CREATE INDEX IF NOT EXISTS idx_build_jobs_status ON build_jobs(status);
CREATE INDEX IF NOT EXISTS idx_build_job_units_job ON build_job_units(job_id);
CREATE INDEX IF NOT EXISTS idx_build_job_units_unit ON build_job_units(unit_id);

-- Insert default configuration
INSERT OR IGNORE INTO build_configuration (config_key, config_value, description, updated_at)
VALUES 
    ('compiler', 'gcc', 'Default compiler to use', datetime('now')),
    ('compiler_flags', '-Wall -Wextra -O2', 'Default compiler flags', datetime('now')),
    ('linker_flags', '', 'Default linker flags', datetime('now')),
    ('enable_dependency_tracking', '1', 'Whether to track and use dependencies for incremental builds', datetime('now')),
    ('build_cache_dir', './build', 'Directory for storing compiled artifacts', datetime('now')),
    ('hash_algorithm', 'sha256', 'Algorithm used for hashing source files', datetime('now'));