#ifndef CASCADE_PAGE_MODULE_H
#define CASCADE_PAGE_MODULE_H

#include <gtk/gtk.h>
#include "CASCADE_EventBus.h"
#include "CASCADE_BackendEngine.h"
#include "CASCADE_Types.h"

/* Forward declaration */
typedef struct PageModule PageModule;

/* Page context - shared data passed to all pages */
typedef struct {
    EventBus *event_bus;
    BackendEngine *backend;
    GtkStack *stack;
    GtkWidget *window;
} PageContext;

/* Page lifecycle states */
typedef enum {
    PAGE_STATE_INIT,
    PAGE_STATE_ACTIVE,
    PAGE_STATE_HIDDEN,
    PAGE_STATE_DESTROY
} PageState;

/* Page module structure */
typedef struct PageModule {
    char *name;
    char *title;
    GtkWidget *widget;
    PageState state;
    PageContext *context;
    
    /* Lifecycle functions */
    ResultT (*on_create)(PageModule *page, PageContext *ctx);
    ResultT (*on_activate)(PageModule *page);
    ResultT (*on_deactivate)(PageModule *page);
    ResultT (*on_destroy)(PageModule *page);
    
    /* Event handlers */
    void (*on_event)(Event *event, void *user_data);
    
    void *private_data;
} PageModule;

/* Page module factory signature */
typedef PageModule* (*PageFactory)(void);

/* Function signatures */
ResultT page_module_create(PageModule *page, const char *name, const char *title);
ResultT page_module_init(PageModule *page, PageContext *ctx);
ResultT page_module_activate(PageModule *page);
ResultT page_module_deactivate(PageModule *page);
ResultT page_module_destroy(PageModule *page);
ResultT page_module_subscribe(PageModule *page, EventType type);

/* Utility functions */
GtkWidget* page_create_scrolled_widget(GtkWidget *child);
GtkWidget* page_create_frame(const char *label, GtkWidget *content);
GtkWidget* page_create_button(const char *label, GCallback callback, gpointer data);
GtkWidget* page_create_entry(const char *placeholder, const char *default_text);

#endif /* CASCADE_PAGE_MODULE_H */
