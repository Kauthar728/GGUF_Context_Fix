#ifndef CASCADE_BACKEND_ENGINE_H
#define CASCADE_BACKEND_ENGINE_H

#include <sqlite3.h>
#include <glib.h>
#include "CASCADE_Types.h"

#define MAX_PATH 4096

/* Scan result structure */
typedef struct {
    int files_indexed;
    int directories_seen;
    int directories_skipped;
    char error[512];
} ScanResult;

/* Search result structure */
typedef struct {
    char *path;
    char *name;
    long long size;
    long long mtime;
} SearchResult;

/* Search results collection */
typedef struct {
    SearchResult *results;
    int count;
    int total;
} SearchResults;

/* Backend engine structure */
typedef struct {
    sqlite3 *db;
    GMutex lock;
    char db_path[MAX_PATH];
} BackendEngine;

/* Function signatures */
ResultT backend_create(const char *db_path);
ResultT backend_open(BackendEngine *engine);
ResultT backend_close(BackendEngine *engine);
ResultT backend_destroy(BackendEngine *engine);

ResultT backend_scan_path(BackendEngine *engine, const char *path, ScanResult *result);
ResultT backend_search(BackendEngine *engine, const char *query, int limit, SearchResults *results);
ResultT backend_search_free(SearchResults *results);

ResultT backend_config_set(BackendEngine *engine, const char *key, const char *value);
ResultT backend_config_get(BackendEngine *engine, const char *key, char **value);
ResultT backend_config_list(BackendEngine *engine, GHashTable **config);

ResultT backend_file_count(BackendEngine *engine, int *count);
ResultT backend_last_scan_time(BackendEngine *engine, long long *timestamp);

#endif /* CASCADE_BACKEND_ENGINE_H */
