#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>
#include <pthread.h>

#define MAX_PATH 4096

sqlite3 *db;

int open_db() {
    char db_path[MAX_PATH];
    snprintf(db_path, sizeof(db_path), "%s/.cascade_search/app.db", getenv("HOME"));
    
    // Create directory if it doesn't exist
    char dir_path[MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/.cascade_search", getenv("HOME"));
    mkdir(dir_path, 0755);
    
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        printf("DB open failed: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    const char *sql =
        "CREATE TABLE IF NOT EXISTS files ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "path TEXT UNIQUE,"
        "name TEXT,"
        "indexed_at INTEGER"
        ");";
    
    char *err = NULL;
    if (sqlite3_exec(db, sql, 0, 0, &err) != SQLITE_OK) {
        printf("DB init failed: %s\n", err);
        sqlite3_free(err);
        return 0;
    }
    
    // Create config table
    const char *config_sql =
        "CREATE TABLE IF NOT EXISTS config ("
        "key TEXT PRIMARY KEY,"
        "value TEXT,"
        "updated_at INTEGER"
        ");";
    
    if (sqlite3_exec(db, config_sql, 0, 0, &err) != SQLITE_OK) {
        printf("Config table init failed: %s\n", err);
        sqlite3_free(err);
    }
    
    // Create credentials table
    const char *cred_sql =
        "CREATE TABLE IF NOT EXISTS credentials ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT,"
        "password_encrypted TEXT,"
        "service TEXT,"
        "created_at INTEGER"
        ");";
    
    if (sqlite3_exec(db, cred_sql, 0, 0, &err) != SQLITE_OK) {
        printf("Credentials table init failed: %s\n", err);
        sqlite3_free(err);
    }
    
    return 1;
}

void insert_file(const char *path, const char *name) {
    const char *sql =
        "INSERT OR IGNORE INTO files(path, name, indexed_at) "
        "VALUES(?,?,strftime('%s','now'));";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void scan(const char *root) {
    DIR *d = opendir(root);
    if (!d) return;
    
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", root, e->d_name);
        
        struct stat st;
        if (lstat(full, &st) == -1) continue;
        
        if (S_ISDIR(st.st_mode)) {
            scan(full);
        } else if (S_ISREG(st.st_mode)) {
            insert_file(full, e->d_name);
        }
    }
    closedir(d);
}

int search(const char *q) {
    const char *sql =
        "SELECT path FROM files WHERE name LIKE ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%%%s%%", q);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%s\n", sqlite3_column_text(stmt, 0));
        count++;
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int search_content(const char *q) {
    const char *sql =
        "SELECT path FROM files WHERE name LIKE ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%%%s%%", q);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = sqlite3_column_text(stmt, 0);
        
        // Read file and search content
        FILE *f = fopen(path, "r");
        if (f) {
            char line[4096];
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, q)) {
                    printf("%s\n", path);
                    count++;
                    break;
                }
            }
            fclose(f);
        }
    }
    
    sqlite3_finalize(stmt);
    return count;
}

void set_config(const char *key, const char *value) {
    const char *sql =
        "INSERT OR REPLACE INTO config(key, value, updated_at) "
        "VALUES(?, ?, strftime('%s','now'));";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    printf("Config set: %s = %s\n", key, value);
}

void get_config(const char *key) {
    const char *sql = "SELECT value FROM config WHERE key = ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%s = %s\n", key, sqlite3_column_text(stmt, 0));
    } else {
        printf("Config key not found: %s\n", key);
    }
    
    sqlite3_finalize(stmt);
}

void list_config() {
    const char *sql = "SELECT key, value FROM config;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    printf("Configuration:\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  %s = %s\n", sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1));
    }
    
    sqlite3_finalize(stmt);
}

int main(int argc, char **argv) {
    if (!open_db()) return 1;
    
    if (argc < 2) {
        printf("CASCADE Search Tool v2.0 (SQLite)\n");
        printf("usage:\n");
        printf("  cascade /scan <path>           - Index directory\n");
        printf("  cascade /search <query>         - Search filenames\n");
        printf("  cascade /search <query> --content - Search file contents\n");
        printf("  cascade --gui                  - Launch GUI mode\n");
        printf("  cascade --setup                - First-time setup\n");
        printf("  cascade --config               - Configuration mode\n");
        printf("  cascade --config set <key> <value> - Set config value\n");
        printf("  cascade --config get <key>     - Get config value\n");
        printf("  cascade --config list           - List all config\n");
        sqlite3_close(db);
        return 1;
    }
    
    if (strcmp(argv[1], "/scan") == 0 && argc > 2) {
        printf("Scanning %s...\n", argv[2]);
        scan(argv[2]);
        printf("Scan complete\n");
    }
    else if (strcmp(argv[1], "/search") == 0 && argc > 2) {
        int content_search = (argc > 3 && strcmp(argv[3], "--content") == 0);
        printf("Searching for: %s%s\n", argv[2], content_search ? " (content)" : "");
        int count = content_search ? search_content(argv[2]) : search(argv[2]);
        printf("Found %d results\n", count);
    }
    else if (strcmp(argv[1], "--gui") == 0) {
        printf("GUI mode placeholder (PyQt6 integration coming soon)\n");
        printf("Run: python3 cascade_gui.py\n");
    }
    else if (strcmp(argv[1], "--setup") == 0) {
        printf("Setup mode\n");
        printf("Database location: %s/.cascade_search/app.db\n", getenv("HOME"));
        printf("Tables created: files, config, credentials\n");
        printf("Ready for use\n");
    }
    else if (strcmp(argv[1], "--config") == 0) {
        if (argc > 3 && strcmp(argv[2], "set") == 0 && argc > 4) {
            set_config(argv[3], argv[4]);
        } else if (argc > 3 && strcmp(argv[2], "get") == 0 && argc > 3) {
            get_config(argv[3]);
        } else if (argc > 2 && strcmp(argv[2], "list") == 0) {
            list_config();
        } else {
            printf("Config mode usage:\n");
            printf("  cascade --config set <key> <value>\n");
            printf("  cascade --config get <key>\n");
            printf("  cascade --config list\n");
        }
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        printf("Run with no arguments for usage\n");
    }
    
    sqlite3_close(db);
    return 0;
}
