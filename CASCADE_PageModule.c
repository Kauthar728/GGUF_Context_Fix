#include "CASCADE_PageModule.h"
#include <stdlib.h>
#include <string.h>

/* page_module_create: Create page module */
ResultT page_module_create(PageModule *page, const char *name, const char *title) {
    ResultT result = {0, NULL, NULL};
    
    if (!page || !name) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    memset(page, 0, sizeof(PageModule));
    page->name = strdup(name);
    page->title = strdup(title ? title : name);
    page->state = PAGE_STATE_INIT;
    
    result.status = 1;
    return result;
}

/* page_module_init: Initialize page with context */
ResultT page_module_init(PageModule *page, PageContext *ctx) {
    ResultT result = {0, NULL, NULL};
    
    if (!page || !ctx) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    page->context = ctx;
    
    if (page->on_create) {
        result = page->on_create(page, ctx);
        if (!result.status) {
            return result;
        }
    }
    
    page->state = PAGE_STATE_INIT;
    result.status = 1;
    return result;
}

/* page_module_activate: Activate page */
ResultT page_module_activate(PageModule *page) {
    ResultT result = {0, NULL, NULL};
    
    if (!page) {
        result.status = 0;
        result.error = strdup("Invalid PageModule");
        return result;
    }
    
    if (page->on_activate) {
        result = page->on_activate(page);
        if (!result.status) {
            return result;
        }
    }
    
    page->state = PAGE_STATE_ACTIVE;
    result.status = 1;
    return result;
}

/* page_module_deactivate: Deactivate page */
ResultT page_module_deactivate(PageModule *page) {
    ResultT result = {0, NULL, NULL};
    
    if (!page) {
        result.status = 0;
        result.error = strdup("Invalid PageModule");
        return result;
    }
    
    if (page->on_deactivate) {
        result = page->on_deactivate(page);
        if (!result.status) {
            return result;
        }
    }
    
    page->state = PAGE_STATE_HIDDEN;
    result.status = 1;
    return result;
}

/* page_module_destroy: Destroy page */
ResultT page_module_destroy(PageModule *page) {
    ResultT result = {0, NULL, NULL};
    
    if (!page) {
        result.status = 0;
        result.error = strdup("Invalid PageModule");
        return result;
    }
    
    if (page->on_destroy) {
        result = page->on_destroy(page);
    }
    
    if (page->name) free(page->name);
    if (page->title) free(page->title);
    if (page->private_data) free(page->private_data);
    
    page->state = PAGE_STATE_DESTROY;
    result.status = 1;
    return result;
}

/* page_module_subscribe: Subscribe page to event type */
ResultT page_module_subscribe(PageModule *page, EventType type) {
    ResultT result = {0, NULL, NULL};
    
    if (!page || !page->context || !page->context->event_bus) {
        result.status = 0;
        result.error = strdup("Invalid PageModule or context");
        return result;
    }
    
    if (page->on_event) {
        result = eventbus_subscribe(page->context->event_bus, type, page->on_event, page);
    } else {
        result.status = 1;
    }
    
    return result;
}

/* page_create_scrolled_widget: Create scrolled widget wrapper */
GtkWidget* page_create_scrolled_widget(GtkWidget *child) {
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), child);
    return scroll;
}

/* page_create_frame: Create frame with label */
GtkWidget* page_create_frame(const char *label, GtkWidget *content) {
    GtkWidget *frame = gtk_frame_new(label);
    gtk_container_add(GTK_CONTAINER(frame), content);
    return frame;
}

/* page_create_button: Create button with callback */
GtkWidget* page_create_button(const char *label, GCallback callback, gpointer data) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    if (callback) {
        g_signal_connect(btn, "clicked", callback, data);
    }
    return btn;
}

/* page_create_entry: Create entry with placeholder */
GtkWidget* page_create_entry(const char *placeholder, const char *default_text) {
    GtkWidget *entry = gtk_entry_new();
    if (placeholder) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    }
    if (default_text) {
        gtk_entry_set_text(GTK_ENTRY(entry), default_text);
    }
    return entry;
}
