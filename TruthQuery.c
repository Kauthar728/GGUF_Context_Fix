/*
#   Ghost[TruthQuery:TruthQuery Domain:Inquiry Purpose:Unified_Truth_Surface
#   Owns:TruthLessons|TruthFailures|AuthorityGraph|CrossDomainQuery|PatternMatching
#   Accepts:query|context|component Returns:truth|lessons|failures|dependencies|traces
#   Requires:MemUnit|SQLite3 Exposes:where_is|what_failed|show_dependencies|trace_execution|find_lessons|authority_graph
#   State:memdb|lessons|failures|graph|patterns Health:query_accuracy Tags:truth,query,unified,cross_domain,authority,lessons,failures ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#define TRUTHQUERY_VERSION "1.0.0"
#define TRUTHQUERY_MAX_SQL 8192
#define TRUTHQUERY_MAX_KEY 256
#define TRUTHQUERY_MAX_VALUE 8192
#define TRUTHQUERY_MAX_PACKET 65536
#define TRUTHQUERY_MAX_MESSAGE 1024

/*##section##[MemDBRef|DatabaseReference]*/
typedef struct TruthQuery_MemDBRef {
    sqlite3 * db;
    int is_external;
    char * last_error;
} TruthQuery_MemDBRef;

/*##section##[TruthLesson|LessonStorage]*/
typedef struct TruthQuery_Lesson {
    int lesson_id;
    char topic[TRUTHQUERY_MAX_KEY];
    char lesson_text[TRUTHQUERY_MAX_VALUE];
    char context[TRUTHQUERY_MAX_VALUE];
    int applied;
    time_t timestamp;
} TruthQuery_Lesson;

/*##section##[TruthFailure|FailureTracking]*/
typedef struct TruthQuery_Failure {
    int failure_id;
    char context[TRUTHQUERY_MAX_KEY];
    char failure_desc[TRUTHQUERY_MAX_VALUE];
    int fix_lesson_id;
    time_t timestamp;
} TruthQuery_Failure;

/*##section##[AuthorityNode|GraphStructure]*/
typedef struct TruthQuery_AuthorityNode {
    char node_id[TRUTHQUERY_MAX_KEY];
    char node_type[TRUTHQUERY_MAX_KEY];
    char authority[TRUTHQUERY_MAX_KEY];
    char parent_id[TRUTHQUERY_MAX_KEY];
} TruthQuery_AuthorityNode;

/*##section##[ResultT|3TupleContract]*/
typedef struct TruthQuery_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthQuery_Result;

/*##section##[State|WorldContainer]*/
typedef struct TruthQuery_State {
    TruthQuery_MemDBRef memdb;
    int is_booted;
    int health_code;
    char last_message[TRUTHQUERY_MAX_MESSAGE];
    char last_result[TRUTHQUERY_MAX_PACKET];
    int query_count;
    time_t boot_time;
    
    sqlite3_stmt * lesson_insert;
    sqlite3_stmt * lesson_search;
    sqlite3_stmt * failure_insert;
    sqlite3_stmt * failure_search;
    sqlite3_stmt * authority_insert;
    sqlite3_stmt * authority_traverse;
    sqlite3_stmt * cross_domain_query;
} TruthQuery_State;

int TruthQuery_lesson_put(TruthQuery_State * self, const char * topic, const char * lesson_text, const char * context);
int TruthQuery_failure_record(TruthQuery_State * self, const char * context, const char * failure_desc, int fix_lesson_id);
int TruthQuery_authority_add(TruthQuery_State * self, const char * node_id, const char * node_type, const char * authority, const char * parent_id);

/*##method##[TruthQuery|create|allocation|constructor]*/
/*{[Input:none][Output:TruthQuery_State_ptr][Side_effects:allocates_state][Pattern:param_validate_execute]}*/
TruthQuery_State * TruthQuery_create(void) {
    TruthQuery_State * self = (TruthQuery_State *)calloc(1, sizeof(TruthQuery_State));
    return self;
}

/*##method##[TruthQuery|boot|lifecycle|initialization]*/
/*{[Input:memdb_ptr][Output:status][Side_effects:creates_truth_tables_statements][Pattern:param_validate_execute]}*/
int TruthQuery_boot(TruthQuery_State * self, sqlite3 * external_memdb) {
    char sql[TRUTHQUERY_MAX_SQL];
    char * err = NULL;
    int rc;
    
    if (!self) return -1;
    memset(self, 0, sizeof(*self));
    
    if (external_memdb) {
        self->memdb.db = external_memdb;
        self->memdb.is_external = 1;
    } else {
        rc = sqlite3_open(":memory:", &self->memdb.db);
        if (rc != SQLITE_OK) return -1;
        self->memdb.is_external = 0;
    }
    
    snprintf(sql, sizeof(sql),
        "CREATE TABLE IF NOT EXISTS truth_lessons("
        "lesson_id INTEGER PRIMARY KEY,"
        "topic TEXT NOT NULL,"
        "lesson_text TEXT NOT NULL,"
        "context TEXT,"
        "applied INTEGER DEFAULT 0,"
        "timestamp INTEGER DEFAULT(unixepoch()));"
        
        "CREATE TABLE IF NOT EXISTS truth_failures("
        "failure_id INTEGER PRIMARY KEY,"
        "context TEXT NOT NULL,"
        "failure_desc TEXT NOT NULL,"
        "fix_lesson_id INTEGER,"
        "timestamp INTEGER DEFAULT(unixepoch()),"
        "FOREIGN KEY(fix_lesson_id) REFERENCES truth_lessons(lesson_id));"
        
        "CREATE TABLE IF NOT EXISTS truth_authority_graph("
        "node_id TEXT PRIMARY KEY,"
        "node_type TEXT NOT NULL,"
        "authority TEXT NOT NULL,"
        "parent_id TEXT,"
        "timestamp INTEGER DEFAULT(unixepoch()));"
        
        "CREATE INDEX IF NOT EXISTS idx_lessons_topic ON truth_lessons(topic);"
        "CREATE INDEX IF NOT EXISTS idx_failures_context ON truth_failures(context);"
        "CREATE INDEX IF NOT EXISTS idx_authority_parent ON truth_authority_graph(parent_id);");
    
    rc = sqlite3_exec(self->memdb.db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return -2;
    }
    
    sqlite3_prepare_v2(self->memdb.db, "INSERT INTO truth_lessons(topic,lesson_text,context,timestamp) VALUES(?1,?2,?3,unixepoch())", -1, &self->lesson_insert, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT lesson_id,topic,lesson_text,context,applied,timestamp FROM truth_lessons WHERE topic LIKE ?1 ORDER BY timestamp DESC LIMIT 20", -1, &self->lesson_search, NULL);
    sqlite3_prepare_v2(self->memdb.db, "INSERT INTO truth_failures(context,failure_desc,fix_lesson_id,timestamp) VALUES(?1,?2,?3,unixepoch())", -1, &self->failure_insert, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT failure_id,context,failure_desc,fix_lesson_id,timestamp FROM truth_failures WHERE context LIKE ?1 ORDER BY timestamp DESC LIMIT 20", -1, &self->failure_search, NULL);
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO truth_authority_graph(node_id,node_type,authority,parent_id,timestamp) VALUES(?1,?2,?3,?4,unixepoch())", -1, &self->authority_insert, NULL);
    sqlite3_prepare_v2(self->memdb.db, "WITH RECURSIVE auth_tree AS (SELECT node_id,node_type,authority,parent_id FROM truth_authority_graph WHERE node_id=?1 UNION ALL SELECT g.node_id,g.node_type,g.authority,g.parent_id FROM truth_authority_graph g JOIN auth_tree a ON g.parent_id=a.node_id) SELECT * FROM auth_tree", -1, &self->authority_traverse, NULL);
    
    self->is_booted = 1;
    self->health_code = 100;
    self->boot_time = time(NULL);
    snprintf(self->last_message, sizeof(self->last_message), "TruthQuery v%s booted", TRUTHQUERY_VERSION);
    
    return 0;
}

/*##method##[TruthQuery|where_is|inquiry|authority_lookup]*/
/*{[Input:action][Output:ResultT][Side_effects:queries_verb_registry_and_authority_graph][Pattern:param_validate_execute]}*/
TruthQuery_Result TruthQuery_where_is(TruthQuery_State * self, const char * action) {
    TruthQuery_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHQUERY_MAX_PACKET];
    int len = 0;
    sqlite3_stmt * stmt = NULL;
    
    if (!self || !self->is_booted || !action) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthQuery:where_is][Action:%s][Results:", action);
    
    sqlite3_prepare_v2(self->memdb.db, "SELECT verb,authority,handler FROM verb_registry WHERE verb LIKE ?1 OR handler LIKE ?1 LIMIT 10", -1, &stmt, NULL);
    char pattern[TRUTHQUERY_MAX_KEY];
    snprintf(pattern, sizeof(pattern), "%%%s%%", action);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char * verb = (const char *)sqlite3_column_text(stmt, 0);
        const char * authority = (const char *)sqlite3_column_text(stmt, 1);
        const char * handler = (const char *)sqlite3_column_text(stmt, 2);
        len += snprintf(buffer + len, sizeof(buffer) - len, "[Verb:%s|Authority:%s|Handler:%s]", 
                       verb ? verb : "", authority ? authority : "", handler ? handler : "");
        r.count++;
    }
    sqlite3_finalize(stmt);
    
    sqlite3_prepare_v2(self->memdb.db, "SELECT node_id,node_type,authority FROM truth_authority_graph WHERE node_id LIKE ?1 OR authority LIKE ?1 LIMIT 10", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char * node_id = (const char *)sqlite3_column_text(stmt, 0);
        const char * node_type = (const char *)sqlite3_column_text(stmt, 1);
        const char * authority = (const char *)sqlite3_column_text(stmt, 2);
        len += snprintf(buffer + len, sizeof(buffer) - len, "[Node:%s|Type:%s|Authority:%s]", 
                       node_id ? node_id : "", node_type ? node_type : "", authority ? authority : "");
        r.count++;
    }
    sqlite3_finalize(stmt);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "where_is:%s found:%d", action, r.count);
    
    return r;
}

/*##method##[TruthQuery|what_failed|inquiry|failure_lookup]*/
/*{[Input:context][Output:ResultT][Side_effects:queries_failures_and_linked_lessons][Pattern:param_validate_execute]}*/
TruthQuery_Result TruthQuery_what_failed(TruthQuery_State * self, const char * context) {
    TruthQuery_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHQUERY_MAX_PACKET];
    int len = 0;
    
    if (!self || !self->is_booted || !context) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthQuery:what_failed][Context:%s][Results:", context);
    
    char pattern[TRUTHQUERY_MAX_KEY];
    snprintf(pattern, sizeof(pattern), "%%%s%%", context);
    sqlite3_bind_text(self->failure_search, 1, pattern, -1, SQLITE_STATIC);
    
    while (sqlite3_step(self->failure_search) == SQLITE_ROW) {
        int failure_id = sqlite3_column_int(self->failure_search, 0);
        const char * ctx = (const char *)sqlite3_column_text(self->failure_search, 1);
        const char * desc = (const char *)sqlite3_column_text(self->failure_search, 2);
        int fix_lesson_id = sqlite3_column_int(self->failure_search, 3);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[FailureID:%d|Context:%s|Desc:%s|FixLesson:%d]", 
                       failure_id, ctx ? ctx : "", desc ? desc : "", fix_lesson_id);
        r.count++;
        
        if (fix_lesson_id > 0) {
            sqlite3_stmt * lesson_stmt = NULL;
            sqlite3_prepare_v2(self->memdb.db, "SELECT topic,lesson_text FROM truth_lessons WHERE lesson_id=?1", -1, &lesson_stmt, NULL);
            sqlite3_bind_int(lesson_stmt, 1, fix_lesson_id);
            if (sqlite3_step(lesson_stmt) == SQLITE_ROW) {
                const char * topic = (const char *)sqlite3_column_text(lesson_stmt, 0);
                const char * lesson = (const char *)sqlite3_column_text(lesson_stmt, 1);
                len += snprintf(buffer + len, sizeof(buffer) - len, "[Lesson:%s|%s]", topic ? topic : "", lesson ? lesson : "");
            }
            sqlite3_finalize(lesson_stmt);
        }
    }
    sqlite3_reset(self->failure_search);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "what_failed:%s found:%d", context, r.count);
    
    return r;
}

/*##method##[TruthQuery|show_dependencies|inquiry|graph_traversal]*/
/*{[Input:component][Output:ResultT][Side_effects:traverses_authority_graph][Pattern:param_validate_execute]}*/
TruthQuery_Result TruthQuery_show_dependencies(TruthQuery_State * self, const char * component) {
    TruthQuery_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHQUERY_MAX_PACKET];
    int len = 0;
    
    if (!self || !self->is_booted || !component) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthQuery:show_dependencies][Component:%s][Results:", component);
    
    sqlite3_bind_text(self->authority_traverse, 1, component, -1, SQLITE_STATIC);
    
    while (sqlite3_step(self->authority_traverse) == SQLITE_ROW) {
        const char * node_id = (const char *)sqlite3_column_text(self->authority_traverse, 0);
        const char * node_type = (const char *)sqlite3_column_text(self->authority_traverse, 1);
        const char * authority = (const char *)sqlite3_column_text(self->authority_traverse, 2);
        const char * parent_id = (const char *)sqlite3_column_text(self->authority_traverse, 3);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[Node:%s|Type:%s|Authority:%s|Parent:%s]", 
                       node_id ? node_id : "", node_type ? node_type : "", authority ? authority : "", parent_id ? parent_id : "");
        r.count++;
    }
    sqlite3_reset(self->authority_traverse);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "show_dependencies:%s found:%d", component, r.count);
    
    return r;
}

/*##method##[TruthQuery|trace_execution|inquiry|history_lookup]*/
/*{[Input:world|action][Output:ResultT][Side_effects:queries_execution_route][Pattern:param_validate_execute]}*/
TruthQuery_Result TruthQuery_trace_execution(TruthQuery_State * self, const char * world, const char * action) {
    TruthQuery_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHQUERY_MAX_PACKET];
    int len = 0;
    sqlite3_stmt * stmt = NULL;
    
    if (!self || !self->is_booted || !world) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthQuery:trace_execution][World:%s][Action:%s][Results:", world, action ? action : "all");
    
    char sql[TRUTHQUERY_MAX_SQL];
    if (action) {
        snprintf(sql, sizeof(sql), "SELECT world,action,detail,status_code,timestamp FROM execution_route WHERE world=?1 AND action=?2 ORDER BY timestamp DESC LIMIT 50");
        sqlite3_prepare_v2(self->memdb.db, sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, world, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, action, -1, SQLITE_STATIC);
    } else {
        snprintf(sql, sizeof(sql), "SELECT world,action,detail,status_code,timestamp FROM execution_route WHERE world=?1 ORDER BY timestamp DESC LIMIT 50");
        sqlite3_prepare_v2(self->memdb.db, sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, world, -1, SQLITE_STATIC);
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char * w = (const char *)sqlite3_column_text(stmt, 0);
        const char * a = (const char *)sqlite3_column_text(stmt, 1);
        const char * d = (const char *)sqlite3_column_text(stmt, 2);
        int status = sqlite3_column_int(stmt, 3);
        time_t ts = sqlite3_column_int64(stmt, 4);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[World:%s|Action:%s|Detail:%s|Status:%d|Time:%ld]", 
                       w ? w : "", a ? a : "", d ? d : "", status, (long)ts);
        r.count++;
    }
    sqlite3_finalize(stmt);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "trace_execution:%s:%s found:%d", world, action ? action : "all", r.count);
    
    return r;
}

/*##method##[TruthQuery|find_lessons|inquiry|lesson_lookup]*/
/*{[Input:topic][Output:ResultT][Side_effects:queries_lessons_by_topic][Pattern:param_validate_execute]}*/
TruthQuery_Result TruthQuery_find_lessons(TruthQuery_State * self, const char * topic) {
    TruthQuery_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHQUERY_MAX_PACKET];
    int len = 0;
    
    if (!self || !self->is_booted || !topic) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthQuery:find_lessons][Topic:%s][Results:", topic);
    
    char pattern[TRUTHQUERY_MAX_KEY];
    snprintf(pattern, sizeof(pattern), "%%%s%%", topic);
    sqlite3_bind_text(self->lesson_search, 1, pattern, -1, SQLITE_STATIC);
    
    while (sqlite3_step(self->lesson_search) == SQLITE_ROW) {
        int lesson_id = sqlite3_column_int(self->lesson_search, 0);
        const char * t = (const char *)sqlite3_column_text(self->lesson_search, 1);
        const char * lesson = (const char *)sqlite3_column_text(self->lesson_search, 2);
        const char * ctx = (const char *)sqlite3_column_text(self->lesson_search, 3);
        int applied = sqlite3_column_int(self->lesson_search, 4);
        time_t ts = sqlite3_column_int64(self->lesson_search, 5);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[LessonID:%d|Topic:%s|Lesson:%s|Context:%s|Applied:%d|Time:%ld]", 
                       lesson_id, t ? t : "", lesson ? lesson : "", ctx ? ctx : "", applied, (long)ts);
        r.count++;
    }
    sqlite3_reset(self->lesson_search);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "find_lessons:%s found:%d", topic, r.count);
    
    return r;
}

/*##method##[TruthQuery|authority_graph|inquiry|full_graph]*/
/*{[Input:start_node][Output:ResultT][Side_effects:returns_full_authority_graph][Pattern:param_validate_execute]}*/
TruthQuery_Result TruthQuery_authority_graph(TruthQuery_State * self, const char * start_node) {
    TruthQuery_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHQUERY_MAX_PACKET];
    int len = 0;
    sqlite3_stmt * stmt = NULL;
    
    if (!self || !self->is_booted) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthQuery:authority_graph][Start:%s][Results:", start_node ? start_node : "all");
    
    char sql[TRUTHQUERY_MAX_SQL];
    if (start_node) {
        snprintf(sql, sizeof(sql), "SELECT node_id,node_type,authority,parent_id FROM truth_authority_graph WHERE node_id=?1 OR parent_id=?1 ORDER BY node_id");
        sqlite3_prepare_v2(self->memdb.db, sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, start_node, -1, SQLITE_STATIC);
    } else {
        snprintf(sql, sizeof(sql), "SELECT node_id,node_type,authority,parent_id FROM truth_authority_graph ORDER BY node_id LIMIT 100");
        sqlite3_prepare_v2(self->memdb.db, sql, -1, &stmt, NULL);
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char * node_id = (const char *)sqlite3_column_text(stmt, 0);
        const char * node_type = (const char *)sqlite3_column_text(stmt, 1);
        const char * authority = (const char *)sqlite3_column_text(stmt, 2);
        const char * parent_id = (const char *)sqlite3_column_text(stmt, 3);
        
        len += snprintf(buffer + len, sizeof(buffer) - len, "[Node:%s|Type:%s|Authority:%s|Parent:%s]", 
                       node_id ? node_id : "", node_type ? node_type : "", authority ? authority : "", parent_id ? parent_id : "");
        r.count++;
    }
    sqlite3_finalize(stmt);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = 0;
    r.value = strdup(buffer);
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "authority_graph:%s found:%d", start_node ? start_node : "all", r.count);
    
    return r;
}

/*##method##[TruthQuery|lesson_put|storage|lesson_insertion]*/
/*{[Input:topic|lesson_text|context][Output:status][Side_effects:inserts_lesson][Pattern:param_validate_execute]}*/
int TruthQuery_lesson_put(TruthQuery_State * self, const char * topic, const char * lesson_text, const char * context) {
    int rc;
    
    if (!self || !self->is_booted || !topic || !lesson_text) return 0;
    
    sqlite3_bind_text(self->lesson_insert, 1, topic, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->lesson_insert, 2, lesson_text, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->lesson_insert, 3, context ? context : "", -1, SQLITE_STATIC);
    rc = sqlite3_step(self->lesson_insert);
    sqlite3_reset(self->lesson_insert);
    
    if (rc == SQLITE_DONE) {
        snprintf(self->last_message, sizeof(self->last_message), "lesson_put:%s", topic);
        return 1;
    }
    return 0;
}

/*##method##[TruthQuery|failure_record|storage|failure_insertion]*/
/*{[Input:context|failure_desc|fix_lesson_id][Output:status][Side_effects:inserts_failure][Pattern:param_validate_execute]}*/
int TruthQuery_failure_record(TruthQuery_State * self, const char * context, const char * failure_desc, int fix_lesson_id) {
    int rc;
    
    if (!self || !self->is_booted || !context || !failure_desc) return 0;
    
    sqlite3_bind_text(self->failure_insert, 1, context, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->failure_insert, 2, failure_desc, -1, SQLITE_STATIC);
    sqlite3_bind_int(self->failure_insert, 3, fix_lesson_id);
    rc = sqlite3_step(self->failure_insert);
    sqlite3_reset(self->failure_insert);
    
    if (rc == SQLITE_DONE) {
        snprintf(self->last_message, sizeof(self->last_message), "failure_record:%s", context);
        return 1;
    }
    return 0;
}

/*##method##[TruthQuery|authority_add|storage|graph_insertion]*/
/*{[Input:node_id|node_type|authority|parent_id][Output:status][Side_effects:inserts_authority_node][Pattern:param_validate_execute]}*/
int TruthQuery_authority_add(TruthQuery_State * self, const char * node_id, const char * node_type, const char * authority, const char * parent_id) {
    int rc;
    
    if (!self || !self->is_booted || !node_id || !node_type || !authority) return 0;
    
    sqlite3_bind_text(self->authority_insert, 1, node_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->authority_insert, 2, node_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->authority_insert, 3, authority, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->authority_insert, 4, parent_id ? parent_id : "", -1, SQLITE_STATIC);
    rc = sqlite3_step(self->authority_insert);
    sqlite3_reset(self->authority_insert);
    
    if (rc == SQLITE_DONE) {
        snprintf(self->last_message, sizeof(self->last_message), "authority_add:%s", node_id);
        return 1;
    }
    return 0;
}

/*##method##[TruthQuery|cleanup|lifecycle|destructor]*/
/*{[Input:none][Output:status][Side_effects:frees_statements_closes_db_if_internal][Pattern:param_validate_execute]}*/
int TruthQuery_cleanup(TruthQuery_State * self) {
    if (!self) return 0;
    
    if (self->lesson_insert) sqlite3_finalize(self->lesson_insert);
    if (self->lesson_search) sqlite3_finalize(self->lesson_search);
    if (self->failure_insert) sqlite3_finalize(self->failure_insert);
    if (self->failure_search) sqlite3_finalize(self->failure_search);
    if (self->authority_insert) sqlite3_finalize(self->authority_insert);
    if (self->authority_traverse) sqlite3_finalize(self->authority_traverse);
    
    if (!self->memdb.is_external && self->memdb.db) {
        sqlite3_close(self->memdb.db);
    }
    
    self->is_booted = 0;
    return 1;
}
