/*
#   Ghost[TruthServer:TruthServer Domain:Orchestration Purpose:Unified_Truth_Server
#   Owns:MemUnit|TruthQuery|CrossDomainFederation|AutoSeeding|QueryRouting
#   Accepts:query|config Returns:unified_truth|lessons|failures|dependencies|traces
#   Requires:MemUnit|TruthQuery|SQLite3 Exposes:boot|query|seed|health|cleanup
#   State:memunit|truthquery|is_booted Health:server_status Tags:truth,server,unified,cross_domain,orchestration ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#define TRUTHSERVER_VERSION "1.0.0"
#define TRUTHSERVER_MAX_SQL 8192
#define TRUTHSERVER_MAX_KEY 256
#define TRUTHSERVER_MAX_VALUE 8192
#define TRUTHSERVER_MAX_PACKET 65536
#define TRUTHSERVER_MAX_MESSAGE 1024

/* Forward declarations */
typedef struct MemUnit_State MemUnit_State;
typedef struct TruthQuery_State TruthQuery_State;
typedef struct TruthSchema_State TruthSchema_State;

/*##section##[TruthQueryResultT|3TupleContract]*/
typedef struct TruthQuery_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthQuery_Result;

/*##section##[TruthSchemaResultT|3TupleContract]*/
typedef struct TruthSchema_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthSchema_Result;

/*##section##[ResultT|3TupleContract]*/
typedef struct TruthServer_Result {
    char * value;
    char * error;
    int status;
    int count;
} TruthServer_Result;

/*##section##[State|WorldContainer]*/
typedef struct TruthServer_State {
    MemUnit_State * memunit;
    TruthQuery_State * truthquery;
    TruthSchema_State * truthschema;
    int is_booted;
    int health_code;
    char last_message[TRUTHSERVER_MAX_MESSAGE];
    char last_result[TRUTHSERVER_MAX_PACKET];
    int query_count;
    time_t boot_time;
    char db_path[TRUTHSERVER_MAX_KEY];
} TruthServer_State;

/* External function declarations */
MemUnit_State * MemUnit_create(void);
int MemUnit_boot(MemUnit_State * self, const char * config);
sqlite3 * MemUnit_get_db(MemUnit_State * self);
int MemUnit_seed_defaults(MemUnit_State * self);
int MemUnit_cleanup(MemUnit_State * self);

TruthQuery_State * TruthQuery_create(void);
int TruthQuery_boot(TruthQuery_State * self, sqlite3 * external_memdb);
TruthQuery_Result TruthQuery_where_is(TruthQuery_State * self, const char * action);
TruthQuery_Result TruthQuery_what_failed(TruthQuery_State * self, const char * context);
TruthQuery_Result TruthQuery_show_dependencies(TruthQuery_State * self, const char * component);
TruthQuery_Result TruthQuery_trace_execution(TruthQuery_State * self, const char * world, const char * action);
TruthQuery_Result TruthQuery_find_lessons(TruthQuery_State * self, const char * topic);
TruthQuery_Result TruthQuery_authority_graph(TruthQuery_State * self, const char * start_node);
int TruthQuery_lesson_put(TruthQuery_State * self, const char * topic, const char * lesson_text, const char * context);
int TruthQuery_failure_record(TruthQuery_State * self, const char * context, const char * failure_desc, int fix_lesson_id);
int TruthQuery_authority_add(TruthQuery_State * self, const char * node_id, const char * node_type, const char * authority, const char * parent_id);
int TruthQuery_cleanup(TruthQuery_State * self);

TruthSchema_State * TruthSchema_create(void);
int TruthSchema_boot(TruthSchema_State * self, sqlite3 * db);
TruthSchema_Result TruthSchema_insert_class(TruthSchema_State * self, const char * class_name, const char * file_path, const char * domain, const char * authority, const char * description);
TruthSchema_Result TruthSchema_insert_method(TruthSchema_State * self, int class_id, const char * method_name, const char * authority, const char * bracket_sig, const char * magnetic_sig);
TruthSchema_Result TruthSchema_insert_file(TruthSchema_State * self, const char * path, const char * hash, const char * language, int line_count);
TruthSchema_Result TruthSchema_insert_domain(TruthSchema_State * self, const char * name, const char * authority, const char * description);
TruthSchema_Result TruthSchema_add_relationship(TruthSchema_State * self, const char * source_type, int source_id, const char * target_type, int target_id, const char * rel_type, float strength);
TruthSchema_Result TruthSchema_record_decision(TruthSchema_State * self, const char * problem, const char * discussion, const char * decision, const char * result, const char * outcome, const char * context);
TruthSchema_Result TruthSchema_find_path(TruthSchema_State * self, const char * source_type, int source_id, const char * target_type, int target_id);
int TruthSchema_cleanup(TruthSchema_State * self);

/*##method##[TruthServer|create|allocation|constructor]*/
/*{[Input:none][Output:TruthServer_State_ptr][Side_effects:allocates_state][Pattern:param_validate_execute]}*/
TruthServer_State * TruthServer_create(void) {
    TruthServer_State * self = (TruthServer_State *)calloc(1, sizeof(TruthServer_State));
    return self;
}

/*##method##[TruthServer|boot|lifecycle|initialization]*/
/*{[Input:config][Output:status][Side_effects:boots_memunit_and_truthquery][Pattern:param_validate_execute]}*/
int TruthServer_boot(TruthServer_State * self, const char * config) {
    int rc;
    const char * db_path = NULL;
    char memunit_config[TRUTHSERVER_MAX_SQL];
    
    if (!self) return -1;
    memset(self, 0, sizeof(*self));
    
    if (config && strstr(config, "persistent=") != NULL) {
        db_path = strstr(config, "persistent=") + 10;
        char * end = strchr(db_path, ',');
        if (end) {
            size_t len = end - db_path;
            if (len < sizeof(self->db_path)) {
                memcpy(self->db_path, db_path, len);
                self->db_path[len] = '\0';
                db_path = self->db_path;
            }
        } else {
            snprintf(self->db_path, sizeof(self->db_path), "%s", db_path);
            db_path = self->db_path;
        }
    }
    
    if (db_path && strlen(db_path) > 0) {
        snprintf(memunit_config, sizeof(memunit_config), "persistent=%s", db_path);
    } else {
        memunit_config[0] = '\0';
    }
    
    self->memunit = MemUnit_create();
    if (!self->memunit) {
        snprintf(self->last_message, sizeof(self->last_message), "MemUnit_create_failed");
        return -2;
    }
    
    rc = MemUnit_boot(self->memunit, memunit_config);
    if (rc != 0) {
        snprintf(self->last_message, sizeof(self->last_message), "MemUnit_boot_failed:%d", rc);
        return -3;
    }
    
    rc = MemUnit_seed_defaults(self->memunit);
    if (rc != 1) {
        snprintf(self->last_message, sizeof(self->last_message), "MemUnit_seed_defaults_failed:%d", rc);
        return -4;
    }
    
    self->truthquery = TruthQuery_create();
    if (!self->truthquery) {
        snprintf(self->last_message, sizeof(self->last_message), "TruthQuery_create_failed");
        return -5;
    }
    
    rc = TruthQuery_boot(self->truthquery, MemUnit_get_db(self->memunit));
    if (rc != 0) {
        snprintf(self->last_message, sizeof(self->last_message), "TruthQuery_boot_failed:%d", rc);
        return -6;
    }
    
    self->truthschema = TruthSchema_create();
    if (!self->truthschema) {
        snprintf(self->last_message, sizeof(self->last_message), "TruthSchema_create_failed");
        return -7;
    }
    
    rc = TruthSchema_boot(self->truthschema, MemUnit_get_db(self->memunit));
    if (rc != 0) {
        snprintf(self->last_message, sizeof(self->last_message), "TruthSchema_boot_failed:%d", rc);
        return -8;
    }
    
    self->is_booted = 1;
    self->health_code = 100;
    self->boot_time = time(NULL);
    snprintf(self->last_message, sizeof(self->last_message), "TruthServer v%s booted with persistent DB: %s", TRUTHSERVER_VERSION, db_path ? db_path : ":memory:");
    
    return 0;
}

/*##method##[TruthServer|query|inquiry|unified_query]*/
/*{[Input:query_type|query_value][Output:ResultT][Side_effects:routes_to_appropriate_truth_query][Pattern:param_validate_execute]}*/
TruthServer_Result TruthServer_query(TruthServer_State * self, const char * query_type, const char * query_value) {
    TruthServer_Result r = {NULL, NULL, 0, 0};
    TruthQuery_Result tq_r;
    
    if (!self || !self->is_booted || !query_type || !query_value) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    if (strcmp(query_type, "where_is") == 0) {
        tq_r = TruthQuery_where_is(self->truthquery, query_value);
    } else if (strcmp(query_type, "what_failed") == 0) {
        tq_r = TruthQuery_what_failed(self->truthquery, query_value);
    } else if (strcmp(query_type, "show_dependencies") == 0) {
        tq_r = TruthQuery_show_dependencies(self->truthquery, query_value);
    } else if (strcmp(query_type, "find_lessons") == 0) {
        tq_r = TruthQuery_find_lessons(self->truthquery, query_value);
    } else if (strcmp(query_type, "authority_graph") == 0) {
        tq_r = TruthQuery_authority_graph(self->truthquery, query_value);
    } else if (strcmp(query_type, "trace_execution") == 0) {
        tq_r = TruthQuery_trace_execution(self->truthquery, query_value, NULL);
    } else {
        r.status = -2;
        r.error = strdup("unknown_query_type");
        return r;
    }
    
    r.status = tq_r.status;
    r.count = tq_r.count;
    if (tq_r.value) r.value = strdup(tq_r.value);
    if (tq_r.error) r.error = strdup(tq_r.error);
    
    free(tq_r.value);
    free(tq_r.error);
    
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "query:%s:%s status:%d count:%d", query_type, query_value, r.status, r.count);
    
    return r;
}

/*##method##[TruthServer|find_path|inquiry|graph_path]*/
/*{[Input:source_class|target_class][Output:ResultT][Side_effects:finds_path_using_relationship_graph][Pattern:param_validate_execute]}*/
TruthServer_Result TruthServer_find_path(TruthServer_State * self, const char * source_class, const char * target_class) {
    TruthServer_Result r = {NULL, NULL, 0, 0};
    TruthSchema_Result ts_r;
    char buffer[TRUTHSERVER_MAX_PACKET];
    int len = 0;
    
    if (!self || !self->is_booted || !source_class || !target_class) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthServer:find_path][Source:%s][Target:%s][Results:", source_class, target_class);
    
    ts_r = TruthSchema_find_path(self->truthschema, "class", atoi(source_class), "class", atoi(target_class));
    if (ts_r.value) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "%s", ts_r.value);
        free(ts_r.value);
    }
    if (ts_r.error) free(ts_r.error);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    
    r.status = ts_r.status;
    r.count = ts_r.count;
    r.value = strdup(buffer);
    
    self->query_count++;
    snprintf(self->last_message, sizeof(self->last_message), "find_path:%s->%s status:%d", source_class, target_class, r.status);
    
    return r;
}

/*##method##[TruthServer|record_decision|storage|decision_recording]*/
/*{[Input:problem|discussion|decision|result|outcome|context][Output:ResultT][Side_effects:records_structured_decision][Pattern:param_validate_execute]}*/
TruthServer_Result TruthServer_record_decision(TruthServer_State * self, const char * problem, const char * discussion, const char * decision, const char * result, const char * outcome, const char * context) {
    TruthServer_Result r = {NULL, NULL, 0, 0};
    TruthSchema_Result ts_r;
    
    if (!self || !self->is_booted || !problem || !decision) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    ts_r = TruthSchema_record_decision(self->truthschema, problem, discussion, decision, result, outcome, context);
    
    r.status = ts_r.status;
    r.count = ts_r.count;
    if (ts_r.value) r.value = strdup(ts_r.value);
    if (ts_r.error) r.error = strdup(ts_r.error);
    
    free(ts_r.value);
    free(ts_r.error);
    
    snprintf(self->last_message, sizeof(self->last_message), "record_decision:%s status:%d", problem, r.status);
    
    return r;
}

/*##method##[TruthServer|seed_lessons|storage|auto_seeding]*/
/*{[Input:none][Output:status][Side_effects:seeds_initial_lessons_from_cascade_memories][Pattern:param_validate_execute]}*/
int TruthServer_seed_lessons(TruthServer_State * self) {
    int count = 0;
    
    if (!self || !self->is_booted) return 0;
    
    count += TruthQuery_lesson_put(self->truthquery, "file_is_maxed", "File complete like letter W, no dependencies no stubs, self contained domain", "architecture");
    count += TruthQuery_lesson_put(self->truthquery, "no_main_in_domains", "No main in domain files, TEST_Engine tests all", "architecture");
    count += TruthQuery_lesson_put(self->truthquery, "user_yaml_only", "User prefers YAML only, no JSON CSV", "preferences");
    count += TruthQuery_lesson_put(self->truthquery, "bracketed_format_memories", "Bracketed format memories, compact machine readable", "preferences");
    count += TruthQuery_lesson_put(self->truthquery, "chat_exports_to_memories", "Chat exports to Memories folder then extracted", "workflow");
    count += TruthQuery_lesson_put(self->truthquery, "never_delete_f1_c", "Never delete f1.c, active parser work", "protection");
    count += TruthQuery_lesson_put(self->truthquery, "never_edit_core_setup", "Never edit above line 47 in Core_Setup.py, CastleDefense protection", "protection");
    count += TruthQuery_lesson_put(self->truthquery, "vbstyle_params_in_results_out", "VBSTYLE: params in, results out, no global state", "architecture");
    count += TruthQuery_lesson_put(self->truthquery, "no_validation_phrases", "No validation phrases or acknowledgments in communication", "communication");
    count += TruthQuery_lesson_put(self->truthquery, "use_absolute_paths", "Always use absolute paths, no relative paths", "architecture");
    
    snprintf(self->last_message, sizeof(self->last_message), "seeded_lessons:%d", count);
    return count;
}

/*##method##[TruthServer|seed_authority_graph|storage|auto_seeding]*/
/*{[Input:none][Output:status][Side_effects:seeds_initial_authority_graph][Pattern:param_validate_execute]}*/
int TruthServer_seed_authority_graph(TruthServer_State * self) {
    int count = 0;
    
    if (!self || !self->is_booted) return 0;
    
    count += TruthQuery_authority_add(self->truthquery, "DatabaseManager", "class", "storage", NULL);
    count += TruthQuery_authority_add(self->truthquery, "SqliteHelper", "class", "mutation", "DatabaseManager");
    count += TruthQuery_authority_add(self->truthquery, "BootResolver", "function", "orchestration", "DatabaseManager");
    count += TruthQuery_authority_add(self->truthquery, "MemUnit", "class", "memory_orchestration", NULL);
    count += TruthQuery_authority_add(self->truthquery, "TruthQuery", "class", "inquiry", "MemUnit");
    count += TruthQuery_authority_add(self->truthquery, "TruthServer", "class", "orchestration", "MemUnit");
    
    snprintf(self->last_message, sizeof(self->last_message), "seeded_authority_graph:%d", count);
    return count;
}

/*##method##[TruthServer|health|inquiry|system_health]*/
/*{[Input:none][Output:ResultT][Side_effects:checks_health_of_all_components][Pattern:param_validate_execute]}*/
TruthServer_Result TruthServer_health(TruthServer_State * self) {
    TruthServer_Result r = {NULL, NULL, 0, 0};
    char buffer[TRUTHSERVER_MAX_PACKET];
    int len = 0;
    
    if (!self) {
        r.status = -1;
        r.error = strdup("null_server");
        return r;
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[TruthServer:health][Version:%s][Booted:%d][Queries:%d][MemUnit:%p][TruthQuery:%p][DB:%s]}", 
                   TRUTHSERVER_VERSION, self->is_booted, self->query_count, 
                   (void *)self->memunit, (void *)self->truthquery, 
                   self->db_path[0] ? self->db_path : ":memory:");
    
    r.status = 0;
    r.value = strdup(buffer);
    snprintf(self->last_message, sizeof(self->last_message), "health_check_complete");
    
    return r;
}

/*##method##[TruthServer|cleanup|lifecycle|destructor]*/
/*{[Input:none][Output:status][Side_effects:cleansup_truthquery_truthschema_and_memunit][Pattern:param_validate_execute]}*/
int TruthServer_cleanup(TruthServer_State * self) {
    if (!self) return 0;
    
    if (self->truthschema) {
        TruthSchema_cleanup(self->truthschema);
        free(self->truthschema);
        self->truthschema = NULL;
    }
    
    if (self->truthquery) {
        TruthQuery_cleanup(self->truthquery);
        free(self->truthquery);
        self->truthquery = NULL;
    }
    
    if (self->memunit) {
        MemUnit_cleanup(self->memunit);
        free(self->memunit);
        self->memunit = NULL;
    }
    
    self->is_booted = 0;
    return 1;
}
