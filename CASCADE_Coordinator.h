#ifndef CASCADE_COORDINATOR_H
#define CASCADE_COORDINATOR_H

#include "CASCADE_EventBus.h"
#include "CASCADE_BackendEngine.h"
#include "CASCADE_PageRegistry.h"
#include "CASCADE_Types.h"
#include <gtk/gtk.h>

/* Coordinator structure */
typedef struct {
    EventBus *event_bus;
    BackendEngine *backend;
    PageRegistry page_registry;
    PageContext *page_context;
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *sidebar;
    gboolean running;
} Coordinator;

/* Function signatures */
ResultT coordinator_create(Coordinator *coord);
ResultT coordinator_init(Coordinator *coord, const char *db_path);
ResultT coordinator_boot(Coordinator *coord);
ResultT coordinator_register_pages(Coordinator *coord);
ResultT coordinator_build_ui(Coordinator *coord, GtkApplication *app);
ResultT coordinator_wire_events(Coordinator *coord);
ResultT coordinator_run(Coordinator *coord);
ResultT coordinator_shutdown(Coordinator *coord);
ResultT coordinator_destroy(Coordinator *coord);

#endif /* CASCADE_COORDINATOR_H */
