/*
#   Ghost[MemUnit:MemUnit Domain:Memory_Orchestration Purpose:Central_Execution_Surface
#   Owns:MemDB|MemBus|GuiDB|GuiBus|Orchestrator|Report|Cache|Search|Assembly
#   Accepts:config|commands|events Returns:results|status|packets|reports
#   Requires:SQLite3 Exposes:boot|route|dispatch|query|assemble|cache|wire|report|health|cleanup
#   State:db|bus|gui|orch|report|stats Health:system_status Tags:memunit,orchestrator,memdb,ram,truth,universal,complete ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#define MEMUNIT_VERSION "4.1.0"
#define MEMUNIT_MAX_SQL 8192
#define MEMUNIT_MAX_KEY 256
#define MEMUNIT_MAX_VALUE 8192
#define MEMUNIT_MAX_PACKET 65536
#define MEMUNIT_MAX_MESSAGE 1024
#define MEMUNIT_MAX_BUS_QUEUE 1024
#define MEMUNIT_MAX_REPORTS 64
#define MEMUNIT_MAX_WORLD_BINDINGS 16

/*##section##[MemDB|DatabaseSurface]*/
typedef struct MemUnit_MemDB {
    sqlite3 * db;
    int is_open;
    int is_persistent;
    char db_path[MEMUNIT_MAX_KEY];
    char * last_error;
} MemUnit_MemDB;

/*##section##[MemBus|MessageRoutingSurface]*/
typedef struct MemUnit_BusMessage {
    int msg_id;
    char sender[MEMUNIT_MAX_KEY];
    char recipient[MEMUNIT_MAX_KEY];
    char verb[MEMUNIT_MAX_KEY];
    char payload[MEMUNIT_MAX_VALUE];
    int priority;
    time_t timestamp;
    int processed;
} MemUnit_BusMessage;

typedef struct MemUnit_MemBus {
    MemUnit_BusMessage queue[MEMUNIT_MAX_BUS_QUEUE];
    int head;
    int tail;
    int count;
    sqlite3_stmt * insert_stmt;
    sqlite3_stmt * select_stmt;
    sqlite3_stmt * mark_processed_stmt;
} MemUnit_MemBus;

/*##section##[GuiDB|GUITruthStorage]*/
typedef struct MemUnit_GuiDB {
    sqlite3_stmt * widget_stmt;
    sqlite3_stmt * layout_stmt;
    sqlite3_stmt * state_stmt;
    sqlite3_stmt * action_stmt;
} MemUnit_GuiDB;

/*##section##[GuiBus|GUIEventRouting]*/
typedef struct MemUnit_GuiBus {
    MemUnit_BusMessage event_queue[MEMUNIT_MAX_BUS_QUEUE];
    int head;
    int tail;
    int count;
    sqlite3_stmt * event_insert;
    sqlite3_stmt * event_select;
    sqlite3_stmt * event_ack;
} MemUnit_GuiBus;

/*##section##[Orchestrator|CoordinationEngine]*/
typedef struct MemUnit_Orchestrator {
    int is_running;
    int cycle_count;
    char current_phase[MEMUNIT_MAX_KEY];
    sqlite3_stmt * world_lookup;
    sqlite3_stmt * dependency_check;
    sqlite3_stmt * route_decision;
} MemUnit_Orchestrator;

/*##section##[Report|OutputSurface]*/
typedef struct MemUnit_ReportEntry {
    int id;
    char category[MEMUNIT_MAX_KEY];
    char headline[MEMUNIT_MAX_VALUE];
    char details[MEMUNIT_MAX_PACKET];
    int severity;
    time_t timestamp;
    int archived;
} MemUnit_ReportEntry;

typedef struct MemUnit_Report {
    MemUnit_ReportEntry entries[MEMUNIT_MAX_REPORTS];
    int count;
    sqlite3_stmt * insert_stmt;
    sqlite3_stmt * select_stmt;
    sqlite3_stmt * archive_stmt;
} MemUnit_Report;

/*##section##[OutputCenter|DeliverySurface]*/
typedef struct MemUnit_OutputCenter {
    sqlite3_stmt * insert_stmt;
    sqlite3_stmt * select_stmt;
} MemUnit_OutputCenter;

/*##section##[SearchEngine|DiscoverySurface]*/
typedef struct MemUnit_SearchEngine {
    sqlite3_stmt * by_verb;
    sqlite3_stmt * by_authority;
    sqlite3_stmt * by_magk;
    sqlite3_stmt * by_bracket_sig;
    int last_count;
} MemUnit_SearchEngine;

/*##section##[AssemblyEngine|PacketBuildingSurface]*/
typedef struct MemUnit_AssemblyEngine {
    char packet_buffer[MEMUNIT_MAX_PACKET];
    sqlite3_stmt * seed_query;
    sqlite3_stmt * related_query;
    sqlite3_stmt * store_packet;
    int last_size;
} MemUnit_AssemblyEngine;

/*##section##[CacheEngine|FastStorageSurface]*/
typedef struct MemUnit_CacheEngine {
    sqlite3_stmt * get_stmt;
    sqlite3_stmt * put_stmt;
    sqlite3_stmt * invalidate_stmt;
    sqlite3_stmt * hit_update;
    int total_hits;
    int total_misses;
} MemUnit_CacheEngine;

/*##section##[ResultT|3TupleContract]*/
typedef struct MemUnit_Result {
    char * value;
    char * error;
    int status;
    int count;
} MemUnit_Result;

/*##section##[RuntimeWorldBinding|LiveExecutionBinding]*/
typedef int (*MemUnit_WorldBootFn)(void * world_state, const char * config);
typedef const char * (*MemUnit_WorldChatFn)(void * world_state, const char * prompt, int max_tokens);
typedef const char * (*MemUnit_WorldHealthFn)(void * world_state);
typedef const char * (*MemUnit_WorldExecTextFn)(void * world_state, const char * action, const char * payload, int number_arg);
typedef void (*MemUnit_WorldCleanupFn)(void * world_state);

typedef struct MemUnit_RuntimeWorldBinding {
    char world_name[MEMUNIT_MAX_KEY];
    char authority[MEMUNIT_MAX_KEY];
    void * world_state;
    MemUnit_WorldBootFn boot_fn;
    MemUnit_WorldChatFn chat_fn;
    MemUnit_WorldHealthFn health_fn;
    MemUnit_WorldExecTextFn exec_text_fn;
    MemUnit_WorldCleanupFn cleanup_fn;
    int is_bound;
} MemUnit_RuntimeWorldBinding;

/*##section##[State|WorldContainer]*/
typedef struct MemUnit_State {
    MemUnit_MemDB memdb;
    MemUnit_MemBus membus;
    MemUnit_GuiDB guidb;
    MemUnit_GuiBus guibus;
    MemUnit_Orchestrator orchestrator;
    MemUnit_Report report;
    MemUnit_OutputCenter output;
    MemUnit_SearchEngine search;
    MemUnit_AssemblyEngine assembly;
    MemUnit_CacheEngine cache;
    int is_booted;
    int health_code;
    char last_message[MEMUNIT_MAX_VALUE];
    char last_result[MEMUNIT_MAX_PACKET];
    MemUnit_RuntimeWorldBinding bindings[MEMUNIT_MAX_WORLD_BINDINGS];
    int binding_count;
    time_t boot_time;
    int command_count;
} MemUnit_State;

int MemUnit_cache_put(MemUnit_State * self, const char * key, const char * value);
MemUnit_Result MemUnit_wire(MemUnit_State * self, const char * world_name, const char * authority, const char * config, const char * deps);
int MemUnit_output_enqueue(MemUnit_State * self, const char * target_type, const char * target_path, const char * format, const char * payload);
sqlite3 * MemUnit_get_db(MemUnit_State * self);

static MemUnit_RuntimeWorldBinding * MemUnit_find_binding(MemUnit_State * self, const char * world_name) {
    int index;
    if (!self || !world_name) return NULL;
    for (index = 0; index < self->binding_count; ++index) {
        if (self->bindings[index].is_bound && strcmp(self->bindings[index].world_name, world_name) == 0) {
            return &self->bindings[index];
        }
    }
    return NULL;
}

static int MemUnit_session_set(MemUnit_State * self, const char * key, const char * value) {
    sqlite3_stmt * stmt = NULL;
    int rc;
    if (!self || !self->memdb.db || !key || !value) return 0;
    rc = sqlite3_prepare_v2(
        self->memdb.db,
        "INSERT OR REPLACE INTO session_state(session_key,session_value,timestamp) VALUES(?1,?2,unixepoch())",
        -1,
        &stmt,
        NULL
    );
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

static int MemUnit_counter_bump(MemUnit_State * self, const char * key, int delta) {
    sqlite3_stmt * stmt = NULL;
    int rc;
    if (!self || !self->memdb.db || !key) return 0;
    rc = sqlite3_prepare_v2(
        self->memdb.db,
        "INSERT INTO runtime_counter(counter_key,counter_value,timestamp) VALUES(?1,?2,unixepoch()) "
        "ON CONFLICT(counter_key) DO UPDATE SET counter_value=runtime_counter.counter_value + excluded.counter_value,timestamp=unixepoch()",
        -1,
        &stmt,
        NULL
    );
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, delta);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

static int MemUnit_record_execution_route(MemUnit_State * self, const char * world_name, const char * action, const char * detail, int status_code) {
    sqlite3_stmt * stmt = NULL;
    int rc;
    if (!self || !self->memdb.db || !world_name || !action) return 0;
    rc = sqlite3_prepare_v2(
        self->memdb.db,
        "INSERT INTO execution_route(world,action,detail,status_code,timestamp) VALUES(?1,?2,?3,?4,unixepoch())",
        -1,
        &stmt,
        NULL
    );
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, world_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, action, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, detail ? detail : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, status_code);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

/*##method##[MemUnit|create|allocation|constructor]*/
/*{[Input:none][Output:MemUnit_State_ptr][Side_effects:allocates_state][Pattern:param_validate_execute]}*/
MemUnit_State * MemUnit_create(void) {
    MemUnit_State * self = (MemUnit_State *)calloc(1, sizeof(MemUnit_State));
    return self;
}

/*##method##[MemUnit|boot|lifecycle|initialization]*/
/*{[Input:config][Output:status][Side_effects:creates_all_tables_statements][Pattern:param_validate_execute]}*/
int MemUnit_boot(MemUnit_State * self, const char * config) {
    char sql[MEMUNIT_MAX_SQL];
    char * err = NULL;
    int rc;
    const char * db_path = NULL;
    
    if (!self) return -1;
    memset(self, 0, sizeof(*self));
    
    if (config && strstr(config, "persistent=") != NULL) {
        db_path = strstr(config, "persistent=") + 10;
        char * end = strchr(db_path, ',');
        if (end) {
            size_t len = end - db_path;
            if (len < sizeof(self->memdb.db_path)) {
                memcpy(self->memdb.db_path, db_path, len);
                self->memdb.db_path[len] = '\0';
                db_path = self->memdb.db_path;
            }
        } else {
            snprintf(self->memdb.db_path, sizeof(self->memdb.db_path), "%s", db_path);
            db_path = self->memdb.db_path;
        }
    }
    
    if (db_path && strlen(db_path) > 0) {
        rc = sqlite3_open(db_path, &self->memdb.db);
        if (rc != SQLITE_OK) {
            snprintf(self->last_message, sizeof(self->last_message), "sqlite3_open_failed:%s:%d", db_path, rc);
            return -1;
        }
        self->memdb.is_persistent = 1;
    } else {
        rc = sqlite3_open(":memory:", &self->memdb.db);
        if (rc != SQLITE_OK) return -1;
        self->memdb.is_persistent = 0;
    }
    self->memdb.is_open = 1;
    
    /* MemDB core tables */
    snprintf(sql, sizeof(sql),
        "CREATE TABLE verb_registry(verb TEXT PRIMARY KEY,authority TEXT,handler TEXT,magk TEXT,bracket_sig TEXT,timestamp INTEGER DEFAULT(unixepoch()));"
        "CREATE TABLE worlds(name TEXT PRIMARY KEY,authority TEXT,status TEXT,config TEXT,deps TEXT,timestamp INTEGER DEFAULT(unixepoch()));"
        "CREATE TABLE cache(key TEXT PRIMARY KEY,value TEXT,hits INTEGER DEFAULT 0,timestamp INTEGER DEFAULT(unixepoch()));"
        "CREATE TABLE packets(id INTEGER PRIMARY KEY,seed TEXT,radius INTEGER,content TEXT,timestamp INTEGER DEFAULT(unixepoch()));"
        "CREATE TABLE bus_messages(msg_id INTEGER PRIMARY KEY,sender TEXT,recipient TEXT,verb TEXT,payload TEXT,priority INTEGER,timestamp INTEGER,processed INTEGER DEFAULT 0);"
        "CREATE TABLE gui_widgets(id TEXT PRIMARY KEY,widget_type TEXT,properties TEXT,parent TEXT,order_idx INTEGER);"
        "CREATE TABLE gui_layouts(id TEXT PRIMARY KEY,layout_type TEXT,config TEXT,active INTEGER);"
        "CREATE TABLE gui_state(widget_id TEXT,key TEXT,value TEXT,PRIMARY KEY(widget_id,key));"
        "CREATE TABLE gui_actions(action_id TEXT PRIMARY KEY,widget_id TEXT,event_type TEXT,handler TEXT,enabled INTEGER);"
        "CREATE TABLE orchestrator_queue(seq INTEGER PRIMARY KEY,world TEXT,phase TEXT,status TEXT,priority INTEGER);"
        "CREATE TABLE reports(id INTEGER PRIMARY KEY,category TEXT,headline TEXT,details TEXT,severity INTEGER,timestamp INTEGER,archived INTEGER DEFAULT 0);"
        "CREATE TABLE output_queue(id INTEGER PRIMARY KEY,target_type TEXT,target_path TEXT,format TEXT,payload TEXT,timestamp INTEGER DEFAULT(unixepoch()),delivered INTEGER DEFAULT 0);"
        "CREATE TABLE session_state(session_key TEXT PRIMARY KEY,session_value TEXT,timestamp INTEGER DEFAULT(unixepoch()));"
        "CREATE TABLE runtime_counter(counter_key TEXT PRIMARY KEY,counter_value INTEGER DEFAULT 0,timestamp INTEGER DEFAULT(unixepoch()));"
        "CREATE TABLE execution_route(route_id INTEGER PRIMARY KEY,world TEXT,action TEXT,detail TEXT,status_code INTEGER,timestamp INTEGER DEFAULT(unixepoch()));");
    
    rc = sqlite3_exec(self->memdb.db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return -2;
    }
    
    /* Prepare all statements */
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO bus_messages(sender,recipient,verb,payload,priority,timestamp) VALUES(?1,?2,?3,?4,?5,unixepoch())", -1, &self->membus.insert_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT msg_id,sender,recipient,verb,payload,priority FROM bus_messages WHERE processed=0 ORDER BY priority DESC,timestamp ASC LIMIT 1", -1, &self->membus.select_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "UPDATE bus_messages SET processed=1 WHERE msg_id=?1", -1, &self->membus.mark_processed_stmt, NULL);
    
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO gui_widgets(id,widget_type,properties,parent,order_idx) VALUES(?1,?2,?3,?4,?5)", -1, &self->guidb.widget_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO gui_layouts(id,layout_type,config,active) VALUES(?1,?2,?3,?4)", -1, &self->guidb.layout_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO gui_state(widget_id,key,value) VALUES(?1,?2,?3)", -1, &self->guidb.state_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO gui_actions(action_id,widget_id,event_type,handler,enabled) VALUES(?1,?2,?3,?4,?5)", -1, &self->guidb.action_stmt, NULL);
    
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO bus_messages(sender,recipient,verb,payload,timestamp) VALUES('gui',?1,?2,?3,unixepoch())", -1, &self->guibus.event_insert, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT rowid,recipient,verb,payload FROM bus_messages WHERE sender='gui' AND processed=0 LIMIT 1", -1, &self->guibus.event_select, NULL);
    sqlite3_prepare_v2(self->memdb.db, "UPDATE bus_messages SET processed=1 WHERE rowid=?1", -1, &self->guibus.event_ack, NULL);
    
    sqlite3_prepare_v2(self->memdb.db, "SELECT name,authority,status FROM worlds WHERE name=?1", -1, &self->orchestrator.world_lookup, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT deps FROM worlds WHERE name=?1", -1, &self->orchestrator.dependency_check, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT handler FROM verb_registry WHERE verb=?1 AND authority=?2", -1, &self->orchestrator.route_decision, NULL);
    
    sqlite3_prepare_v2(self->memdb.db, "INSERT INTO reports(category,headline,details,severity,timestamp) VALUES(?1,?2,?3,?4,unixepoch())", -1, &self->report.insert_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT id,category,headline,details,severity,timestamp FROM reports WHERE archived=0 ORDER BY severity DESC,timestamp DESC", -1, &self->report.select_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "UPDATE reports SET archived=1 WHERE id=?1", -1, &self->report.archive_stmt, NULL);

    sqlite3_prepare_v2(self->memdb.db, "INSERT INTO output_queue(target_type,target_path,format,payload,timestamp) VALUES(?1,?2,?3,?4,unixepoch())", -1, &self->output.insert_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT id,target_type,target_path,format,payload FROM output_queue WHERE delivered=0 ORDER BY timestamp ASC", -1, &self->output.select_stmt, NULL);
    
    sqlite3_prepare_v2(self->memdb.db, "SELECT verb,handler,magk FROM verb_registry WHERE verb=?1", -1, &self->search.by_verb, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT verb,handler FROM verb_registry WHERE authority=?1", -1, &self->search.by_authority, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT verb,handler FROM verb_registry WHERE magk=?1", -1, &self->search.by_magk, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT verb,handler FROM verb_registry WHERE bracket_sig LIKE ?1", -1, &self->search.by_bracket_sig, NULL);
    
    sqlite3_prepare_v2(self->memdb.db, "SELECT content FROM packets WHERE seed=?1 AND radius=?2 ORDER BY timestamp DESC LIMIT 1", -1, &self->assembly.seed_query, NULL);
    sqlite3_prepare_v2(self->memdb.db, "SELECT v2.verb,v2.handler FROM verb_registry v1 JOIN verb_registry v2 ON v1.magk=v2.magk WHERE v1.verb=?1 AND v2.verb!=?1", -1, &self->assembly.related_query, NULL);
    sqlite3_prepare_v2(self->memdb.db, "INSERT INTO packets(seed,radius,content) VALUES(?1,?2,?3)", -1, &self->assembly.store_packet, NULL);
    
    sqlite3_prepare_v2(self->memdb.db, "SELECT value FROM cache WHERE key=?1", -1, &self->cache.get_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO cache(key,value,timestamp) VALUES(?1,?2,unixepoch())", -1, &self->cache.put_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "DELETE FROM cache WHERE key=?1", -1, &self->cache.invalidate_stmt, NULL);
    sqlite3_prepare_v2(self->memdb.db, "UPDATE cache SET hits=hits+1 WHERE key=?1", -1, &self->cache.hit_update, NULL);
    
    self->is_booted = 1;
    self->health_code = 100;
    self->boot_time = time(NULL);
    snprintf(self->last_message, sizeof(self->last_message), "MemUnit v%s booted", MEMUNIT_VERSION);
    self->last_result[0] = '\0';
    MemUnit_counter_bump(self, "boot_count", 1);
    MemUnit_session_set(self, "memunit_version", MEMUNIT_VERSION);
    
    (void)config;
    return 0;
}

/*##method##[MemUnit|seed_defaults|registry|demo_data]*/
/*{[Input:none][Output:status][Side_effects:seeds_worlds_and_verb_registry][Pattern:param_validate_execute]}*/
int MemUnit_seed_defaults(MemUnit_State * self) {
    char * err = NULL;
    const char * sql =
        "INSERT OR REPLACE INTO worlds(name,authority,status,config,deps) VALUES"
        "('MemUnit','Memory_Orchestration','booted','db=:memory:','SQLite3'),"
        "('InferenceWorld','GGUF_Inference','available','resident=1','llama_cpp_bridge_runtime');"
        "INSERT OR REPLACE INTO verb_registry(verb,authority,handler,magk,bracket_sig) VALUES"
        "('boot','GGUF_Inference','InferenceWorld_boot','model_runtime','[World:InferenceWorld][Action:boot]'),"
        "('chat','GGUF_Inference','InferenceWorld_chat','model_runtime','[World:InferenceWorld][Action:chat]'),"
        "('health','GGUF_Inference','InferenceWorld_health','model_runtime','[World:InferenceWorld][Action:health]'),"
        "('cache_put','MemUnit','MemUnit_cache_put','memory_runtime','[World:MemUnit][Action:cache_put]'),"
        "('cache_get','MemUnit','MemUnit_cache_get','memory_runtime','[World:MemUnit][Action:cache_get]'),"
        "('assemble','MemUnit','MemUnit_assemble','memory_runtime','[World:MemUnit][Action:assemble]'),"
        "('query','MemUnit','MemUnit_query','memory_runtime','[World:MemUnit][Action:query]'),"
        "('execute','MemUnit','MemUnit_execute','memory_runtime','[World:MemUnit][Action:execute]'),"
        "('bind_world','MemUnit','MemUnit_bind_world_runtime','memory_runtime','[World:MemUnit][Action:bind_world]');";

    if (!self || !self->is_booted || !self->memdb.db) return 0;
    if (sqlite3_exec(self->memdb.db, sql, NULL, NULL, &err) != SQLITE_OK) {
        if (err) {
            snprintf(self->last_message, sizeof(self->last_message), "seed_defaults_failed:%s", err);
            sqlite3_free(err);
        }
        return 0;
    }
    self->command_count++;
    snprintf(self->last_message, sizeof(self->last_message), "seeded_defaults");
    return 1;
}

/*##method##[MemUnit|bind_world_runtime|world_registration|live_binding]*/
/*{[Input:world_name|authority|config|deps|world_state|callbacks][Output:ResultT][Side_effects:registers_and_binds_runtime_world][Pattern:param_validate_execute]}*/
MemUnit_Result MemUnit_bind_world_runtime(
    MemUnit_State * self,
    const char * world_name,
    const char * authority,
    const char * config,
    const char * deps,
    void * world_state,
    MemUnit_WorldBootFn boot_fn,
    MemUnit_WorldChatFn chat_fn,
    MemUnit_WorldHealthFn health_fn,
    MemUnit_WorldExecTextFn exec_text_fn,
    MemUnit_WorldCleanupFn cleanup_fn
) {
    MemUnit_Result r = {NULL, NULL, 0, 0};
    MemUnit_RuntimeWorldBinding * binding;

    if (!self || !self->is_booted || !world_name || !authority || !world_state) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }

    binding = MemUnit_find_binding(self, world_name);
    if (!binding) {
        if (self->binding_count >= MEMUNIT_MAX_WORLD_BINDINGS) {
            r.status = -2;
            r.error = strdup("binding_limit_reached");
            return r;
        }
        binding = &self->bindings[self->binding_count++];
        memset(binding, 0, sizeof(*binding));
    }

    snprintf(binding->world_name, sizeof(binding->world_name), "%s", world_name);
    snprintf(binding->authority, sizeof(binding->authority), "%s", authority);
    binding->world_state = world_state;
    binding->boot_fn = boot_fn;
    binding->chat_fn = chat_fn;
    binding->health_fn = health_fn;
    binding->exec_text_fn = exec_text_fn;
    binding->cleanup_fn = cleanup_fn;
    binding->is_bound = 1;

    r = MemUnit_wire(self, world_name, authority, config, deps);
    if (r.status == 0) {
        MemUnit_session_set(self, "last_bound_world", world_name);
        MemUnit_counter_bump(self, "world_bind_count", 1);
        MemUnit_record_execution_route(self, world_name, "bind", authority, 0);
        snprintf(self->last_message, sizeof(self->last_message), "bound_runtime_world:%s", world_name);
    }
    return r;
}

/*##method##[MemUnit|route|membus|message_routing]*/
/*{[Input:sender|recipient|verb|payload|priority][Output:status][Side_effects:enqueues_to_membus][Pattern:param_validate_execute]}*/
int MemUnit_route(MemUnit_State * self, const char * sender, const char * recipient, const char * verb, const char * payload, int priority) {
    if (!self || !self->is_booted || !sender || !recipient || !verb) return 0;
    
    sqlite3_bind_text(self->membus.insert_stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->membus.insert_stmt, 2, recipient, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->membus.insert_stmt, 3, verb, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->membus.insert_stmt, 4, payload ? payload : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(self->membus.insert_stmt, 5, priority);
    
    int rc = sqlite3_step(self->membus.insert_stmt);
    sqlite3_reset(self->membus.insert_stmt);
    
    if (rc == SQLITE_DONE) {
        self->command_count++;
        snprintf(self->last_message, sizeof(self->last_message), "routed:%s->%s:%s", sender, recipient, verb);
        return 1;
    }
    return 0;
}

/*##method##[MemUnit|dispatch|orchestrator|execution]*/
/*{[Input:none][Output:status][Side_effects:processes_one_membus_message][Pattern:param_validate_execute]}*/
int MemUnit_dispatch(MemUnit_State * self) {
    int rc;
    int msg_id;
    const char * recipient;
    const char * verb;
    char recipient_copy[MEMUNIT_MAX_KEY];
    char verb_copy[MEMUNIT_MAX_KEY];
    
    if (!self || !self->is_booted) return 0;
    
    rc = sqlite3_step(self->membus.select_stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_reset(self->membus.select_stmt);
        return 0;
    }
    
    msg_id = sqlite3_column_int(self->membus.select_stmt, 0);
    recipient = (const char *)sqlite3_column_text(self->membus.select_stmt, 2);
    verb = (const char *)sqlite3_column_text(self->membus.select_stmt, 3);
    snprintf(recipient_copy, sizeof(recipient_copy), "%s", recipient ? recipient : "");
    snprintf(verb_copy, sizeof(verb_copy), "%s", verb ? verb : "");
    
    sqlite3_bind_int(self->membus.mark_processed_stmt, 1, msg_id);
    sqlite3_step(self->membus.mark_processed_stmt);
    sqlite3_reset(self->membus.mark_processed_stmt);
    sqlite3_reset(self->membus.select_stmt);
    
    self->orchestrator.cycle_count++;
    snprintf(self->orchestrator.current_phase, sizeof(self->orchestrator.current_phase), "%s:%s", recipient_copy, verb_copy);
    self->command_count++;
    
    return 1;
}

/*##method##[MemUnit|query|search|database_lookup]*/
/*{[Input:query_type|query_value][Output:ResultT][Side_effects:queries_verb_registry][Pattern:param_validate_execute]}*/
MemUnit_Result MemUnit_query(MemUnit_State * self, const char * type, const char * value) {
    MemUnit_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt = NULL;
    char buffer[MEMUNIT_MAX_PACKET];
    int len = 0;
    
    if (!self || !self->is_booted || !type || !value) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    if (strcmp(type, "verb") == 0) stmt = self->search.by_verb;
    else if (strcmp(type, "authority") == 0) stmt = self->search.by_authority;
    else if (strcmp(type, "magk") == 0) stmt = self->search.by_magk;
    else if (strcmp(type, "bracket_sig") == 0) stmt = self->search.by_bracket_sig;
    else {
        r.status = -2;
        r.error = strdup("unknown_query_type");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_STATIC);
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "{[Results:");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char * v = (const char *)sqlite3_column_text(stmt, 0);
        const char * h = (const char *)sqlite3_column_text(stmt, 1);
        len += snprintf(buffer + len, sizeof(buffer) - len, "[%s|%s]", v ? v : "", h ? h : "");
        r.count++;
    }
    len += snprintf(buffer + len, sizeof(buffer) - len, "]}");
    sqlite3_reset(stmt);
    
    r.status = 0;
    r.value = strdup(buffer);
    self->search.last_count = r.count;
    self->command_count++;
    snprintf(self->last_message, sizeof(self->last_message), "query:%s:%s found:%d", type, value, r.count);
    
    return r;
}

/*##method##[MemUnit|search_text|query|compat_surface]*/
/*{[Input:type|value][Output:text][Side_effects:caches_last_result_text][Pattern:param_validate_execute]}*/
const char * MemUnit_search_text(MemUnit_State * self, const char * type, const char * value) {
    MemUnit_Result r;
    if (!self) return NULL;
    r = MemUnit_query(self, type, value);
    self->last_result[0] = '\0';
    if (r.value) {
        snprintf(self->last_result, sizeof(self->last_result), "%s", r.value);
    } else if (r.error) {
        snprintf(self->last_result, sizeof(self->last_result), "{[Error:%s]}", r.error);
    }
    free(r.value);
    free(r.error);
    return self->last_result[0] ? self->last_result : NULL;
}

/*##method##[MemUnit|assemble|packet|building]*/
/*{[Input:seed|radius|mode][Output:ResultT][Side_effects:builds_packet_from_database][Pattern:param_validate_execute]}*/
MemUnit_Result MemUnit_assemble(MemUnit_State * self, const char * seed, int radius, const char * mode) {
    MemUnit_Result r = {NULL, NULL, 0, 0};
    char packet[MEMUNIT_MAX_PACKET];
    int len = 0;
    
    if (!self || !self->is_booted || !seed) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    len += snprintf(packet + len, sizeof(packet) - len, "{[Packet|Seed:%s|Radius:%d|Mode:%s][Items:", seed, radius, mode ? mode : "related");
    
    if (strcmp(mode, "related") == 0) {
        sqlite3_bind_text(self->assembly.related_query, 1, seed, -1, SQLITE_STATIC);
        while (sqlite3_step(self->assembly.related_query) == SQLITE_ROW && r.count < radius * 5) {
            const char * v = (const char *)sqlite3_column_text(self->assembly.related_query, 0);
            len += snprintf(packet + len, sizeof(packet) - len, "[%s]", v ? v : "");
            r.count++;
        }
        sqlite3_reset(self->assembly.related_query);
    }
    
    len += snprintf(packet + len, sizeof(packet) - len, "]]}");
    
    sqlite3_bind_text(self->assembly.store_packet, 1, seed, -1, SQLITE_STATIC);
    sqlite3_bind_int(self->assembly.store_packet, 2, radius);
    sqlite3_bind_text(self->assembly.store_packet, 3, packet, -1, SQLITE_STATIC);
    sqlite3_step(self->assembly.store_packet);
    sqlite3_reset(self->assembly.store_packet);
    
    r.status = 0;
    r.value = strdup(packet);
    self->assembly.last_size = len;
    self->command_count++;
    snprintf(self->last_message, sizeof(self->last_message), "assembled:%s items:%d", seed, r.count);
    
    return r;
}

/*##method##[MemUnit|assemble_text|packet|compat_surface]*/
/*{[Input:seed|radius|mode][Output:text][Side_effects:caches_last_packet_text][Pattern:param_validate_execute]}*/
const char * MemUnit_assemble_text(MemUnit_State * self, const char * seed, int radius, const char * mode) {
    MemUnit_Result r;
    if (!self) return NULL;
    r = MemUnit_assemble(self, seed, radius, mode);
    self->last_result[0] = '\0';
    if (r.value) {
        snprintf(self->last_result, sizeof(self->last_result), "%s", r.value);
    } else if (r.error) {
        snprintf(self->last_result, sizeof(self->last_result), "{[Error:%s]}", r.error);
    }
    free(r.value);
    free(r.error);
    return self->last_result[0] ? self->last_result : NULL;
}

/*##method##[MemUnit|cache_get|fast_storage]*/
/*{[Input:key][Output:ResultT][Side_effects:updates_hit_count][Pattern:param_validate_execute]}*/
MemUnit_Result MemUnit_cache_get(MemUnit_State * self, const char * key) {
    MemUnit_Result r = {NULL, NULL, 0, 0};
    
    if (!self || !self->is_booted || !key) {
        r.status = -1;
        r.error = strdup("invalid_params");
        self->cache.total_misses++;
        return r;
    }
    
    sqlite3_bind_text(self->cache.get_stmt, 1, key, -1, SQLITE_STATIC);
    
    if (sqlite3_step(self->cache.get_stmt) == SQLITE_ROW) {
        const char * v = (const char *)sqlite3_column_text(self->cache.get_stmt, 0);
        if (v) {
            r.value = strdup(v);
            r.status = 0;
            sqlite3_bind_text(self->cache.hit_update, 1, key, -1, SQLITE_STATIC);
            sqlite3_step(self->cache.hit_update);
            sqlite3_reset(self->cache.hit_update);
            self->cache.total_hits++;
            snprintf(self->last_message, sizeof(self->last_message), "cache_hit:%s", key);
        }
    } else {
        r.status = 1;
        r.error = strdup("not_found");
        self->cache.total_misses++;
        snprintf(self->last_message, sizeof(self->last_message), "cache_miss:%s", key);
    }
    
    sqlite3_reset(self->cache.get_stmt);
    return r;
}

/*##method##[MemUnit|cache_store|fast_storage|compat_surface]*/
/*{[Input:key|value][Output:status][Side_effects:stores_cache_entry][Pattern:param_validate_execute]}*/
int MemUnit_cache_store(MemUnit_State * self, const char * key, const char * value) {
    return MemUnit_cache_put(self, key, value);
}

/*##method##[MemUnit|cache_retrieve|fast_storage|compat_surface]*/
/*{[Input:key][Output:text][Side_effects:caches_last_cache_value][Pattern:param_validate_execute]}*/
const char * MemUnit_cache_retrieve(MemUnit_State * self, const char * key) {
    MemUnit_Result r;
    if (!self) return NULL;
    r = MemUnit_cache_get(self, key);
    self->last_result[0] = '\0';
    if (r.value) {
        snprintf(self->last_result, sizeof(self->last_result), "%s", r.value);
    }
    free(r.value);
    free(r.error);
    return self->last_result[0] ? self->last_result : NULL;
}

/*##method##[MemUnit|cache_put|fast_storage]*/
/*{[Input:key|value][Output:status][Side_effects:inserts_to_cache_table][Pattern:param_validate_execute]}*/
int MemUnit_cache_put(MemUnit_State * self, const char * key, const char * value) {
    int rc;
    
    if (!self || !self->is_booted || !key || !value) return 0;
    
    sqlite3_bind_text(self->cache.put_stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->cache.put_stmt, 2, value, -1, SQLITE_STATIC);
    rc = sqlite3_step(self->cache.put_stmt);
    sqlite3_reset(self->cache.put_stmt);
    
    if (rc == SQLITE_DONE) {
        self->command_count++;
        snprintf(self->last_message, sizeof(self->last_message), "cache_put:%s", key);
        return 1;
    }
    return 0;
}

/*##method##[MemUnit|gui_event|guibus|gui_routing]*/
/*{[Input:widget_id|event_type|payload][Output:status][Side_effects:enqueues_to_guibus][Pattern:param_validate_execute]}*/
int MemUnit_gui_event(MemUnit_State * self, const char * widget_id, const char * event_type, const char * payload) {
    int rc;
    
    if (!self || !self->is_booted || !widget_id || !event_type) return 0;
    
    sqlite3_bind_text(self->guibus.event_insert, 1, widget_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->guibus.event_insert, 2, event_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->guibus.event_insert, 3, payload ? payload : "", -1, SQLITE_STATIC);
    rc = sqlite3_step(self->guibus.event_insert);
    sqlite3_reset(self->guibus.event_insert);
    
    if (rc == SQLITE_DONE) {
        self->command_count++;
        snprintf(self->last_message, sizeof(self->last_message), "gui_event:%s:%s", widget_id, event_type);
        return 1;
    }
    return 0;
}

/*##method##[MemUnit|report|output_surface]*/
/*{[Input:category|headline|details|severity][Output:status][Side_effects:inserts_to_reports_table][Pattern:param_validate_execute]}*/
int MemUnit_report(MemUnit_State * self, const char * category, const char * headline, const char * details, int severity) {
    int rc;
    
    if (!self || !self->is_booted || !category || !headline) return 0;
    
    sqlite3_bind_text(self->report.insert_stmt, 1, category, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->report.insert_stmt, 2, headline, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->report.insert_stmt, 3, details ? details : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(self->report.insert_stmt, 4, severity);
    rc = sqlite3_step(self->report.insert_stmt);
    sqlite3_reset(self->report.insert_stmt);
    
    if (rc == SQLITE_DONE) {
        self->command_count++;
        snprintf(self->last_message, sizeof(self->last_message), "report:%s:%s", category, headline);
        return 1;
    }
    return 0;
}

/*##method##[MemUnit|output_enqueue|output_surface|delivery_queue]*/
/*{[Input:target_type|target_path|format|payload][Output:status][Side_effects:stores_pending_output][Pattern:param_validate_execute]}*/
int MemUnit_output_enqueue(MemUnit_State * self, const char * target_type, const char * target_path, const char * format, const char * payload) {
    int rc;
    if (!self || !self->is_booted || !target_type || !format || !payload) return 0;
    sqlite3_bind_text(self->output.insert_stmt, 1, target_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->output.insert_stmt, 2, target_path ? target_path : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(self->output.insert_stmt, 3, format, -1, SQLITE_STATIC);
    sqlite3_bind_text(self->output.insert_stmt, 4, payload, -1, SQLITE_STATIC);
    rc = sqlite3_step(self->output.insert_stmt);
    sqlite3_reset(self->output.insert_stmt);
    if (rc == SQLITE_DONE) {
        self->command_count++;
        snprintf(self->last_message, sizeof(self->last_message), "output:%s:%s", target_type, format);
        return 1;
    }
    return 0;
}

/*##method##[MemUnit|get_db|accessor|database_pointer]*/
/*{[Input:none][Output:sqlite3_ptr][Side_effects:none][Pattern:param_validate_execute]}*/
sqlite3 * MemUnit_get_db(MemUnit_State * self) {
    if (!self || !self->is_booted) return NULL;
    return self->memdb.db;
}

/*##method##[MemUnit|wire|world_registration]*/
/*{[Input:world_name|authority|config|deps][Output:ResultT][Side_effects:registers_in_worlds_table][Pattern:param_validate_execute]}*/
MemUnit_Result MemUnit_wire(MemUnit_State * self, const char * world_name, const char * authority, const char * config, const char * deps) {
    MemUnit_Result r = {NULL, NULL, 0, 0};
    sqlite3_stmt * stmt;
    int rc;
    
    if (!self || !self->is_booted || !world_name || !authority) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }
    
    rc = sqlite3_prepare_v2(self->memdb.db, "INSERT OR REPLACE INTO worlds(name,authority,status,config,deps) VALUES(?1,?2,'wired',?3,?4)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        r.status = -2;
        r.error = strdup("prepare_failed");
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, world_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, authority, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, config ? config : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, deps ? deps : "", -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        r.status = 0;
        r.value = strdup(world_name);
        self->command_count++;
        snprintf(self->last_message, sizeof(self->last_message), "wired_world:%s", world_name);
    } else {
        r.status = -3;
        r.error = strdup("insert_failed");
    }
    
    return r;
}

/*##method##[MemUnit|execute|world_runtime|central_authority]*/
/*{[Input:world_name|action|text_payload|number_arg][Output:ResultT][Side_effects:routes_dispatches_executes_and_records_state][Pattern:param_validate_execute]}*/
MemUnit_Result MemUnit_execute(MemUnit_State * self, const char * world_name, const char * action, const char * text_payload, int number_arg) {
    MemUnit_Result r = {NULL, NULL, 0, 0};
    MemUnit_RuntimeWorldBinding * binding;
    const char * text_result = NULL;
    int boot_rc;

    if (!self || !self->is_booted || !world_name || !action) {
        r.status = -1;
        r.error = strdup("invalid_params");
        return r;
    }

    binding = MemUnit_find_binding(self, world_name);
    if (!binding) {
        r.status = -2;
        r.error = strdup("world_not_bound");
        return r;
    }

    MemUnit_route(self, "driver", world_name, action, text_payload ? text_payload : "", 100);
    MemUnit_dispatch(self);
    snprintf(self->orchestrator.current_phase, sizeof(self->orchestrator.current_phase), "%s:%s", world_name, action);
    MemUnit_session_set(self, "last_world", world_name);
    MemUnit_session_set(self, "last_action", action);
    MemUnit_counter_bump(self, "execute_count", 1);

    if (strcmp(action, "boot") == 0) {
        if (!binding->boot_fn) {
            r.status = -3;
            r.error = strdup("boot_not_supported");
        } else {
            boot_rc = binding->boot_fn(binding->world_state, text_payload ? text_payload : "");
            r.status = boot_rc == 0 ? 0 : -4;
            r.value = strdup(boot_rc == 0 ? "booted" : "boot_failed");
            MemUnit_counter_bump(self, "boot_calls", 1);
            MemUnit_record_execution_route(self, world_name, action, text_payload ? text_payload : "", r.status);
            if (boot_rc == 0 && binding->health_fn) {
                const char * health = binding->health_fn(binding->world_state);
                MemUnit_cache_put(self, "inference_health", health ? health : "");
                MemUnit_report(self, "boot", world_name, health ? health : "booted", 1);
                MemUnit_output_enqueue(self, "screen", "", "json", health ? health : "booted");
            }
        }
        return r;
    }

    if (strcmp(action, "chat") == 0) {
        if (!binding->chat_fn) {
            if (!binding->exec_text_fn) {
                r.status = -5;
                r.error = strdup("chat_not_supported");
                return r;
            }
            text_result = binding->exec_text_fn(binding->world_state, action, text_payload ? text_payload : "", number_arg);
        } else {
            text_result = binding->chat_fn(binding->world_state, text_payload ? text_payload : "", number_arg);
        }
        if (!text_result) {
            r.status = -6;
            r.error = strdup("chat_failed");
        } else {
            r.status = 0;
            r.value = strdup(text_result);
            MemUnit_cache_put(self, "last_prompt", text_payload ? text_payload : "");
            MemUnit_cache_put(self, "last_response", text_result);
            MemUnit_report(self, "chat", "world_chat", text_result, 1);
            MemUnit_output_enqueue(self, "screen", "", "text", text_result);
            MemUnit_counter_bump(self, "chat_turns", 1);
            MemUnit_record_execution_route(self, world_name, action, text_payload ? text_payload : "", 0);
        }
        return r;
    }

    if (strcmp(action, "health") == 0) {
        if (!binding->health_fn) {
            if (!binding->exec_text_fn) {
                r.status = -7;
                r.error = strdup("health_not_supported");
                return r;
            }
            text_result = binding->exec_text_fn(binding->world_state, action, text_payload ? text_payload : "", number_arg);
        } else {
            text_result = binding->health_fn(binding->world_state);
        }
        if (!text_result) {
            r.status = -8;
            r.error = strdup("health_failed");
        } else {
            r.status = 0;
            r.value = strdup(text_result);
            MemUnit_cache_put(self, "last_world_health", text_result);
            MemUnit_record_execution_route(self, world_name, action, "", 0);
        }
        return r;
    }

    if (binding->exec_text_fn) {
        text_result = binding->exec_text_fn(binding->world_state, action, text_payload ? text_payload : "", number_arg);
        if (!text_result) {
            r.status = -10;
            r.error = strdup("exec_failed");
        } else {
            r.status = 0;
            r.value = strdup(text_result);
            MemUnit_session_set(self, "last_exec_result", text_result);
            MemUnit_record_execution_route(self, world_name, action, text_payload ? text_payload : "", 0);
        }
        return r;
    }

    r.status = -9;
    r.error = strdup("unknown_action");
    return r;
}

/*##method##[MemUnit|execute_text|world_runtime|compat_surface]*/
/*{[Input:world_name|action|text_payload|number_arg][Output:text][Side_effects:caches_last_runtime_result][Pattern:param_validate_execute]}*/
const char * MemUnit_execute_text(MemUnit_State * self, const char * world_name, const char * action, const char * text_payload, int number_arg) {
    MemUnit_Result r;
    if (!self) return NULL;
    r = MemUnit_execute(self, world_name, action, text_payload, number_arg);
    self->last_result[0] = '\0';
    if (r.value) {
        snprintf(self->last_result, sizeof(self->last_result), "%s", r.value);
    } else if (r.error) {
        snprintf(self->last_result, sizeof(self->last_result), "{[Error:%s]}", r.error);
    }
    free(r.value);
    free(r.error);
    return self->last_result[0] ? self->last_result : NULL;
}

/*##method##[MemUnit|health|status_query]*/
/*{[Input:none][Output:string][Side_effects:counts_all_tables][Pattern:param_validate_execute]}*/
char * MemUnit_health(MemUnit_State * self) {
    static char snapshot[MEMUNIT_MAX_PACKET];
    int verbs = 0, worlds = 0, cache = 0, packets = 0, bus = 0, gui = 0, reports = 0, outputs = 0, sessions = 0, routes = 0;
    sqlite3_stmt * stmt;
    time_t uptime;
    
    if (!self) return NULL;
    
    if (self->memdb.is_open) {
        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM verb_registry", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) verbs = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM worlds", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) worlds = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM cache", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) cache = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM packets", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) packets = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM bus_messages WHERE processed=0", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) bus = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM gui_widgets", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) gui = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM reports WHERE archived=0", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) reports = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM output_queue WHERE delivered=0", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) outputs = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM session_state", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) sessions = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        sqlite3_prepare_v2(self->memdb.db, "SELECT COUNT(*) FROM execution_route", -1, &stmt, NULL);
        if (sqlite3_step(stmt) == SQLITE_ROW) routes = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    
    uptime = self->is_booted ? (time(NULL) - self->boot_time) : 0;
    
    snprintf(snapshot, sizeof(snapshot),
        "{[MemUnit|Health][Version:%s][Booted:%d][Uptime:%ld][Commands:%d][Hits:%d][Misses:%d]"
        "[Verbs:%d][Worlds:%d][Bindings:%d][Cache:%d][Packets:%d][BusQueue:%d][GuiWidgets:%d][Reports:%d][Outputs:%d][Sessions:%d][Routes:%d][OrchCycles:%d]"
        "[Phase:%s][Message:%s][HealthCode:%d]}",
        MEMUNIT_VERSION, self->is_booted, (long)uptime, self->command_count,
        self->cache.total_hits, self->cache.total_misses,
        verbs, worlds, self->binding_count, cache, packets, bus, gui, reports, outputs, sessions, routes,
        self->orchestrator.cycle_count, self->orchestrator.current_phase,
        self->last_message, self->health_code);
    
    return snapshot;
}

/*##method##[MemUnit|cleanup|lifecycle|destruction]*/
/*{[Input:none][Output:none][Side_effects:finalizes_all_statements_closes_database][Pattern:param_validate_execute]}*/
void MemUnit_cleanup(MemUnit_State * self) {
    int index;
    if (!self) return;

    for (index = 0; index < self->binding_count; ++index) {
        if (self->bindings[index].is_bound && self->bindings[index].cleanup_fn && self->bindings[index].world_state) {
            self->bindings[index].cleanup_fn(self->bindings[index].world_state);
            self->bindings[index].world_state = NULL;
        }
        self->bindings[index].is_bound = 0;
    }
    self->binding_count = 0;
    
    if (self->membus.insert_stmt) sqlite3_finalize(self->membus.insert_stmt);
    if (self->membus.select_stmt) sqlite3_finalize(self->membus.select_stmt);
    if (self->membus.mark_processed_stmt) sqlite3_finalize(self->membus.mark_processed_stmt);
    
    if (self->guidb.widget_stmt) sqlite3_finalize(self->guidb.widget_stmt);
    if (self->guidb.layout_stmt) sqlite3_finalize(self->guidb.layout_stmt);
    if (self->guidb.state_stmt) sqlite3_finalize(self->guidb.state_stmt);
    if (self->guidb.action_stmt) sqlite3_finalize(self->guidb.action_stmt);
    
    if (self->guibus.event_insert) sqlite3_finalize(self->guibus.event_insert);
    if (self->guibus.event_select) sqlite3_finalize(self->guibus.event_select);
    if (self->guibus.event_ack) sqlite3_finalize(self->guibus.event_ack);
    
    if (self->orchestrator.world_lookup) sqlite3_finalize(self->orchestrator.world_lookup);
    if (self->orchestrator.dependency_check) sqlite3_finalize(self->orchestrator.dependency_check);
    if (self->orchestrator.route_decision) sqlite3_finalize(self->orchestrator.route_decision);
    
    if (self->report.insert_stmt) sqlite3_finalize(self->report.insert_stmt);
    if (self->report.select_stmt) sqlite3_finalize(self->report.select_stmt);
    if (self->report.archive_stmt) sqlite3_finalize(self->report.archive_stmt);

    if (self->output.insert_stmt) sqlite3_finalize(self->output.insert_stmt);
    if (self->output.select_stmt) sqlite3_finalize(self->output.select_stmt);
    
    if (self->search.by_verb) sqlite3_finalize(self->search.by_verb);
    if (self->search.by_authority) sqlite3_finalize(self->search.by_authority);
    if (self->search.by_magk) sqlite3_finalize(self->search.by_magk);
    if (self->search.by_bracket_sig) sqlite3_finalize(self->search.by_bracket_sig);
    
    if (self->assembly.seed_query) sqlite3_finalize(self->assembly.seed_query);
    if (self->assembly.related_query) sqlite3_finalize(self->assembly.related_query);
    if (self->assembly.store_packet) sqlite3_finalize(self->assembly.store_packet);
    
    if (self->cache.get_stmt) sqlite3_finalize(self->cache.get_stmt);
    if (self->cache.put_stmt) sqlite3_finalize(self->cache.put_stmt);
    if (self->cache.invalidate_stmt) sqlite3_finalize(self->cache.invalidate_stmt);
    if (self->cache.hit_update) sqlite3_finalize(self->cache.hit_update);
    
    if (self->memdb.is_open && self->memdb.db) {
        sqlite3_close(self->memdb.db);
        self->memdb.is_open = 0;
        self->memdb.db = NULL;
    }
    
    self->is_booted = 0;
    self->health_code = 0;
    snprintf(self->last_message, sizeof(self->last_message), "MemUnit cleaned up");
}

/*##method##[MemUnit|destroy|allocation|destructor]*/
/*{[Input:MemUnit_State_ptr][Output:none][Side_effects:cleanup_and_free][Pattern:param_validate_execute]}*/
void MemUnit_destroy(MemUnit_State * self) {
    if (!self) return;
    MemUnit_cleanup(self);
    free(self);
}
