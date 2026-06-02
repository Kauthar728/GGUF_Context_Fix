#include <gtk/gtk.h>
#include <sqlite3.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#define MAX_PATH 4096

sqlite3 *db;

/* =========================
   UI STRUCTURE
========================= */

typedef struct {
    GtkWidget *window;

    GtkWidget *stack;

    GtkWidget *search_entry;
    GtkWidget *tree;
    GtkListStore *store;

    GtkWidget *preview_label;
    GtkWidget *status;

    GtkWidget *scan_entry;
} App;

/* =========================
   DB INIT
========================= */

int db_open(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/.cascade_gui.db", getenv("HOME"));

    if (sqlite3_open(path, &db) != SQLITE_OK) {
        printf("DB fail\n");
        return 0;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS files("
        "id INTEGER PRIMARY KEY,"
        "path TEXT UNIQUE,"
        "name TEXT);";

    char *err = NULL;
    sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) sqlite3_free(err);

    return 1;
}

/* =========================
   INDEXING
========================= */

void insert_file(const char *path, const char *name) {
    const char *sql = "INSERT OR IGNORE INTO files(path,name) VALUES(?,?)";
    sqlite3_stmt *stmt;

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void scan_dir(const char *root) {
    DIR *d = opendir(root);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", root, e->d_name);

        struct stat st;
        if (stat(full, &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(full);
        } else {
            insert_file(full, e->d_name);
        }
    }

    closedir(d);
}

/* =========================
   SEARCH
========================= */

void clear_tree(App *app) {
    gtk_list_store_clear(app->store);
}

void add_row(App *app, const char *name, const char *path) {
    GtkTreeIter iter;
    gtk_list_store_append(app->store, &iter);

    gtk_list_store_set(app->store, &iter,
                       0, name,
                       1, path,
                       -1);
}

void search_db(App *app, const char *q) {
    clear_tree(app);

    const char *sql = "SELECT name, path FROM files WHERE name LIKE ?";
    sqlite3_stmt *stmt;

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%%%s%%", q);

    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        const char *path = (const char*)sqlite3_column_text(stmt, 1);

        add_row(app, name, path);
    }

    sqlite3_finalize(stmt);
}

/* =========================
   TREE SELECTION (preview)
========================= */

void on_select(GtkTreeSelection *sel, gpointer data) {
    App *app = data;

    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *name;
        gchar *path;

        gtk_tree_model_get(model, &iter, 0, &name, 1, &path, -1);

        char buf[1024];
        snprintf(buf, sizeof(buf),
                 "File: %s\nPath: %s",
                 name, path);

        gtk_label_set_text(GTK_LABEL(app->preview_label), buf);

        g_free(name);
        g_free(path);
    }
}

/* =========================
   BUTTON ACTIONS
========================= */

void on_search(GtkButton *b, App *app) {
    (void)b;
    const char *q = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
    search_db(app, q);
    gtk_label_set_text(GTK_LABEL(app->status), "Search complete");
}

void on_scan(GtkButton *b, App *app) {
    (void)b;
    const char *path = gtk_entry_get_text(GTK_ENTRY(app->scan_entry));

    gtk_label_set_text(GTK_LABEL(app->status), "Scanning...");

    scan_dir(path);

    gtk_label_set_text(GTK_LABEL(app->status), "Scan complete");
}

/* =========================
   UI BUILD
========================= */

GtkWidget* make_sidebar(App *app) {
    (void)app;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *search_btn = gtk_button_new_with_label("Search");
    GtkWidget *scan_btn   = gtk_button_new_with_label("Scan");
    GtkWidget *config_btn = gtk_button_new_with_label("Config");

    gtk_box_pack_start(GTK_BOX(box), search_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scan_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), config_btn, FALSE, FALSE, 0);

    return box;
}

GtkWidget* make_search_page(App *app) {
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    app->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->search_entry), "search files...");

    GtkWidget *btn = gtk_button_new_with_label("Search");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_search), app);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);

    app->store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

    app->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->store));

    GtkCellRenderer *r1 = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(app->tree), -1, "Name", r1, "text", 0, NULL);

    GtkCellRenderer *r2 = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(app->tree), -1, "Path", r2, "text", 1, NULL);

    GtkTreeSelection *sel =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree));

    g_signal_connect(sel, "changed", G_CALLBACK(on_select), app);

    gtk_container_add(GTK_CONTAINER(scroll), app->tree);

    app->preview_label = gtk_label_new("Select a file...");
    gtk_label_set_xalign(GTK_LABEL(app->preview_label), 0);

    gtk_box_pack_start(GTK_BOX(v), app->search_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(v), app->preview_label, FALSE, FALSE, 0);

    return v;
}

GtkWidget* make_scan_page(App *app) {
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    app->scan_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app->scan_entry), getenv("HOME"));

    GtkWidget *btn = gtk_button_new_with_label("Scan");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_scan), app);

    gtk_box_pack_start(GTK_BOX(v), app->scan_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), btn, FALSE, FALSE, 0);

    return v;
}

/* =========================
   APP INIT
========================= */

void activate(GtkApplication *gapp, gpointer data) {
    (void)data;
    App *app = calloc(1, sizeof(App));

    app->window = gtk_application_window_new(gapp);
    gtk_window_set_title(GTK_WINDOW(app->window), "CASCADE Workstation");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 700);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget *sidebar = make_sidebar(app);

    app->stack = gtk_stack_new();

    gtk_stack_add_titled(GTK_STACK(app->stack),
                         make_search_page(app),
                         "search", "Search");

    gtk_stack_add_titled(GTK_STACK(app->stack),
                         make_scan_page(app),
                         "scan", "Scan");

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    app->status = gtk_label_new("Ready");

    gtk_box_pack_start(GTK_BOX(right), app->stack, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(right), app->status, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), right, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(app->window), root);

    gtk_window_present(GTK_WINDOW(app->window));
}

/* =========================
   MAIN
========================= */

int main(int argc, char **argv) {
    if (!db_open()) return 1;

    GtkApplication *app =
        gtk_application_new("com.cascade.workstation", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    sqlite3_close(db);
    return status;
}