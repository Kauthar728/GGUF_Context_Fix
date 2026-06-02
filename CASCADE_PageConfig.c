#include "CASCADE_PageConfig.h"
#include <stdlib.h>
#include <string.h>

/* on_set_clicked: Set config callback */
static void on_set_clicked(GtkWidget *widget, gpointer data) {
    PageModule *page = (PageModule *)data;
    ConfigPageData *priv = (ConfigPageData *)page->private_data;
    const char *key = gtk_entry_get_text(GTK_ENTRY(priv->key_entry));
    const char *value = gtk_entry_get_text(GTK_ENTRY(priv->value_entry));
    
    (void)widget;
    
    if (!key || !key[0]) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Please enter a key");
        return;
    }
    
    /* Publish config change event */
    Event event = {0};
    event.type = EVENT_CONFIG_CHANGE;
    event.payload.config_data.key = strdup(key);
    event.payload.config_data.value = strdup(value ? value : "");
    eventbus_publish(page->context->event_bus, &event);
    
    gtk_label_set_text(GTK_LABEL(priv->status_label), "Config updated");
}

/* on_get_clicked: Get config callback */
static void on_get_clicked(GtkWidget *widget, gpointer data) {
    PageModule *page = (PageModule *)data;
    ConfigPageData *priv = (ConfigPageData *)page->private_data;
    const char *key = gtk_entry_get_text(GTK_ENTRY(priv->key_entry));
    
    (void)widget;
    
    if (!key || !key[0]) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Please enter a key");
        return;
    }
    
    char *value = NULL;
    ResultT r = backend_config_get(page->context->backend, key, &value);
    
    if (r.status && value) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s = %s", key, value);
        gtk_label_set_text(GTK_LABEL(priv->status_label), buf);
        free(value);
    } else {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Key not found");
    }
    
    if (r.error) free(r.error);
}

/* on_create: Page creation lifecycle */
static ResultT on_create(PageModule *page, PageContext *ctx) {
    (void)ctx;
    ResultT result = {0, NULL, NULL};
    ConfigPageData *priv = calloc(1, sizeof(ConfigPageData));
    
    if (!priv) {
        result.status = 0;
        result.error = strdup("Failed to allocate ConfigPageData");
        return result;
    }
    
    page->private_data = priv;
    
    /* Create main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    
    /* Key entry row */
    GtkWidget *key_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    priv->key_entry = page_create_entry("Config key...", "");
    gtk_box_pack_start(GTK_BOX(key_box), priv->key_entry, TRUE, TRUE, 0);
    
    /* Value entry row */
    GtkWidget *value_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    priv->value_entry = page_create_entry("Config value...", "");
    gtk_box_pack_start(GTK_BOX(value_box), priv->value_entry, TRUE, TRUE, 0);
    
    /* Button row */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    priv->set_button = page_create_button("Set", G_CALLBACK(on_set_clicked), page);
    priv->get_button = page_create_button("Get", G_CALLBACK(on_get_clicked), page);
    gtk_box_pack_start(GTK_BOX(btn_box), priv->set_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), priv->get_button, FALSE, FALSE, 0);
    
    /* Status label */
    priv->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(priv->status_label), 0);
    
    /* Assemble */
    gtk_box_pack_start(GTK_BOX(vbox), key_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), value_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), priv->status_label, FALSE, FALSE, 0);
    
    page->widget = vbox;
    
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

/* page_config_create: Factory function */
PageModule* page_config_create(void) {
    PageModule *page = calloc(1, sizeof(PageModule));
    if (!page) return NULL;
    
    ResultT r = page_module_create(page, "config", "Config");
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
