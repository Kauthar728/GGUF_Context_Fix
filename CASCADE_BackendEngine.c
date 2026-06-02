#include "CASCADE_BackendEngine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/* Skip directories */
static int should_skip_dir(const char *name) {
    static const char *skip[] = {
        ".git", ".hg", ".svn", "node_modules", ".venv", "venv", "env",
        "__pycache__", "site-packages", "target", "build", "dist",
        ".cache", ".Trash", "Library/Caches"
    };
    for (size_t i = 0; i < sizeof(skip)/sizeof(skip[0]); i++) {
        if (strcmp(name, skip[i]) == 0) return 1;
    }
    return 0;
}

/* backend_create: Create backend engine */
ResultT backend_create(const char *db_path) {
    BackendEngine *engine = calloc(1, sizeof(BackendEngine));
    ResultT result = {0, NULL, NULL};
    
    if (!engine) {
        result.status = 0;
        result.error = strdup("Failed to allocate BackendEngine");
        return result;
    }
    
    strncpy(engine->db_path, db_path, MAX_PATH - 1);
    g_mutex_init(&engine->lock);
    
    result.status = 1;
    result.data = engine;
    return result;
}

/* backend_open: Open database */
ResultT backend_open(BackendEngine *engine) {
    ResultT result = {0, NULL, NULL};
    
    if (!engine) {
        result.status = 0;
        result.error = strdup("Invalid BackendEngine");
        return result;
    }
    
    if (sqlite3_open(engine->db_path, &engine->db) != SQLITE_OK) {
        result.status = 0;
        result.error = strdup(sqlite3_errmsg(engine->db));
        return result;
    }
    
    sqlite3_busy_timeout(engine->db, 5000);
    
    g_mutex_lock(&engine->lock);
    
    char *err = NULL;
    if (sqlite3_exec(engine->db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err) != SQLITE_OK) {
        result.status = 0;
        result.error = strdup(err ? err : sqlite3_errmsg(engine->db));
        sqlite3_free(err);
        g_mutex_unlock(&engine->lock);
        return result;
    }
    
    if (sqlite3_exec(engine->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, &err) != SQLITE_OK) {
        result.status = 0;
        result.error = strdup(err ? err : sqlite3_errmsg(engine->db));
        sqlite3_free(err);
        g_mutex_unlock(&engine->lock);
        return result;
    }
    
    const char *files_sql =
        "CREATE TABLE IF NOT EXISTS files ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "path TEXT UNIQUE NOT NULL,"
        "name TEXT NOT NULL,"
        "size INTEGER DEFAULT 0,"
        "mtime INTEGER DEFAULT 0,"
        "indexed_at INTEGER NOT NULL"
        ");";
    
    if (sqlite3_exec(engine->db, files_sql, NULL, NULL, &err) != SQLITE_OK) {
        result.status = 0;
        result.error = strdup(err ? err : sqlite3_errmsg(engine->db));
        sqlite3_free(err);
        g_mutex_unlock(&engine->lock);
        return result;
    }
    
    sqlite3_exec(engine->db, "CREATE INDEX IF NOT EXISTS idx_files_name ON files(name);", NULL, NULL, &err);
    sqlite3_free(err);
    
    const char *config_sql =
        "CREATE TABLE IF NOT EXISTS config ("
        "key TEXT PRIMARY KEY,"
        "value TEXT,"
        "updated_at INTEGER"
        ");";
    
    if (sqlite3_exec(engine->db, config_sql, NULL, NULL, &err) != SQLITE_OK) {
        result.status = 0;
        result.error = strdup(err ? err : sqlite3_errmsg(engine->db));
        sqlite3_free(err);
    } else {
        result.status = 1;
    }
    
    g_mutex_unlock(&engine->lock);
    return result;
}

/* backend_close: Close database */
ResultT backend_close(BackendEngine *engine) {
    ResultT result = {0, NULL, NULL};
    
    if (!engine) {
        result.status = 0;
        result.error = strdup("Invalid BackendEngine");
        return result;
    }
    
    if (engine->db) {
        sqlite3_close(engine->db);
        engine->db = NULL;
    }
    
    result.status = 1;
    return result;
}

/* backend_destroy: Destroy backend engine */
ResultT backend_destroy(BackendEngine *engine) {
    ResultT result = {0, NULL, NULL};
    
    if (!engine) {
        result.status = 0;
        result.error = strdup("Invalid BackendEngine");
        return result;
    }
    
    g_mutex_clear(&engine->lock);
    free(engine);
    
    result.status = 1;
    return result;
}

/* backend_scan_path: Scan directory */
ResultT backend_scan_path(BackendEngine *engine, const char *path, ScanResult *result) {
    ResultT r = {0, NULL, NULL};
    
    if (!engine || !path || !result) {
        r.status = 0;
        r.error = strdup("Invalid parameters");
        return r;
    }
    
    memset(result, 0, sizeof(ScanResult));
    
    struct stat st;
    if (lstat(path, &st) == -1) {
        snprintf(result->error, sizeof(result->error), "%s: %s", path, strerror(errno));
        r.status = 0;
        r.error = strdup(result->error);
        return r;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        snprintf(result->error, sizeof(result->error), "%s is not a directory", path);
        r.status = 0;
        r.error = strdup(result->error);
        return r;
    }
    
    g_mutex_lock(&engine->lock);
    
    char *err = NULL;
    if (sqlite3_exec(engine->db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, &err) != SQLITE_OK) {
        snprintf(result->error, sizeof(result->error), "Transaction failed: %s", err ? err : sqlite3_errmsg(engine->db));
        sqlite3_free(err);
        g_mutex_unlock(&engine->lock);
        r.status = 0;
        r.error = strdup(result->error);
        return r;
    }
    
    DIR *d = opendir(path);
    if (!d) {
        snprintf(result->error, sizeof(result->error), "Cannot open %s: %s", path, strerror(errno));
        sqlite3_exec(engine->db, "ROLLBACK;", NULL, NULL, NULL);
        g_mutex_unlock(&engine->lock);
        r.status = 0;
        r.error = strdup(result->error);
        return r;
    }
    
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        
        struct stat fst;
        if (lstat(full, &fst) == -1) continue;
        
        if (S_ISDIR(fst.st_mode)) {
            if (should_skip_dir(e->d_name)) {
                result->directories_skipped++;
                continue;
            }
            result->directories_seen++;
            ScanResult sub_result = {0};
            ResultT sub_r = backend_scan_path(engine, full, &sub_result);
            result->files_indexed += sub_result.files_indexed;
            result->directories_seen += sub_result.directories_seen;
            result->directories_skipped += sub_result.directories_skipped;
            if (sub_r.error) free(sub_r.error);
        } else if (S_ISREG(fst.st_mode)) {
            const char *sql = "INSERT OR REPLACE INTO files(path,name,size,mtime,indexed_at) VALUES(?,?,?,?,strftime('%s','now'))";
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, full, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, e->d_name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 3, fst.st_size);
                sqlite3_bind_int64(stmt, 4, fst.st_mtime);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    result->files_indexed++;
                }
                sqlite3_finalize(stmt);
            }
        }
    }
    
    closedir(d);
    
    if (sqlite3_exec(engine->db, "COMMIT;", NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_exec(engine->db, "ROLLBACK;", NULL, NULL, NULL);
        snprintf(result->error, sizeof(result->error), "Commit failed: %s", err ? err : sqlite3_errmsg(engine->db));
        sqlite3_free(err);
        g_mutex_unlock(&engine->lock);
        r.status = 0;
        r.error = strdup(result->error);
        return r;
    }
    
    g_mutex_unlock(&engine->lock);
    r.status = 1;
    return r;
}

/* backend_search: Search files */
ResultT backend_search(BackendEngine *engine, const char *query, int limit, SearchResults *results) {
    ResultT r = {0, NULL, NULL};
    
    if (!engine || !query || !results) {
        r.status = 0;
        r.error = strdup("Invalid parameters");
        return r;
    }
    
    memset(results, 0, sizeof(SearchResults));
    
    const char *sql = "SELECT path,name,size,mtime FROM files WHERE name LIKE ? ESCAPE '\\' ORDER BY name COLLATE NOCASE, path LIMIT ?";
    sqlite3_stmt *stmt;
    
    g_mutex_lock(&engine->lock);
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        r.status = 0;
        r.error = strdup(sqlite3_errmsg(engine->db));
        g_mutex_unlock(&engine->lock);
        return r;
    }
    
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%%%s%%", query);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }
    
    results->results = calloc(count, sizeof(SearchResult));
    results->total = count;
    
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    
    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        const char *path = (const char*)sqlite3_column_text(stmt, 0);
        const char *name = (const char*)sqlite3_column_text(stmt, 1);
        results->results[idx].path = strdup(path ? path : "");
        results->results[idx].name = strdup(name ? name : "");
        results->results[idx].size = sqlite3_column_int64(stmt, 2);
        results->results[idx].mtime = sqlite3_column_int64(stmt, 3);
        idx++;
    }
    
    results->count = idx;
    sqlite3_finalize(stmt);
    g_mutex_unlock(&engine->lock);
    
    r.status = 1;
    return r;
}

/* backend_search_free: Free search results */
ResultT backend_search_free(SearchResults *results) {
    ResultT r = {0, NULL, NULL};
    
    if (!results) {
        r.status = 0;
        r.error = strdup("Invalid SearchResults");
        return r;
    }
    
    for (int i = 0; i < results->count; i++) {
        free(results->results[i].path);
        free(results->results[i].name);
    }
    free(results->results);
    memset(results, 0, sizeof(SearchResults));
    
    r.status = 1;
    return r;
}

/* backend_config_set: Set config value */
ResultT backend_config_set(BackendEngine *engine, const char *key, const char *value) {
    ResultT r = {0, NULL, NULL};
    
    if (!engine || !key) {
        r.status = 0;
        r.error = strdup("Invalid parameters");
        return r;
    }
    
    const char *sql = "INSERT OR REPLACE INTO config(key,value,updated_at) VALUES(?,?,strftime('%s','now'))";
    sqlite3_stmt *stmt;
    
    g_mutex_lock(&engine->lock);
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        r.status = 0;
        r.error = strdup(sqlite3_errmsg(engine->db));
        g_mutex_unlock(&engine->lock);
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value ? value : "", -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        r.status = 1;
    } else {
        r.status = 0;
        r.error = strdup(sqlite3_errmsg(engine->db));
    }
    
    sqlite3_finalize(stmt);
    g_mutex_unlock(&engine->lock);
    return r;
}

/* backend_config_get: Get config value */
ResultT backend_config_get(BackendEngine *engine, const char *key, char **value) {
    ResultT r = {0, NULL, NULL};
    
    if (!engine || !key || !value) {
        r.status = 0;
        r.error = strdup("Invalid parameters");
        return r;
    }
    
    const char *sql = "SELECT value FROM config WHERE key = ?";
    sqlite3_stmt *stmt;
    
    g_mutex_lock(&engine->lock);
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        r.status = 0;
        r.error = strdup(sqlite3_errmsg(engine->db));
        g_mutex_unlock(&engine->lock);
        return r;
    }
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *val = (const char*)sqlite3_column_text(stmt, 0);
        *value = strdup(val ? val : "");
        r.status = 1;
    } else {
        *value = strdup("");
        r.status = 1;
    }
    
    sqlite3_finalize(stmt);
    g_mutex_unlock(&engine->lock);
    return r;
}

/* backend_file_count: Get file count */
ResultT backend_file_count(BackendEngine *engine, int *count) {
    ResultT r = {0, NULL, NULL};
    
    if (!engine || !count) {
        r.status = 0;
        r.error = strdup("Invalid parameters");
        return r;
    }
    
    const char *sql = "SELECT COUNT(*) FROM files";
    sqlite3_stmt *stmt;
    
    g_mutex_lock(&engine->lock);
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        r.status = 0;
        r.error = strdup(sqlite3_errmsg(engine->db));
        g_mutex_unlock(&engine->lock);
        return r;
    }
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *count = sqlite3_column_int(stmt, 0);
        r.status = 1;
    } else {
        r.status = 0;
        r.error = strdup("Query failed");
    }
    
    sqlite3_finalize(stmt);
    g_mutex_unlock(&engine->lock);
    return r;
}

/* backend_last_scan_time: Get last scan time */
ResultT backend_last_scan_time(BackendEngine *engine, long long *timestamp) {
    ResultT r = {0, NULL, NULL};
    
    if (!engine || !timestamp) {
        r.status = 0;
        r.error = strdup("Invalid parameters");
        return r;
    }
    
    const char *sql = "SELECT MAX(indexed_at) FROM files";
    sqlite3_stmt *stmt;
    
    g_mutex_lock(&engine->lock);
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        r.status = 0;
        r.error = strdup(sqlite3_errmsg(engine->db));
        g_mutex_unlock(&engine->lock);
        return r;
    }
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *timestamp = sqlite3_column_int64(stmt, 0);
        r.status = 1;
    } else {
        *timestamp = 0;
        r.status = 1;
    }
    
    sqlite3_finalize(stmt);
    g_mutex_unlock(&engine->lock);
    return r;
}
