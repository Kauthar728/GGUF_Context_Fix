#include "CASCADE_PageLogs.h"
#include <stdlib.h>
#include <string.h>

/* on_clear_clicked: Clear logs callback */
static void on_clear_clicked(GtkWidget *widget, gpointer data) {
    PageModule *page = (PageModule *)data;
    LogsPageData *priv = (LogsPageData *)page->private_data;
    GtkTextBuffer *buffer;
    
    (void)widget;
    
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->log_text));
    gtk_text_buffer_set_text(buffer, "", -1);
    gtk_label_set_text(GTK_LABEL(priv->status_label), "Logs cleared");
}

/* on_log_event: Handle log events */
static void on_log_event(Event *event, void *user_data) {
    PageModule *page = (PageModule *)user_data;
    LogsPageData *priv = (LogsPageData *)page->private_data;
    
    if (event->type == EVENT_LOG_MESSAGE) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->log_text));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, event->payload.log_message, -1);
        gtk_text_buffer_insert(buffer, &end, "\n", -1);
    }
}

/* on_create: Page creation lifecycle */
static ResultT on_create(PageModule *page, PageContext *ctx) {
    (void)ctx;
    ResultT result = {0, NULL, NULL};
    LogsPageData *priv = calloc(1, sizeof(LogsPageData));
    
    if (!priv) {
        result.status = 0;
        result.error = strdup("Failed to allocate LogsPageData");
        return result;
    }
    
    page->private_data = priv;
    
    /* Create main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    
    /* Log text view */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    
    priv->log_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(priv->log_text), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(priv->log_text), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), priv->log_text);
    
    /* Button row */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    priv->clear_button = page_create_button("Clear Logs", G_CALLBACK(on_clear_clicked), page);
    gtk_box_pack_start(GTK_BOX(btn_box), priv->clear_button, FALSE, FALSE, 0);
    
    /* Status label */
    priv->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(priv->status_label), 0);
    
    /* Assemble */
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), priv->status_label, FALSE, FALSE, 0);
    
    page->widget = vbox;
    page->on_event = on_log_event;
    
    /* Subscribe to log events */
    page_module_subscribe(page, EVENT_LOG_MESSAGE);
    
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

/* page_logs_create: Factory function */
PageModule* page_logs_create(void) {
    PageModule *page = calloc(1, sizeof(PageModule));
    if (!page) return NULL;
    
    ResultT r = page_module_create(page, "logs", "Logs");
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
