/*
#   Ghost[World:LMDBWorld Domain:LMDB_Runtime_Store Purpose:Persistent_KeyValue_Storage_World
#   Owns:lmdb_env|dbi|persistent_kv_state Accepts:config_path|action|payload Returns:value_text|status|health_snapshot
#   Requires:LMDB_library Exposes:create|boot|execute_text|health|cleanup|destroy
#   State:path|env|dbi|map_size|booted|last_key|last_value|last_message Health:lmdb_status_snapshot Tags:world,lmdb,persistence,storage,kv ]
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lmdb.h"

#define LMDBWORLD_PATH 1024
#define LMDBWORLD_TEXT 8192

typedef struct LMDBWorld_State {
    char env_path[LMDBWORLD_PATH];
    char last_key[256];
    char last_message[256];
    char last_value[LMDBWORLD_TEXT];
    MDB_env * env;
    MDB_dbi dbi;
    size_t map_size;
    int is_booted;
    int health_code;
} LMDBWorld_State;

static void LMDBWorld_set_status(LMDBWorld_State * self, int code, const char * message) {
    if (!self) {
        return;
    }
    self->health_code = code;
    if (message) {
        snprintf(self->last_message, sizeof(self->last_message), "%s", message);
    }
}

static int LMDBWorld_ensure_dir(const char * path) {
    if (!path || path[0] == '\0') {
        return -1;
    }
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static size_t LMDBWorld_parse_map_size(const char * config) {
    const char * found;
    if (!config) {
        return (size_t)64 * 1024 * 1024;
    }
    found = strstr(config, "map_size=");
    if (!found) {
        return (size_t)64 * 1024 * 1024;
    }
    return (size_t)strtoull(found + 9, NULL, 10);
}

static void LMDBWorld_parse_path(const char * config, char * out_path, size_t out_size) {
    const char * found;
    const char * end;
    const char * env_path = getenv("GGUF_CONTEXT_LMDB_PATH");
    if (!out_path || out_size == 0) {
        return;
    }
    if (env_path && env_path[0] != '\0') {
        snprintf(out_path, out_size, "%s", env_path);
        return;
    }
    if (config) {
        found = strstr(config, "path=");
        if (found) {
            found += 5;
            end = strchr(found, ';');
            if (!end) {
                snprintf(out_path, out_size, "%s", found);
                return;
            }
            snprintf(out_path, out_size, "%.*s", (int)(end - found), found);
            return;
        }
    }
    snprintf(out_path, out_size, "%s", "/Users/waynephilliplundall/testbed/GGUF_Context_Fix/runtime_lmdb");
}

LMDBWorld_State * LMDBWorld_create(void) {
    return (LMDBWorld_State *)calloc(1, sizeof(LMDBWorld_State));
}

int LMDBWorld_boot(LMDBWorld_State * self, const char * config) {
    MDB_txn * txn = NULL;
    int rc;

    if (!self) {
        return -1;
    }
    memset(self, 0, sizeof(*self));
    LMDBWorld_parse_path(config, self->env_path, sizeof(self->env_path));
    self->map_size = LMDBWorld_parse_map_size(config);
    if (self->map_size == 0) {
        self->map_size = (size_t)64 * 1024 * 1024;
    }

    if (LMDBWorld_ensure_dir(self->env_path) != 0) {
        LMDBWorld_set_status(self, 0, "failed to create lmdb directory");
        return -1;
    }

    rc = mdb_env_create(&self->env);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_env_create failed");
        return -1;
    }
    mdb_env_set_maxdbs(self->env, 4);
    mdb_env_set_mapsize(self->env, self->map_size);
    rc = mdb_env_open(self->env, self->env_path, 0, 0664);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_env_open failed");
        mdb_env_close(self->env);
        self->env = NULL;
        return -1;
    }

    rc = mdb_txn_begin(self->env, NULL, 0, &txn);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_txn_begin failed");
        mdb_env_close(self->env);
        self->env = NULL;
        return -1;
    }
    rc = mdb_dbi_open(txn, "runtime_kv", MDB_CREATE, &self->dbi);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        LMDBWorld_set_status(self, 0, "mdb_dbi_open failed");
        mdb_env_close(self->env);
        self->env = NULL;
        return -1;
    }
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_txn_commit failed");
        mdb_env_close(self->env);
        self->env = NULL;
        return -1;
    }

    self->is_booted = 1;
    LMDBWorld_set_status(self, 100, "LMDBWorld booted");
    return 0;
}

static const char * LMDBWorld_put(LMDBWorld_State * self, const char * key, const char * value) {
    MDB_txn * txn = NULL;
    MDB_val mkey;
    MDB_val mval;
    int rc;

    if (!self || !self->is_booted || !key || !value) {
        return NULL;
    }
    rc = mdb_txn_begin(self->env, NULL, 0, &txn);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_txn_begin put failed");
        return NULL;
    }
    mkey.mv_data = (void *)key;
    mkey.mv_size = strlen(key);
    mval.mv_data = (void *)value;
    mval.mv_size = strlen(value);
    rc = mdb_put(txn, self->dbi, &mkey, &mval, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        LMDBWorld_set_status(self, 0, "mdb_put failed");
        return NULL;
    }
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_txn_commit put failed");
        return NULL;
    }
    snprintf(self->last_key, sizeof(self->last_key), "%s", key);
    snprintf(self->last_value, sizeof(self->last_value), "%s", value);
    LMDBWorld_set_status(self, 100, "put ok");
    return "stored";
}

static const char * LMDBWorld_get(LMDBWorld_State * self, const char * key) {
    MDB_txn * txn = NULL;
    MDB_val mkey;
    MDB_val mval;
    int rc;

    if (!self || !self->is_booted || !key) {
        return NULL;
    }
    rc = mdb_txn_begin(self->env, NULL, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_txn_begin get failed");
        return NULL;
    }
    mkey.mv_data = (void *)key;
    mkey.mv_size = strlen(key);
    rc = mdb_get(txn, self->dbi, &mkey, &mval);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        LMDBWorld_set_status(self, 0, rc == MDB_NOTFOUND ? "not found" : "mdb_get failed");
        return NULL;
    }
    snprintf(self->last_key, sizeof(self->last_key), "%s", key);
    snprintf(self->last_value, sizeof(self->last_value), "%.*s", (int)mval.mv_size, (const char *)mval.mv_data);
    mdb_txn_abort(txn);
    LMDBWorld_set_status(self, 100, "get ok");
    return self->last_value;
}

static const char * LMDBWorld_list_prefix(LMDBWorld_State * self, const char * prefix) {
    MDB_txn * txn = NULL;
    MDB_cursor * cursor = NULL;
    MDB_val mkey;
    MDB_val mval;
    int rc;
    size_t used = 0;
    size_t prefix_len;

    if (!self || !self->is_booted || !prefix) {
        return NULL;
    }
    self->last_value[0] = '\0';
    prefix_len = strlen(prefix);
    rc = mdb_txn_begin(self->env, NULL, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) {
        LMDBWorld_set_status(self, 0, "mdb_txn_begin list failed");
        return NULL;
    }
    rc = mdb_cursor_open(txn, self->dbi, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        LMDBWorld_set_status(self, 0, "mdb_cursor_open failed");
        return NULL;
    }
    rc = mdb_cursor_get(cursor, &mkey, &mval, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        const char * key_text = (const char *)mkey.mv_data;
        if (mkey.mv_size >= prefix_len && strncmp(key_text, prefix, prefix_len) == 0) {
            int written = snprintf(
                self->last_value + used,
                sizeof(self->last_value) - used,
                "%s%.*s\n",
                used > 0 ? "" : "",
                (int)mkey.mv_size,
                key_text
            );
            if (written < 0 || (size_t)written >= sizeof(self->last_value) - used) {
                break;
            }
            used += (size_t)written;
        }
        rc = mdb_cursor_get(cursor, &mkey, &mval, MDB_NEXT);
    }
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    snprintf(self->last_key, sizeof(self->last_key), "%s", prefix);
    LMDBWorld_set_status(self, 100, used > 0 ? "list ok" : "list empty");
    return self->last_value;
}

const char * LMDBWorld_execute_text(LMDBWorld_State * self, const char * action, const char * payload, int number_arg) {
    char key[256];
    char value[LMDBWORLD_TEXT];
    const char * split;
    (void)number_arg;

    if (!self || !action) {
        return NULL;
    }
    if (strcmp(action, "put") == 0) {
        if (!payload) {
            LMDBWorld_set_status(self, 0, "missing payload");
            return NULL;
        }
        split = strchr(payload, '\t');
        if (!split) {
            split = strchr(payload, '=');
        }
        if (!split) {
            LMDBWorld_set_status(self, 0, "put payload needs key<tab>value");
            return NULL;
        }
        snprintf(key, sizeof(key), "%.*s", (int)(split - payload), payload);
        snprintf(value, sizeof(value), "%s", split + 1);
        return LMDBWorld_put(self, key, value);
    }
    if (strcmp(action, "get") == 0) {
        return LMDBWorld_get(self, payload ? payload : "");
    }
    if (strcmp(action, "list") == 0) {
        return LMDBWorld_list_prefix(self, payload ? payload : "");
    }
    if (strcmp(action, "health") == 0) {
        return NULL;
    }
    LMDBWorld_set_status(self, 0, "unknown action");
    return NULL;
}

const char * LMDBWorld_health(LMDBWorld_State * self) {
    static char snapshot[1024];
    if (!self) {
        return "LMDBWorld missing";
    }
    snprintf(
        snapshot,
        sizeof(snapshot),
        "{"
        "\"health_code\":%d,"
        "\"message\":\"%s\","
        "\"env_path\":\"%s\","
        "\"booted\":%d,"
        "\"map_size\":%llu,"
        "\"last_key\":\"%s\""
        "}",
        self->health_code,
        self->last_message,
        self->env_path,
        self->is_booted,
        (unsigned long long)self->map_size,
        self->last_key
    );
    return snapshot;
}

void LMDBWorld_cleanup(LMDBWorld_State * self) {
    if (!self) {
        return;
    }
    if (self->env) {
        mdb_dbi_close(self->env, self->dbi);
        mdb_env_close(self->env);
        self->env = NULL;
    }
    self->is_booted = 0;
}

void LMDBWorld_destroy(LMDBWorld_State * self) {
    if (!self) {
        return;
    }
    LMDBWorld_cleanup(self);
    free(self);
}
