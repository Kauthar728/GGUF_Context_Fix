#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>
#include <gtk/gtk.h>

#define MAX_PATH 4096
#define RESULT_LIMIT 2000

static sqlite3 *db = NULL;
static GMutex db_lock;
static int gui_mode = 0;

typedef struct {
    GtkWidget *window;
    GtkWidget *search_entry;
    GtkWidget *path_entry;
    GtkWidget *results_text;
    GtkWidget *status_label;
    GtkWidget *search_button;
    GtkWidget *scan_button;
    GtkWidget *progress_bar;
    guint progress_timer_id;
    gboolean scan_running;
} AppWidgets;

typedef struct {
    AppWidgets *app;
    char *path;
    int files_indexed;
    int directories_seen;
    int directories_skipped;
    char error[512];
} ScanJob;

static const char *home_dir(void) {
    const char *home = getenv("HOME");
    return (home && home[0]) ? home : ".";
}

static int path_join(char *out, size_t out_len, const char *left, const char *right) {
    int written = snprintf(out, out_len, "%s/%s", left, right);
    return written > 0 && (size_t)written < out_len;
}

static int exec_sql(const char *sql) {
    char *err = NULL;
    int rc;

    g_mutex_lock(&db_lock);
    rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    g_mutex_unlock(&db_lock);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLite error: %s\n", err ? err : sqlite3_errmsg(db));
        sqlite3_free(err);
        return 0;
    }

    return 1;
}

static int table_has_column(const char *table, const char *column) {
    sqlite3_stmt *stmt = NULL;
    char sql[256];
    int found = 0;

    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);

    g_mutex_lock(&db_lock);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Schema check failed: %s\n", sqlite3_errmsg(db));
        g_mutex_unlock(&db_lock);
        return 0;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (name && strcmp((const char *)name, column) == 0) {
            found = 1;
            break;
        }
    }

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_lock);
    return found;
}

static int ensure_column(const char *table, const char *column, const char *definition) {
    char sql[512];

    if (table_has_column(table, column)) {
        return 1;
    }

    snprintf(sql, sizeof(sql), "ALTER TABLE %s ADD COLUMN %s %s;", table, column, definition);
    return exec_sql(sql);
}

static int open_db(void) {
    char db_path[MAX_PATH];
    char dir_path[MAX_PATH];
    const char *home = home_dir();

    snprintf(dir_path, sizeof(dir_path), "%s/.cascade_search", home);
    if (mkdir(dir_path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Could not create %s: %s\n", dir_path, strerror(errno));
        return 0;
    }

    snprintf(db_path, sizeof(db_path), "%s/app.db", dir_path);
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "DB open failed: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_busy_timeout(db, 5000);

    if (!exec_sql("PRAGMA journal_mode=WAL;")) return 0;
    if (!exec_sql("PRAGMA synchronous=NORMAL;")) return 0;
    if (!exec_sql(
            "CREATE TABLE IF NOT EXISTS files ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "path TEXT UNIQUE NOT NULL,"
            "name TEXT NOT NULL,"
            "size INTEGER DEFAULT 0,"
            "mtime INTEGER DEFAULT 0,"
            "indexed_at INTEGER NOT NULL"
            ");")) {
        return 0;
    }
    if (!ensure_column("files", "size", "INTEGER DEFAULT 0")) return 0;
    if (!ensure_column("files", "mtime", "INTEGER DEFAULT 0")) return 0;
    if (!exec_sql("CREATE INDEX IF NOT EXISTS idx_files_name ON files(name);")) return 0;
    if (!exec_sql(
            "CREATE TABLE IF NOT EXISTS config ("
            "key TEXT PRIMARY KEY,"
            "value TEXT,"
            "updated_at INTEGER"
            ");")) {
        return 0;
    }

    return 1;
}

static void close_db(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

static int should_skip_dir_name(const char *name) {
    static const char *skip_dirs[] = {
        ".git", ".hg", ".svn", "node_modules", ".venv", "venv", "env",
        "__pycache__", "site-packages", "target", "build", "dist",
        ".cache", ".Trash", "Library/Caches"
    };
    size_t count = sizeof(skip_dirs) / sizeof(skip_dirs[0]);

    for (size_t i = 0; i < count; i++) {
        if (strcmp(name, skip_dirs[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static int insert_file(const char *path, const char *name, const struct stat *st) {
    const char *sql =
        "INSERT INTO files(path, name, size, mtime, indexed_at) "
        "VALUES(?,?,?,?,strftime('%s','now')) "
        "ON CONFLICT(path) DO UPDATE SET "
        "name=excluded.name,"
        "size=excluded.size,"
        "mtime=excluded.mtime,"
        "indexed_at=excluded.indexed_at;";
    sqlite3_stmt *stmt = NULL;
    int ok = 0;

    g_mutex_lock(&db_lock);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)st->st_size);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)st->st_mtime);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }

    if (!ok) {
        fprintf(stderr, "Insert failed for %s: %s\n", path, sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_lock);
    return ok;
}

static void scan_dir(const char *root, ScanJob *job) {
    DIR *d = opendir(root);
    struct dirent *e;

    if (!d) {
        return;
    }

    job->directories_seen++;

    while ((e = readdir(d)) != NULL) {
        char full[MAX_PATH];
        struct stat st;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }

        if (!path_join(full, sizeof(full), root, e->d_name)) {
            continue;
        }

        if (lstat(full, &st) == -1) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (should_skip_dir_name(e->d_name)) {
                job->directories_skipped++;
                continue;
            }
            scan_dir(full, job);
        } else if (S_ISREG(st.st_mode)) {
            if (insert_file(full, e->d_name, &st)) {
                job->files_indexed++;
            }
        }
    }

    closedir(d);
}

static int scan_path(const char *root, ScanJob *job) {
    struct stat st;

    if (!root || !root[0]) {
        snprintf(job->error, sizeof(job->error), "No directory path supplied");
        return 0;
    }

    if (lstat(root, &st) == -1) {
        snprintf(job->error, sizeof(job->error), "%s: %s", root, strerror(errno));
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        snprintf(job->error, sizeof(job->error), "%s is not a directory", root);
        return 0;
    }

    if (!exec_sql("BEGIN IMMEDIATE TRANSACTION;")) {
        snprintf(job->error, sizeof(job->error), "Could not start SQLite transaction");
        return 0;
    }

    scan_dir(root, job);

    if (!exec_sql("COMMIT;")) {
        exec_sql("ROLLBACK;");
        snprintf(job->error, sizeof(job->error), "Could not commit SQLite transaction");
        return 0;
    }

    return 1;
}

static int search_to_string(const char *q, GString *results, int limit) {
    const char *sql =
        "SELECT path, size, mtime FROM files "
        "WHERE name LIKE ? ESCAPE '\\' "
        "ORDER BY name COLLATE NOCASE, path "
        "LIMIT ?;";
    sqlite3_stmt *stmt = NULL;
    char pattern[1024];
    int count = 0;

    if (!q || !q[0]) {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "%%%s%%", q);

    g_mutex_lock(&db_lock);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Search prepare failed: %s\n", sqlite3_errmsg(db));
        g_mutex_unlock(&db_lock);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *path = sqlite3_column_text(stmt, 0);
        sqlite3_int64 size = sqlite3_column_int64(stmt, 1);
        if (path) {
            g_string_append_printf(results, "%s  (%lld bytes)\n", path, (long long)size);
            count++;
        }
    }

    sqlite3_finalize(stmt);
    g_mutex_unlock(&db_lock);
    return count;
}

static int search_to_stdout(const char *q) {
    GString *results = g_string_new(NULL);
    int count = search_to_string(q, results, 100000);

    fputs(results->str, stdout);
    g_string_free(results, TRUE);
    return count;
}

static gboolean pulse_progress(gpointer data) {
    AppWidgets *app = (AppWidgets *)data;

    if (!app->scan_running) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 0.0);
        app->progress_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->progress_bar));
    return G_SOURCE_CONTINUE;
}

static gboolean finish_scan_on_ui(gpointer data) {
    ScanJob *job = (ScanJob *)data;
    AppWidgets *app = job->app;
    char status[512];

    app->scan_running = FALSE;
    gtk_widget_set_sensitive(app->scan_button, TRUE);
    gtk_widget_set_sensitive(app->search_button, TRUE);

    if (job->error[0]) {
        snprintf(status, sizeof(status), "Scan failed: %s", job->error);
    } else {
        snprintf(status, sizeof(status),
                 "Scan complete: %d files, %d directories, %d skipped",
                 job->files_indexed, job->directories_seen, job->directories_skipped);
    }

    gtk_label_set_text(GTK_LABEL(app->status_label), status);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), NULL);

    g_free(job->path);
    g_free(job);
    return G_SOURCE_REMOVE;
}

static gpointer scan_worker(gpointer data) {
    ScanJob *job = (ScanJob *)data;

    if (!scan_path(job->path, job) && !job->error[0]) {
        snprintf(job->error, sizeof(job->error), "Unknown scan error");
    }

    g_idle_add(finish_scan_on_ui, job);
    return NULL;
}

static void on_search_clicked(GtkWidget *widget, gpointer data) {
    AppWidgets *app = (AppWidgets *)data;
    const char *query = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
    GString *results;
    GtkTextBuffer *buffer;
    char status[256];
    int count;

    (void)widget;

    if (!query || !query[0]) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Please enter a search query");
        return;
    }

    results = g_string_new(NULL);
    count = search_to_string(query, results, RESULT_LIMIT);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->results_text));
    gtk_text_buffer_set_text(buffer, results->str, -1);

    snprintf(status, sizeof(status), "Found %d results%s", count,
             count == RESULT_LIMIT ? " (limited)" : "");
    gtk_label_set_text(GTK_LABEL(app->status_label), status);
    g_string_free(results, TRUE);
}

static void on_scan_clicked(GtkWidget *widget, gpointer data) {
    AppWidgets *app = (AppWidgets *)data;
    const char *path = gtk_entry_get_text(GTK_ENTRY(app->path_entry));
    ScanJob *job;

    (void)widget;

    if (!path || !path[0]) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Please enter a directory path");
        return;
    }

    if (app->scan_running) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Scan already running");
        return;
    }

    job = g_new0(ScanJob, 1);
    job->app = app;
    job->path = g_strdup(path);

    app->scan_running = TRUE;
    gtk_label_set_text(GTK_LABEL(app->status_label), "Scanning...");
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), "Scanning");
    gtk_widget_set_sensitive(app->scan_button, FALSE);
    gtk_widget_set_sensitive(app->search_button, FALSE);

    if (!app->progress_timer_id) {
        app->progress_timer_id = g_timeout_add(100, pulse_progress, app);
    }

    g_thread_unref(g_thread_new("cascade-scan", scan_worker, job));
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    AppWidgets *widgets = g_new0(AppWidgets, 1);
    GtkWidget *vbox;
    GtkWidget *search_frame;
    GtkWidget *search_box;
    GtkWidget *scan_frame;
    GtkWidget *scan_box;
    GtkWidget *results_frame;
    GtkWidget *scrolled;

    (void)user_data;

    widgets->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(widgets->window), "CASCADE Search Tool");
    gtk_window_set_default_size(GTK_WINDOW(widgets->window), 900, 650);
    g_object_set_data_full(G_OBJECT(widgets->window), "app-widgets", widgets, g_free);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(widgets->window), vbox);

    search_frame = gtk_frame_new("Search");
    gtk_box_pack_start(GTK_BOX(vbox), search_frame, FALSE, FALSE, 0);

    search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(search_box), 8);
    gtk_container_add(GTK_CONTAINER(search_frame), search_box);

    widgets->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(widgets->search_entry), "Filename contains...");
    gtk_box_pack_start(GTK_BOX(search_box), widgets->search_entry, TRUE, TRUE, 0);

    widgets->search_button = gtk_button_new_with_label("Search");
    g_signal_connect(widgets->search_button, "clicked", G_CALLBACK(on_search_clicked), widgets);
    g_signal_connect(widgets->search_entry, "activate", G_CALLBACK(on_search_clicked), widgets);
    gtk_box_pack_start(GTK_BOX(search_box), widgets->search_button, FALSE, FALSE, 0);

    scan_frame = gtk_frame_new("Scan Directory");
    gtk_box_pack_start(GTK_BOX(vbox), scan_frame, FALSE, FALSE, 0);

    scan_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(scan_box), 8);
    gtk_container_add(GTK_CONTAINER(scan_frame), scan_box);

    widgets->path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(widgets->path_entry), home_dir());
    gtk_box_pack_start(GTK_BOX(scan_box), widgets->path_entry, TRUE, TRUE, 0);

    widgets->scan_button = gtk_button_new_with_label("Scan");
    g_signal_connect(widgets->scan_button, "clicked", G_CALLBACK(on_scan_clicked), widgets);
    gtk_box_pack_start(GTK_BOX(scan_box), widgets->scan_button, FALSE, FALSE, 0);

    widgets->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(widgets->progress_bar), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), widgets->progress_bar, FALSE, FALSE, 0);

    results_frame = gtk_frame_new("Results");
    gtk_box_pack_start(GTK_BOX(vbox), results_frame, TRUE, TRUE, 0);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(results_frame), scrolled);

    widgets->results_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(widgets->results_text), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(widgets->results_text), TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), widgets->results_text);

    widgets->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(widgets->status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(vbox), widgets->status_label, FALSE, FALSE, 0);

    gtk_widget_show_all(widgets->window);
}

static int run_gui(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.cascade.search", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

static void print_usage(void) {
    printf("CASCADE Search Tool v4.0 (C + GTK3 + SQLite)\n");
    printf("usage:\n");
    printf("  cascade /scan <path>            Index directory\n");
    printf("  cascade /search <query>          Search indexed filenames\n");
    printf("  cascade --gui | -g               Launch GTK3 GUI\n");
    printf("  cascade --setup                  Initialize database\n");
    printf("  cascade --config                 Show database configuration\n");
}

int main(int argc, char **argv) {
    int use_gui = 0;
    int gui_arg_index = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gui") == 0 || strcmp(argv[i], "-g") == 0) {
            use_gui = 1;
            gui_arg_index = i;
            break;
        }
    }

    g_mutex_init(&db_lock);

    if (!open_db()) {
        g_mutex_clear(&db_lock);
        return 1;
    }

    if (use_gui) {
        int gtk_argc = argc - 1;
        char **gtk_argv = g_new0(char *, gtk_argc + 1);
        int j = 0;
        int status;

        gui_mode = 1;
        for (int i = 0; i < argc; i++) {
            if (i != gui_arg_index) {
                gtk_argv[j++] = argv[i];
            }
        }

        status = run_gui(gtk_argc, gtk_argv);
        g_free(gtk_argv);
        close_db();
        g_mutex_clear(&db_lock);
        return status;
    }

    if (argc < 2) {
        print_usage();
        close_db();
        g_mutex_clear(&db_lock);
        return 1;
    }

    if (strcmp(argv[1], "/scan") == 0 && argc > 2) {
        ScanJob job = {0};

        printf("Scanning %s...\n", argv[2]);
        if (scan_path(argv[2], &job)) {
            printf("Scan complete: %d files, %d directories, %d skipped\n",
                   job.files_indexed, job.directories_seen, job.directories_skipped);
        } else {
            fprintf(stderr, "Scan failed: %s\n", job.error);
            close_db();
            g_mutex_clear(&db_lock);
            return 1;
        }
    } else if (strcmp(argv[1], "/search") == 0 && argc > 2) {
        int count;

        printf("Searching for: %s\n", argv[2]);
        count = search_to_stdout(argv[2]);
        printf("Found %d results\n", count);
    } else if (strcmp(argv[1], "--setup") == 0) {
        printf("Setup complete\n");
        printf("Database location: %s/.cascade_search/app.db\n", home_dir());
        printf("Tables ready: files, config\n");
    } else if (strcmp(argv[1], "--config") == 0) {
        printf("Database location: %s/.cascade_search/app.db\n", home_dir());
        printf("Journal mode: WAL\n");
        printf("Result limit in GUI: %d\n", RESULT_LIMIT);
    } else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
    }

    close_db();
    g_mutex_clear(&db_lock);
    return 0;
}
