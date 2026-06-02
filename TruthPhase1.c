/*
#   Ghost[TruthPhase1:TruthPhase1 Domain:Storage Purpose:Phase1_Retrieval_Foundation
#   Owns:Methods|Chats|Documents|Descriptions|SourceFiles
#   Accepts:method_data|chat_data|document_data Returns:retrieval_queries|distilled_truth
#   Requires:SQLite3 Exposes:insert_method|insert_chat|insert_document|search_methods|search_chats|search_documents
#   State:methods|chats|documents|descriptions Health:retrieval_readiness Tags:phase1,retrieval,methods,chats,documents,descriptions ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#define TRUTHPHASE1_VERSION "1.0.0"
#define TRUTHPHASE1_MAX_SQL 8192
#define TRUTHPHASE1_MAX_KEY 256
#define TRUTHPHASE1_MAX_VALUE 8192
#define TRUTHPHASE1_MAX_PACKET 65536

/*##section##[ResultT|3TupleContract]*/
typedef struct TruthPhase1_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthPhase1_Result;

/*##section##[State|WorldContainer]*/
typedef struct TruthPhase1_State {
    sqlite3 * db;
    int is_booted;
    char last_message[TRUTHPHASE1_MAX_KEY];
    int operation_count;
    time_t boot_time;
} TruthPhase1_State;

/*##method##[TruthPhase1|create|allocation|constructor]*/
/*{[Input:none][Output:TruthPhase1_State_ptr][Side_effects:allocates_state][Pattern:param_validate_execute]}*/
TruthPhase1_State * TruthPhase1_create(void) {
    TruthPhase1_State * self = (TruthPhase1_State *)calloc(1, sizeof(TruthPhase1_State));
    return self;
}

/*##method##[TruthPhase1|boot|lifecycle|initialization]*/
/*{[Input:db_ptr][Output:status][Side_effects:creates_phase1_tables][Pattern:param_validate_execute]}*/
int TruthPhase1_boot(TruthPhase1_State * self, sqlite3 * db) {
    char sql[TRUTHPHASE1_MAX_SQL];
    char * err = NULL;
    int rc;
    
    if (!self || !db) return -1;
    memset(self, 0, sizeof(*self));
    self->db = db;
    
    snprintf(sql, sizeof(sql),
        /* Phase 1: Methods - the unit of work */
        "CREATE TABLE IF NOT EXISTS python_methods("
        "method_id INTEGER PRIMARY KEY,"
        "method_name TEXT NOT NULL,"
        "description TEXT,"
        "code TEXT,"
        "source_file TEXT NOT NULL,"
        "line_start INTEGER,"
        "line_end INTEGER,"
        "created_at INTEGER DEFAULT(unixepoch()));"
        
        "CREATE TABLE IF NOT EXISTS swift_methods("
        "method_id INTEGER PRIMARY KEY,"
        "method_name TEXT NOT NULL,"
        "description TEXT,"
        "code TEXT,"
        "source_file TEXT NOT NULL,"
        "line_start INTEGER,"
        "line_end INTEGER,"
        "created_at INTEGER DEFAULT(unixepoch()));"
        
        "CREATE TABLE IF NOT EXISTS c_methods("
        "method_id INTEGER PRIMARY KEY,"
        "method_name TEXT NOT NULL,"
        "description TEXT,"
        "code TEXT,"
        "source_file TEXT NOT NULL,"
        "line_start INTEGER,"
        "line_end INTEGER,"
        "created_at INTEGER DEFAULT(unixepoch()));"
        
        /* Phase 1: Chats */
        "CREATE TABLE IF NOT EXISTS chats("
        "chat_id INTEGER PRIMARY KEY,"
        "chat_title TEXT,"
        "chat_content TEXT,"
        "source_file TEXT NOT NULL,"
        "created_at INTEGER DEFAULT(unixepoch()));"
        
        /* Phase 1: Documents */
        "CREATE TABLE IF NOT EXISTS documents("
        "doc_id INTEGER PRIMARY KEY,"
        "doc_title TEXT,"
        "doc_content TEXT,"
        "source_file TEXT NOT NULL,"
        "created_at INTEGER DEFAULT(unixepoch()));"
        
        /* Indexes for retrieval */
        "CREATE INDEX IF NOT EXISTS idx_python_methods_name ON python_methods(method_name);"
        "CREATE INDEX IF NOT EXISTS idx_python_methods_desc ON python_methods(description);"
        "CREATE INDEX IF NOT EXISTS idx_swift_methods_name ON swift_methods(method_name);"
        "CREATE INDEX IF NOT EXISTS idx_swift_methods_desc ON swift_methods(description);"
        "CREATE INDEX IF NOT EXISTS idx_c_methods_name ON c_methods(method_name);"
        "CREATE INDEX IF NOT EXISTS idx_c_methods_desc ON c_methods(description);"
        "CREATE INDEX IF NOT EXISTS idx_chats_content ON chats(chat_content);"
        "CREATE INDEX IF NOT EXISTS idx_docs_content ON documents(doc_content);");
    
    rc = sqlite3_exec(self->db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return -2;
    }
    
    self->is_booted = 1;
    self->boot_time = time(NULL);
    snprintf(self->last_message, sizeof(self->last_message), "TruthPhase1 v%s booted", TRUTHPHASE1_VERSION);
    
    return 0;
}

/*##method##[TruthPhase1|insert_method|storage|method_insertion]*/
/*{[Input:language|method_name|description|code|source_file|line_start|line_end][Output:ResultT][Side_effects:inserts_method_into_language_table][Pattern:param_validate_execute]}*/
TruthPhase1_Result TruthPhase1_insert_method(TruthPhase1_State * self, const char * language, const char * method_name, const char * description, const char * code, const char * source_file, int line_start, int line_end) {
    TruthPhase1_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    char sql[TRUTHPHASE1_MAX_SQL];
    int rc;
    
    if (!self || !self->is_booted || !language || !method_name || !source_file) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    snprintf(sql, sizeof(sql), "INSERT INTO %s_methods(method_name,description,code,source_file,line_start,line_end) VALUES(?1,?2,?3,?4,?5,?6) RETURNING method_id", language);
    
    rc = sqlite3_prepare_v2(self->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, method_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, description ? description : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, code ? code : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, source_file, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, line_start);
    sqlite3_bind_int(stmt, 6, line_end);
    
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

/*##method##[TruthPhase1|insert_chat|storage|chat_insertion]*/
/*{[Input:chat_title|chat_content|source_file][Output:ResultT][Side_effects:inserts_chat_record][Pattern:param_validate_execute]}*/
TruthPhase1_Result TruthPhase1_insert_chat(TruthPhase1_State * self, const char * chat_title, const char * chat_content, const char * source_file) {
    TruthPhase1_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !chat_content || !source_file) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT INTO chats(chat_title,chat_content,source_file) VALUES(?1,?2,?3) RETURNING chat_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, chat_title ? chat_title : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, chat_content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, source_file, -1, SQLITE_STATIC);
    
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

/*##method##[TruthPhase1|insert_document|storage|document_insertion]*/
/*{[Input:doc_title|doc_content|source_file][Output:ResultT][Side_effects:inserts_document_record][Pattern:param_validate_execute]}*/
TruthPhase1_Result TruthPhase1_insert_document(TruthPhase1_State * self, const char * doc_title, const char * doc_content, const char * source_file) {
    TruthPhase1_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !doc_content || !source_file) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->db, "INSERT INTO documents(doc_title,doc_content,source_file) VALUES(?1,?2,?3) RETURNING doc_id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, doc_title ? doc_title : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, doc_content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, source_file, -1, SQLITE_STATIC);
    
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

/*##method##[TruthPhase1|search_methods|inquiry|method_search]*/
/*{[Input:language|query][Output:ResultT][Side_effects:searches_methods_by_name_or_description][Pattern:param_validate_execute]}*/
TruthPhase1_Result TruthPhase1_search_methods(TruthPhase1_State * self, const char * language, const char * query) {
    TruthPhase1_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHPHASE1_MAX_PACKET];
    char sql[TRUTHPHASE1_MAX_SQL];
    int len = 0;
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !language || !query) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthPhase1:search_methods][Language:%s][Query:%s][Results:", language, query);
    
    snprintf(sql, sizeof(sql), "SELECT method_id,method_name,description,source_file,line_start,line_end FROM %s_methods WHERE method_name LIKE ?1 OR description LIKE ?2 LIMIT 10", language);
    
    char pattern[TRUTHPHASE1_MAX_KEY];
    snprintf(pattern, sizeof(pattern), "%%%s%%", query);
    
    rc = sqlite3_prepare_v2(self->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int method_id = sqlite3_column_int(stmt, 0);
        const char * method_name = (const char *)sqlite3_column_text(stmt, 1);
        const char * description = (const char *)sqlite3_column_text(stmt, 2);
        const char * source_file = (const char *)sqlite3_column_text(stmt, 3);
        int line_start = sqlite3_column_int(stmt, 4);
        int line_end = sqlite3_column_int(stmt, 5);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[MethodID:%d|Name:%s|Desc:%s|File:%s|Lines:%d-%d]", 
                       method_id, method_name ? method_name : "", description ? description : "", source_file ? source_file : "", line_start, line_end);
        r.count++;
    }
    
    sqlite3_finalize(stmt);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthPhase1|search_chats|inquiry|chat_search]*/
/*{[Input:query][Output:ResultT][Side_effects:searches_chats_by_content][Pattern:param_validate_execute]}*/
TruthPhase1_Result TruthPhase1_search_chats(TruthPhase1_State * self, const char * query) {
    TruthPhase1_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHPHASE1_MAX_PACKET];
    int len = 0;
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !query) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthPhase1:search_chats][Query:%s][Results:", query);
    
    char pattern[TRUTHPHASE1_MAX_KEY];
    snprintf(pattern, sizeof(pattern), "%%%s%%", query);
    
    rc = sqlite3_prepare_v2(self->db, "SELECT chat_id,chat_title,source_file FROM chats WHERE chat_content LIKE ?1 LIMIT 10", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int chat_id = sqlite3_column_int(stmt, 0);
        const char * chat_title = (const char *)sqlite3_column_text(stmt, 1);
        const char * source_file = (const char *)sqlite3_column_text(stmt, 2);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[ChatID:%d|Title:%s|File:%s]", 
                       chat_id, chat_title ? chat_title : "", source_file ? source_file : "");
        r.count++;
    }
    
    sqlite3_finalize(stmt);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthPhase1|search_documents|inquiry|document_search]*/
/*{[Input:query][Output:ResultT][Side_effects:searches_documents_by_content][Pattern:param_validate_execute]}*/
TruthPhase1_Result TruthPhase1_search_documents(TruthPhase1_State * self, const char * query) {
    TruthPhase1_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHPHASE1_MAX_PACKET];
    int len = 0;
    sqlite3_stmt * stmt = NULL;
    int rc;
    
    if (!self || !self->is_booted || !query) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthPhase1:search_documents][Query:%s][Results:", query);
    
    char pattern[TRUTHPHASE1_MAX_KEY];
    snprintf(pattern, sizeof(pattern), "%%%s%%", query);
    
    rc = sqlite3_prepare_v2(self->db, "SELECT doc_id,doc_title,source_file FROM documents WHERE doc_content LIKE ?1 LIMIT 10", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int doc_id = sqlite3_column_int(stmt, 0);
        const char * doc_title = (const char *)sqlite3_column_text(stmt, 1);
        const char * source_file = (const char *)sqlite3_column_text(stmt, 2);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[DocID:%d|Title:%s|File:%s]", 
                       doc_id, doc_title ? doc_title : "", source_file ? source_file : "");
        r.count++;
    }
    
    sqlite3_finalize(stmt);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthPhase1|distill_truth|inquiry|distilled_assembly]*/
/*{[Input:query][Output:ResultT][Side_effects:assembles_distilled_truth_20_100KB][Pattern:param_validate_execute]}*/
TruthPhase1_Result TruthPhase1_distill_truth(TruthPhase1_State * self, const char * query) {
    TruthPhase1_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHPHASE1_MAX_PACKET];
    int len = 0;
    TruthPhase1_Result temp;
    
    if (!self || !self->is_booted || !query) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthPhase1:distill_truth][Query:%s][Distilled:", query);
    
    temp = TruthPhase1_search_methods(self, "python", query);
    if (temp.value) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "[PythonMethods:%s]", temp.value);
        free(temp.value);
    }
    if (temp.error) free(temp.error);
    
    temp = TruthPhase1_search_chats(self, query);
    if (temp.value) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "[Chats:%s]", temp.value);
        free(temp.value);
    }
    if (temp.error) free(temp.error);
    
    temp = TruthPhase1_search_documents(self, query);
    if (temp.value) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "[Documents:%s]", temp.value);
        free(temp.value);
    }
    if (temp.error) free(temp.error);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->operation_count++;
    
    return r;
}

/*##method##[TruthPhase1|cleanup|lifecycle|destructor]*/
/*{[Input:none][Output:status][Side_effects:cleansup_state][Pattern:param_validate_execute]}*/
int TruthPhase1_cleanup(TruthPhase1_State * self) {
    if (!self) return 0;
    self->is_booted = 0;
    return 1;
}
