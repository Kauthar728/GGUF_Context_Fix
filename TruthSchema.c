/*
#   Ghost[TruthSchema:TruthSchema Domain:Storage Purpose:Structured_Truth_Tables
#   Owns:Classes|Methods|Files|Domains|Relationships|Timestamps
#   Accepts:schema_data Returns:table_operations|query_capabilities
#   Requires:SQLite3 Exposes:create_truth_tables|insert_class|insert_method|insert_file|insert_domain
#   State:schema_version|table_definitions Health:schema_integrity Tags:truth,schema,structured,relationships,evolution ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#define TRUTHSCHEMA_VERSION "1.0.0"
#define TRUTHSCHEMA_MAX_SQL 8192
#define TRUTHSCHEMA_MAX_KEY 256
#define TRUTHSCHEMA_MAX_VALUE 8192

/*##section##[ResultT|3TupleContract]*/
typedef struct TruthSchema_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthSchema_Result;

/*##section##[State|WorldContainer]*/
typedef struct TruthSchema_State {
    sqlite3 * db;
    int is_booted;
    char last_message[TRUTHSCHEMA_MAX_KEY];
    int operation_count;
    time_t boot_time;
} TruthSchema_State;

/*##method##[TruthSchema|create|allocation|constructor]*/
/*{[Input:none][Output:TruthSchema_State_ptr][Side_effects:allocates_state][Pattern:param_validate_execute]}*/
TruthSchema_State * TruthSchema_create(void) {
    TruthSchema_State * self = (TruthSchema_State *)calloc(1, sizeof(TruthSchema_State));
    return self;
}

/*##method##[TruthSchema|boot|lifecycle|initialization]*/
/*{[Input:db_ptr][Output:status][Side_effects:creates_all_truth_tables][Pattern:param_validate_execute]}*/
int TruthSchema_boot(TruthSchema_State * self, sqlite3 * db) {
    char sql[TRUTHSCHEMA_MAX_SQL];
    char * err = NULL;
    int rc;
    
    if (!self || !db) return -1;
    memset(self, 0, sizeof(*self));
    self->db = db;
    
    snprintf(sql, sizeof(sql),
        /* Layer 1: Truth Tables */
        "CREATE TABLE IF NOT EXISTS truth_classes("
        "class_id INTEGER PRIMARY KEY,"
        "class_name TEXT NOT NULL,"
        "file_path TEXT NOT NULL,"
        "domain TEXT NOT NULL,"
        "authority TEXT NOT NULL,"
        "description TEXT,"
        "created_at INTEGER DEFAULT(unixepoch()),"
        "updated_at INTEGER DEFAULT(unixepoch()),"
        "is_active INTEGER DEFAULT 1);"
        
        "CREATE TABLE IF NOT EXISTS truth_methods("
        "method_id INTEGER PRIMARY KEY,"
        "class_id INTEGER NOT NULL,"
        "method_name TEXT NOT NULL,"
        "authority TEXT NOT NULL,"
        "bracket_sig TEXT,"
        "magnetic_sig TEXT,"
        "created_at INTEGER DEFAULT(unixepoch()),"
        "updated_at INTEGER DEFAULT(unixepoch()),"
        "is_active INTEGER DEFAULT 1,"
        "FOREIGN KEY(class_id) REFERENCES truth_classes(class_id));"
        
        "CREATE TABLE IF NOT EXISTS truth_files("
        "file_id INTEGER PRIMARY KEY,"
        "path TEXT NOT NULL UNIQUE,"
        "hash TEXT,"
        "language TEXT,"
        "line_count INTEGER DEFAULT 0,"
        "created_at INTEGER DEFAULT(unixepoch()),"
        "updated_at INTEGER DEFAULT(unixepoch()),"
        "is_active INTEGER DEFAULT 1);"
        
        "CREATE TABLE IF NOT EXISTS truth_domains("
        "domain_id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL UNIQUE,"
        "authority TEXT NOT NULL,"
        "description TEXT,"
        "created_at INTEGER DEFAULT(unixepoch()),"
        "updated_at INTEGER DEFAULT(unixepoch()),"
        "is_active INTEGER DEFAULT 1);"
        
        /* Layer 2: Relationship Graph */
        "CREATE TABLE IF NOT EXISTS truth_relationships("
        "rel_id INTEGER PRIMARY KEY,"
        "source_type TEXT NOT NULL,"
        "source_id INTEGER NOT NULL,"
        "target_type TEXT NOT NULL,"
        "target_id INTEGER NOT NULL,"
        "rel_type TEXT NOT NULL,"
        "strength REAL DEFAULT 0.5,"
        "created_at INTEGER DEFAULT(unixepoch()),"
        "is_active INTEGER DEFAULT 1);"
        
        /* Layer 3: Embeddings */
        "CREATE TABLE IF NOT EXISTS truth_embeddings("
        "embed_id INTEGER PRIMARY KEY,"
        "entity_type TEXT NOT NULL,"
        "entity_id INTEGER NOT NULL,"
        "vector BLOB,"
        "model TEXT NOT NULL,"
        "created_at INTEGER DEFAULT(unixepoch()),"
        "is_active INTEGER DEFAULT 1);"
        
        /* Layer 4: Historical Knowledge */
        "CREATE TABLE IF NOT EXISTS truth_decisions("
        "decision_id INTEGER PRIMARY KEY,"
        "problem TEXT NOT NULL,"
        "discussion TEXT,"
        "decision TEXT NOT NULL,"
        "result TEXT,"
        "outcome TEXT,"
        "context TEXT,"
        "created_at INTEGER DEFAULT(unixepoch()),"
        "reversed_at INTEGER,"
        "reversed_reason TEXT,"
        "is_active INTEGER DEFAULT 1);"
        
        /* Indexes */
        "CREATE INDEX IF NOT EXISTS idx_classes_domain ON truth_classes(domain);"
        "CREATE INDEX IF NOT EXISTS idx_classes_authority ON truth_classes(authority);"
        "CREATE INDEX IF NOT EXISTS idx_methods_class ON truth_methods(class_id);"
        "CREATE INDEX IF NOT EXISTS idx_methods_authority ON truth_methods(authority);"
        "CREATE INDEX IF NOT EXISTS idx_files_path ON truth_files(path);"
        "CREATE INDEX IF NOT EXISTS idx_relationships_source ON truth_relationships(source_type,source_id);"
        "CREATE INDEX IF NOT EXISTS idx_relationships_target ON truth_relationships(target_type,target_id);"
        "CREATE INDEX IF NOT EXISTS idx_relationships_type ON truth_relationships(rel_type);"
        "CREATE INDEX IF NOT EXISTS idx_decisions_problem ON truth_decisions(problem);"
        "CREATE INDEX IF NOT EXISTS idx_decisions_active ON truth_decisions(is_active);"
        
        /* Triggers for timestamp updates */
        "CREATE TRIGGER IF NOT EXISTS trigger_classes_updated "
        "AFTER UPDATE ON truth_classes "
        "BEGIN "
        "UPDATE truth_classes SET updated_at=unixepoch() WHERE class_id=NEW.class_id; "
        "END;"
        
        "CREATE TRIGGER IF NOT EXISTS trigger_methods_updated "
        "AFTER UPDATE ON truth_methods "
        "BEGIN "
        "UPDATE truth_methods SET updated_at=unixepoch() WHERE method_id=NEW.method_id; "
        "END;"
        
        "CREATE TRIGGER IF NOT EXISTS trigger_files_updated "
        "AFTER UPDATE ON truth_files "
        "BEGIN "
        "UPDATE truth_files SET updated_at=unixepoch() WHERE file_id=NEW.file_id; "
        "END;"
        
        "CREATE TRIGGER IF NOT EXISTS trigger_domains_updated "
        "AFTER UPDATE ON truth_domains "
        "BEGIN "
        "UPDATE truth_domains SET updated_at=unixepoch() WHERE domain_id=NEW.domain_id; "
        "END;");
    
    rc = sqlite3_exec(self->db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return -2;
    }
    
    self->is_booted = 1;
    self->boot_time = time(NULL);
    snprintf(self->last_message, sizeof(self->last_message), "TruthSchema v%s booted", TRUTHSCHEMA_VERSION);
    
    return 0;
}

/*##method##[TruthSchema|insert_class|storage|class_insertion]*/
/*{[Input:class_name|file_path|domain|authority|description][Output:ResultT][Side_effects:inserts_class_record][Pattern:param_validate_execute]}*/
TruthSchema_Result TruthSchema_insert_class(TruthSchema_State * self, const char * class_name, const char * file_path, const char * domain, const char * authority, const char * description) {
    TruthSchema_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !class_name || !file_path || !domain || !authority) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT INTO truth_classes(class_name,file_path,domain,authority,description) VALUES(?1,?2,?3,?4,?5) RETURNING class_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, class_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, file_path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, domain, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, authority, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, description ? description : "", -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r.count = sqlite3_column_int(stmt, 0);
        r.status = 0;
    } else {
        r.status = -3;
        r.error = strdup("insert_failed");
    }
    
    sqlite3_finalize(stmt);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthSchema|insert_method|storage|method_insertion]*/
/*{[Input:class_id|method_name|authority|bracket_sig|magnetic_sig][Output:ResultT][Side_effects:inserts_method_record][Pattern:param_validate_execute]}*/
TruthSchema_Result TruthSchema_insert_method(TruthSchema_State * self, int class_id, const char * method_name, const char * authority, const char * bracket_sig, const char * magnetic_sig) {
    TruthSchema_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || class_id <= 0 || !method_name || !authority) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT INTO truth_methods(class_id,method_name,authority,bracket_sig,magnetic_sig) VALUES(?1,?2,?3,?4,?5) RETURNING method_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_int(stmt, 1, class_id);
    sqlite3_bind_text(stmt, 2, method_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, authority, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, bracket_sig ? bracket_sig : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, magnetic_sig ? magnetic_sig : "", -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r.count = sqlite3_column_int(stmt, 0);
        r.status = 0;
    } else {
        r.status = -3;
        r.error = strdup("insert_failed");
    }
    
    sqlite3_finalize(stmt);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthSchema|insert_file|storage|file_insertion]*/
/*{[Input:path|hash|language|line_count][Output:ResultT][Side_effects:inserts_file_record][Pattern:param_validate_execute]}*/
TruthSchema_Result TruthSchema_insert_file(TruthSchema_State * self, const char * path, const char * hash, const char * language, int line_count) {
    TruthSchema_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !path) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT OR REPLACE INTO truth_files(path,hash,language,line_count) VALUES(?1,?2,?3,?4) RETURNING file_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash ? hash : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, language ? language : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, line_count);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r.count = sqlite3_column_int(stmt, 0);
        r.status = 0;
    } else {
        r.status = -3;
        r.error = strdup("insert_failed");
    }
    
    sqlite3_finalize(stmt);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthSchema|insert_domain|storage|domain_insertion]*/
/*{[Input:name|authority|description][Output:ResultT][Side_effects:inserts_domain_record][Pattern:param_validate_execute]}*/
TruthSchema_Result TruthSchema_insert_domain(TruthSchema_State * self, const char * name, const char * authority, const char * description) {
    TruthSchema_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !name || !authority) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT OR REPLACE INTO truth_domains(name,authority,description) VALUES(?1,?2,?3) RETURNING domain_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, authority, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, description ? description : "", -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r.count = sqlite3_column_int(stmt, 0);
        r.status = 0;
    } else {
        r.status = -3;
        r.error = strdup("insert_failed");
    }
    
    sqlite3_finalize(stmt);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthSchema|add_relationship|storage|relationship_insertion]*/
/*{[Input:source_type|source_id|target_type|target_id|rel_type|strength][Output:ResultT][Side_effects:inserts_relationship_record][Pattern:param_validate_execute]}*/
TruthSchema_Result TruthSchema_add_relationship(TruthSchema_State * self, const char * source_type, int source_id, const char * target_type, int target_id, const char * rel_type, float strength) {
    TruthSchema_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !source_type || source_id <= 0 || !target_type || target_id <= 0 || !rel_type) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT INTO truth_relationships(source_type,source_id,target_type,target_id,rel_type,strength) VALUES(?1,?2,?3,?4,?5,?6) RETURNING rel_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, source_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, source_id);
    sqlite3_bind_text(stmt, 3, target_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, target_id);
    sqlite3_bind_text(stmt, 5, rel_type, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 6, strength);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r.count = sqlite3_column_int(stmt, 0);
        r.status = 0;
    } else {
        r.status = -3;
        r.error = strdup("insert_failed");
    }
    
    sqlite3_finalize(stmt);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthSchema|record_decision|storage|decision_insertion]*/
/*{[Input:problem|discussion|decision|result|outcome|context][Output:ResultT][Side_effects:inserts_decision_record][Pattern:param_validate_execute]}*/
TruthSchema_Result TruthSchema_record_decision(TruthSchema_State * self, const char * problem, const char * discussion, const char * decision, const char * result, const char * outcome, const char * context) {
    TruthSchema_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !problem || !decision) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT INTO truth_decisions(problem,discussion,decision,result,outcome,context) VALUES(?1,?2,?3,?4,?5,?6) RETURNING decision_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, problem, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, discussion ? discussion : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, decision, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, result ? result : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, outcome ? outcome : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, context ? context : "", -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r.count = sqlite3_column_int(stmt, 0);
        r.status = 0;
    } else {
        r.status = -3;
        r.error = strdup("insert_failed");
    }
    
    sqlite3_finalize(stmt);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthSchema|find_path|inquiry|graph_traversal]*/
/*{[Input:source_type|source_id|target_type|target_id][Output:ResultT][Side_effects:finds_path_using_recursive_cte][Pattern:param_validate_execute]}*/
TruthSchema_Result TruthSchema_find_path(TruthSchema_State * self, const char * source_type, int source_id, const char * target_type, int target_id) {
    TruthSchema_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHSCHEMA_MAX_VALUE];
    char sql[TRUTHSCHEMA_MAX_SQL];
    int len = 0;
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !source_type || source_id <= 0 || !target_type || target_id <= 0) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthSchema:find_path][Source:%s:%d][Target:%s:%d][Path:", source_type, source_id, target_type, target_id);
    
    snprintf(sql, sizeof(sql),
        "WITH RECURSIVE path_cte AS ("
        "SELECT source_type, source_id, target_type, target_id, rel_type, 0 as depth, "
        "CAST(source_type || ':' || source_id || '->' || rel_type || '->' || target_type || ':' || target_id AS TEXT) as path "
        "FROM truth_relationships "
        "WHERE source_type=?1 AND source_id=?2 "
        "UNION ALL "
        "SELECT r.source_type, r.source_id, r.target_type, r.target_id, r.rel_type, p.depth + 1, "
        "p.path || '->' || r.rel_type || '->' || r.target_type || ':' || r.target_id "
        "FROM truth_relationships r "
        "JOIN path_cte p ON r.source_type = p.target_type AND r.source_id = p.target_id "
        "WHERE p.depth < 10"
        ") "
        "SELECT path FROM path_cte WHERE target_type=?3 AND target_id=?4 LIMIT 1");
    
    rc = sqlite3_prepare_v2(self->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, source_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, source_id);
    sqlite3_bind_text(stmt, 3, target_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, target_id);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char * path = (const char *)sqlite3_column_text(stmt, 0);
        len += snprintf(buffer + len, sizeof(buffer) - len, "%s", path ? path : "none");
        r.status = 0;
        r.count = 1;
    } else {
        len += snprintf(buffer + len, sizeof(buffer) - len, "none");
        r.status = 0;
        r.count = 0;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    sqlite3_finalize(stmt);
    r.value = strdup(buffer);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthSchema|cleanup|lifecycle|destructor]*/
/*{[Input:none][Output:status][Side_effects:cleansup_state][Pattern:param_validate_execute]}*/
int TruthSchema_cleanup(TruthSchema_State * self) {
    if (!self) return 0;
    self->is_booted = 0;
    return 1;
}
