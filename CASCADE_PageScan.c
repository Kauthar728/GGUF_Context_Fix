#include "CASCADE_PageScan.h"
#include <stdlib.h>
#include <string.h>

/* on_scan_clicked: Scan button callback */
static void on_scan_clicked(GtkWidget *widget, gpointer data) {
    PageModule *page = (PageModule *)data;
    ScanPageData *priv = (ScanPageData *)page->private_data;
    const char *path = gtk_entry_get_text(GTK_ENTRY(priv->path_entry));
    
    (void)widget;
    
    if (!path || !path[0]) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Please enter a directory path");
        return;
    }
    
    if (priv->scan_running) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Scan already running");
        return;
    }
    
    priv->scan_running = TRUE;
    gtk_widget_set_sensitive(priv->scan_button, FALSE);
    gtk_label_set_text(GTK_LABEL(priv->status_label), "Scanning...");
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(priv->progress_bar), "Scanning");
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(priv->progress_bar));
    
    /* Publish scan start event */
    Event event = {0};
    event.type = EVENT_SCAN_START;
    event.payload.scan_data.path = strdup(path);
    eventbus_publish(page->context->event_bus, &event);
}

/* on_scan_event: Handle scan events */
static void on_scan_event(Event *event, void *user_data) {
    PageModule *page = (PageModule *)user_data;
    ScanPageData *priv = (ScanPageData *)page->private_data;
    
    if (event->type == EVENT_SCAN_COMPLETE) {
        priv->scan_running = FALSE;
        gtk_widget_set_sensitive(priv->scan_button, TRUE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(priv->progress_bar), NULL);
        
        ScanResult *result = (ScanResult *)event->payload.scan_data.path; /* Reuse payload */
        char status[512];
        if (result->error[0]) {
            snprintf(status, sizeof(status), "Scan failed: %s", result->error);
        } else {
            snprintf(status, sizeof(status), "Scan complete: %d files, %d directories, %d skipped",
                     result->files_indexed, result->directories_seen, result->directories_skipped);
        }
        gtk_label_set_text(GTK_LABEL(priv->status_label), status);
    }
}

/* on_create: Page creation lifecycle */
static ResultT on_create(PageModule *page, PageContext *ctx) {
    (void)ctx;
    ResultT result = {0, NULL, NULL};
    ScanPageData *priv = calloc(1, sizeof(ScanPageData));
    
    if (!priv) {
        result.status = 0;
        result.error = strdup("Failed to allocate ScanPageData");
        return result;
    }
    
    page->private_data = priv;
    
    /* Create main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    
    /* Path entry row */
    GtkWidget *path_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    priv->path_entry = page_create_entry("Directory path...", getenv("HOME"));
    priv->scan_button = page_create_button("Scan", G_CALLBACK(on_scan_clicked), page);
    
    gtk_box_pack_start(GTK_BOX(path_box), priv->path_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(path_box), priv->scan_button, FALSE, FALSE, 0);
    
    /* Progress bar */
    priv->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(priv->progress_bar), TRUE);
    
    /* Status label */
    priv->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(priv->status_label), 0);
    
    /* Stats label */
    priv->stats_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(priv->stats_label), 0);
    
    /* Assemble */
    gtk_box_pack_start(GTK_BOX(vbox), path_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), priv->progress_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), priv->status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), priv->stats_label, FALSE, FALSE, 0);
    
    page->widget = vbox;
    page->on_event = on_scan_event;
    
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

/* page_scan_create: Factory function */
PageModule* page_scan_create(void) {
    PageModule *page = calloc(1, sizeof(PageModule));
    if (!page) return NULL;
    
    ResultT r = page_module_create(page, "scan", "Scan");
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
