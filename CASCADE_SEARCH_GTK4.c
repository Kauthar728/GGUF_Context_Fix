#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>
#include <pthread.h>
#include <gtk/gtk.h>

#define MAX_PATH 4096

sqlite3 *db;
int gui_mode = 0;

typedef struct {
    GtkWidget *window;
    GtkWidget *search_entry;
    GtkWidget *path_entry;
    GtkWidget *results_text;
    GtkWidget *status_label;
    GtkWidget *search_button;
    GtkWidget *scan_button;
    GtkWidget *progress_bar;
} AppWidgets;

int open_db() {
    char db_path[MAX_PATH];
    snprintf(db_path, sizeof(db_path), "%s/.cascade_search/app.db", getenv("HOME"));
    
    char dir_path[MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/.cascade_search", getenv("HOME"));
    mkdir(dir_path, 0755);
    
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "DB open failed: %s\n", sqlite3_errmsg(db));
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
        fprintf(stderr, "DB init failed: %s\n", err);
        sqlite3_free(err);
        return 0;
    }
    
    const char *config_sql =
        "CREATE TABLE IF NOT EXISTS config ("
        "key TEXT PRIMARY KEY,"
        "value TEXT,"
        "updated_at INTEGER"
        ");";
    
    if (sqlite3_exec(db, config_sql, 0, 0, &err) != SQLITE_OK) {
        fprintf(stderr, "Config table init failed: %s\n", err);
        sqlite3_free(err);
    }
    
    const char *cred_sql =
        "CREATE TABLE IF NOT EXISTS credentials ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT,"
        "password_encrypted TEXT,"
        "service TEXT,"
        "created_at INTEGER"
        ");";
    
    if (sqlite3_exec(db, cred_sql, 0, 0, &err) != SQLITE_OK) {
        fprintf(stderr, "Credentials table init failed: %s\n", err);
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

int search(const char *q, char *results, size_t max_len) {
    const char *sql =
        "SELECT path FROM files WHERE name LIKE ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%%%s%%", q);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    
    int count = 0;
    size_t offset = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && offset < max_len - 100) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        int written = snprintf(results + offset, max_len - offset, "%s\n", path);
        if (written > 0 && offset + written < max_len) {
            offset += written;
            count++;
        } else {
            break;
        }
    }
    
    sqlite3_finalize(stmt);
    return count;
}

// GTK4 GUI Functions
void on_search_clicked(GtkWidget *widget, gpointer data) {
    AppWidgets *app = (AppWidgets *)data;
    
    const char *query = gtk_editable_get_text(GTK_EDITABLE(app->search_entry));
    if (!query || strlen(query) == 0) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Please enter a search query");
        return;
    }
    
    char results[65536];
    int count = search(query, results, sizeof(results));
    
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->results_text));
    gtk_text_buffer_set_text(buffer, results, -1);
    
    char status[256];
    snprintf(status, sizeof(status), "Found %d results", count);
    gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

void on_scan_clicked(GtkWidget *widget, gpointer data) {
    AppWidgets *app = (AppWidgets *)data;
    
    const char *path = gtk_editable_get_text(GTK_EDITABLE(app->path_entry));
    if (!path || strlen(path) == 0) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Please enter a directory path");
        return;
    }
    
    gtk_label_set_text(GTK_LABEL(app->status_label), "Scanning...");
    gtk_widget_set_sensitive(app->scan_button, FALSE);
    gtk_widget_set_sensitive(app->search_button, FALSE);
    
    // Process GTK events (GTK4 doesn't have gtk_events_pending)
    // In GTK4, use async operations or threads for long-running tasks
    
    scan(path);
    
    gtk_label_set_text(GTK_LABEL(app->status_label), "Scan complete");
    gtk_widget_set_sensitive(app->scan_button, TRUE);
    gtk_widget_set_sensitive(app->search_button, TRUE);
}

void activate(GtkApplication *app, gpointer user_data) {
    AppWidgets *widgets = g_malloc(sizeof(AppWidgets));
    
    widgets->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(widgets->window), "CASCADE Search Tool");
    gtk_window_set_default_size(GTK_WINDOW(widgets->window), 800, 600);
    
    // Main container
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(widgets->window), vbox);
    
    // Search section
    GtkWidget *search_frame = gtk_frame_new("Search");
    gtk_box_append(GTK_BOX(vbox), search_frame);
    
    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_frame_set_child(GTK_FRAME(search_frame), search_box);
    
    widgets->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(widgets->search_entry), "Enter search query...");
    gtk_box_append(GTK_BOX(search_box), widgets->search_entry);
    
    widgets->search_button = gtk_button_new_with_label("Search");
    g_signal_connect(widgets->search_button, "clicked", G_CALLBACK(on_search_clicked), widgets);
    gtk_box_append(GTK_BOX(search_box), widgets->search_button);
    
    // Scan section
    GtkWidget *scan_frame = gtk_frame_new("Scan Directory");
    gtk_box_append(GTK_BOX(vbox), scan_frame);
    
    GtkWidget *scan_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_frame_set_child(GTK_FRAME(scan_frame), scan_box);
    
    widgets->path_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(widgets->path_entry), getenv("HOME"));
    gtk_box_append(GTK_BOX(scan_box), widgets->path_entry);
    
    widgets->scan_button = gtk_button_new_with_label("Scan");
    g_signal_connect(widgets->scan_button, "clicked", G_CALLBACK(on_scan_clicked), widgets);
    gtk_box_append(GTK_BOX(scan_box), widgets->scan_button);
    
    // Results section
    GtkWidget *results_frame = gtk_frame_new("Results");
    gtk_box_append(GTK_BOX(vbox), results_frame);
    
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_frame_set_child(GTK_FRAME(results_frame), scrolled);
    
    widgets->results_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(widgets->results_text), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), widgets->results_text);
    
    // Status bar
    widgets->status_label = gtk_label_new("Ready");
    gtk_box_append(GTK_BOX(vbox), widgets->status_label);
    
    gtk_window_present(GTK_WINDOW(widgets->window));
}

int run_gui(int argc, char **argv) {
    GtkApplication *app;
    int status;
    
    app = gtk_application_new("com.cascade.search", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}

int main(int argc, char **argv) {
    // Check for GUI mode before opening DB (GTK consumes args)
    int use_gui = 0;
    int gui_arg_index = -1;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gui") == 0 || strcmp(argv[i], "-g") == 0) {
            use_gui = 1;
            gui_arg_index = i;
            break;
        }
    }
    
    if (!open_db()) return 1;
    
    if (use_gui) {
        gui_mode = 1;
        // Remove GUI arg from argv for GTK
        int gtk_argc = argc - 1;
        char **gtk_argv = malloc(sizeof(char*) * gtk_argc);
        int j = 0;
        for (int i = 0; i < argc; i++) {
            if (i != gui_arg_index) {
                gtk_argv[j++] = argv[i];
            }
        }
        
        int status = run_gui(gtk_argc, gtk_argv);
        free(gtk_argv);
        sqlite3_close(db);
        return status;
    }
    
    // CLI mode
    if (argc < 2) {
        printf("CASCADE Search Tool v4.0 (C + GTK4)\n");
        printf("usage:\n");
        printf("  cascade /scan <path>           - Index directory\n");
        printf("  cascade /search <query>         - Search filenames\n");
        printf("  cascade --gui                  - Launch GTK4 GUI\n");
        printf("  cascade --setup                - First-time setup\n");
        printf("  cascade --config               - Configuration mode\n");
        sqlite3_close(db);
        return 1;
    }
    
    if (strcmp(argv[1], "/scan") == 0 && argc > 2) {
        printf("Scanning %s...\n", argv[2]);
        scan(argv[2]);
        printf("Scan complete\n");
    }
    else if (strcmp(argv[1], "/search") == 0 && argc > 2) {
        printf("Searching for: %s\n", argv[2]);
        char results[65536];
        int count = search(argv[2], results, sizeof(results));
        printf("%s", results);
        printf("Found %d results\n", count);
    }
    else if (strcmp(argv[1], "--setup") == 0) {
        printf("Setup mode\n");
        printf("Database location: %s/.cascade_search/app.db\n", getenv("HOME"));
        printf("Tables created: files, config, credentials\n");
        printf("Ready for use\n");
    }
    else if (strcmp(argv[1], "--config") == 0) {
        printf("Config mode (placeholder)\n");
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        printf("Run with no arguments for usage\n");
    }
    
    sqlite3_close(db);
    return 0;
}
