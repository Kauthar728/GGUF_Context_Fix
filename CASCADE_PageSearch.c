#include "CASCADE_PageSearch.h"
#include <stdlib.h>
#include <string.h>

/* on_search_clicked: Search button callback */
static void on_search_clicked(GtkWidget *widget, gpointer data) {
    PageModule *page = (PageModule *)data;
    SearchPageData *priv = (SearchPageData *)page->private_data;
    const char *query = gtk_entry_get_text(GTK_ENTRY(priv->search_entry));
    
    (void)widget;
    
    if (!query || !query[0]) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Please enter a search query");
        return;
    }
    
    /* Publish search request event */
    Event event = {0};
    event.type = EVENT_SEARCH_REQUEST;
    event.payload.search_request.query = strdup(query);
    event.payload.search_request.limit = priv->result_limit;
    eventbus_publish(page->context->event_bus, &event);
    
    gtk_label_set_text(GTK_LABEL(priv->status_label), "Searching...");
}

/* on_tree_selection: Tree view selection callback */
static void on_tree_selection(GtkTreeSelection *sel, gpointer data) {
    PageModule *page = (PageModule *)data;
    SearchPageData *priv = (SearchPageData *)page->private_data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *path, *name;
        gtk_tree_model_get(model, &iter, 0, &name, 1, &path, -1);
        
        char buf[1024];
        snprintf(buf, sizeof(buf), "File: %s\nPath: %s", name, path);
        gtk_label_set_text(GTK_LABEL(priv->preview_label), buf);
        
        /* Publish file selected event */
        Event event = {0};
        event.type = EVENT_FILE_SELECTED;
        event.payload.file_data.path = strdup(path);
        event.payload.file_data.name = strdup(name);
        eventbus_publish(page->context->event_bus, &event);
        
        g_free(name);
        g_free(path);
    }
}

/* on_search_event: Handle search results event */
static void on_search_event(Event *event, void *user_data) {
    PageModule *page = (PageModule *)user_data;
    SearchPageData *priv = (SearchPageData *)page->private_data;
    
    if (event->type == EVENT_SEARCH_RESULTS) {
        SearchResults *results = (SearchResults *)event->payload.search_request.query; /* Reuse payload */
        
        gtk_list_store_clear(priv->list_store);
        
        for (int i = 0; i < results->count; i++) {
            GtkTreeIter iter;
            gtk_list_store_append(priv->list_store, &iter);
            gtk_list_store_set(priv->list_store, &iter,
                             0, results->results[i].name,
                             1, results->results[i].path,
                             -1);
        }
        
        char status[256];
        snprintf(status, sizeof(status), "Found %d results", results->count);
        gtk_label_set_text(GTK_LABEL(priv->status_label), status);
    }
}

/* on_create: Page creation lifecycle */
static ResultT on_create(PageModule *page, PageContext *ctx) {
    (void)ctx;
    ResultT result = {0, NULL, NULL};
    SearchPageData *priv = calloc(1, sizeof(SearchPageData));
    
    if (!priv) {
        result.status = 0;
        result.error = strdup("Failed to allocate SearchPageData");
        return result;
    }
    
    priv->result_limit = 2000;
    page->private_data = priv;
    
    /* Create main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    
    /* Search row */
    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    priv->search_entry = page_create_entry("Search files...", "");
    priv->search_button = page_create_button("Search", G_CALLBACK(on_search_clicked), page);
    
    gtk_box_pack_start(GTK_BOX(search_box), priv->search_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(search_box), priv->search_button, FALSE, FALSE, 0);
    
    /* Tree view */
    priv->list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    priv->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(priv->list_store));
    
    GtkCellRenderer *r1 = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(priv->tree_view), -1, "Name", r1, "text", 0, NULL);
    
    GtkCellRenderer *r2 = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(priv->tree_view), -1, "Path", r2, "text", 1, NULL);
    
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
    g_signal_connect(sel, "changed", G_CALLBACK(on_tree_selection), page);
    
    GtkWidget *scroll = page_create_scrolled_widget(priv->tree_view);
    gtk_widget_set_vexpand(scroll, TRUE);
    
    /* Preview label */
    priv->preview_label = gtk_label_new("Select a file to preview");
    gtk_label_set_xalign(GTK_LABEL(priv->preview_label), 0);
    gtk_label_set_selectable(GTK_LABEL(priv->preview_label), TRUE);
    
    /* Status label */
    priv->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(priv->status_label), 0);
    
    /* Assemble */
    gtk_box_pack_start(GTK_BOX(vbox), search_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), priv->preview_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), priv->status_label, FALSE, FALSE, 0);
    
    page->widget = vbox;
    page->on_event = on_search_event;
    
    result.status = 1;
    return result;
}

/* on_activate: Page activation lifecycle */
static ResultT on_activate(PageModule *page) {
    ResultT result = {0, NULL, NULL};
    (void)page;
    result.status = 1;
    return result;
}

/* on_deactivate: Page deactivation lifecycle */
static ResultT on_deactivate(PageModule *page) {
    ResultT result = {0, NULL, NULL};
    (void)page;
    result.status = 1;
    return result;
}

/* on_destroy: Page destruction lifecycle */
static ResultT on_destroy(PageModule *page) {
    ResultT result = {0, NULL, NULL};
    
    if (page->private_data) {
        free(page->private_data);
        page->private_data = NULL;
    }
    
    result.status = 1;
    return result;
}

/* page_search_create: Factory function */
PageModule* page_search_create(void) {
    PageModule *page = calloc(1, sizeof(PageModule));
    if (!page) return NULL;
    
    ResultT r = page_module_create(page, "search", "Search");
    if (!r.status) {
        free(page);
        if (r.error) free(r.error);
        return NULL;
    }
    
    page->on_create = on_create;
    page->on_activate = on_activate;
    page->on_deactivate = on_deactivate;
    page->on_destroy = on_destroy;
    
    return page;
}
