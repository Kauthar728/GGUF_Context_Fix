#include "CASCADE_Coordinator.h"
#include "CASCADE_PageSearch.h"
#include "CASCADE_PageScan.h"
#include "CASCADE_PageConfig.h"
#include "CASCADE_PageLogs.h"
#include <stdlib.h>
#include <string.h>

/* Backend event handler for search requests */
static void on_search_request(Event *event, void *user_data) {
    Coordinator *coord = (Coordinator *)user_data;
    
    if (event->type == EVENT_SEARCH_REQUEST) {
        SearchResults results = {0};
        ResultT r = backend_search(coord->backend, 
                                  event->payload.search_request.query,
                                  event->payload.search_request.limit,
                                  &results);
        
        if (r.status) {
            Event response = {0};
            response.type = EVENT_SEARCH_RESULTS;
            response.payload.search_request.query = (char*)&results; /* Pass pointer */
            eventbus_publish(coord->event_bus, &response);
            backend_search_free(&results);
        }
        
        if (r.error) free(r.error);
    }
}

/* Backend event handler for scan requests */
static void on_scan_request(Event *event, void *user_data) {
    Coordinator *coord = (Coordinator *)user_data;
    
    if (event->type == EVENT_SCAN_START) {
        ScanResult result = {0};
        ResultT r = backend_scan_path(coord->backend, 
                                     event->payload.scan_data.path,
                                     &result);
        
        Event response = {0};
        response.type = EVENT_SCAN_COMPLETE;
        response.payload.scan_data.path = (char*)&result; /* Pass pointer */
        eventbus_publish(coord->event_bus, &response);
        
        if (r.error) free(r.error);
    }
}

/* Backend event handler for config changes */
static void on_config_change(Event *event, void *user_data) {
    Coordinator *coord = (Coordinator *)user_data;
    
    if (event->type == EVENT_CONFIG_CHANGE) {
        ResultT r = backend_config_set(coord->backend,
                                      event->payload.config_data.key,
                                      event->payload.config_data.value);
        if (r.error) free(r.error);
    }
}

/* Sidebar button callback */
static void on_sidebar_button(GtkWidget *widget, gpointer data) {
    Coordinator *coord = (Coordinator *)data;
    const char *page_name = g_object_get_data(G_OBJECT(widget), "page-name");
    
    if (page_name) {
        gtk_stack_set_visible_child_name(GTK_STACK(coord->stack), page_name);
    }
}

/* coordinator_create: Create coordinator */
ResultT coordinator_create(Coordinator *coord) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    memset(coord, 0, sizeof(Coordinator));
    
    /* Create event bus */
    ResultT r = eventbus_create();
    if (!r.status) {
        result.status = 0;
        result.error = r.error;
        return result;
    }
    coord->event_bus = (EventBus *)r.data;
    
    /* Create backend engine */
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/.cascade_search/app.db", getenv("HOME"));
    r = backend_create(db_path);
    if (!r.status) {
        result.status = 0;
        result.error = r.error;
        eventbus_destroy(coord->event_bus);
        return result;
    }
    coord->backend = (BackendEngine *)r.data;
    
    /* Create page context */
    coord->page_context = calloc(1, sizeof(PageContext));
    coord->page_context->event_bus = coord->event_bus;
    coord->page_context->backend = coord->backend;
    
    /* Create page registry */
    r = page_registry_create(&coord->page_registry, coord->page_context);
    if (!r.status) {
        result.status = 0;
        result.error = r.error;
        backend_destroy(coord->backend);
        eventbus_destroy(coord->event_bus);
        free(coord->page_context);
        return result;
    }
    
    coord->running = FALSE;
    result.status = 1;
    return result;
}

/* coordinator_init: Initialize coordinator */
ResultT coordinator_init(Coordinator *coord, const char *db_path) {
    (void)db_path;
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    /* Open backend */
    ResultT r = backend_open(coord->backend);
    if (!r.status) {
        result.status = 0;
        result.error = r.error;
        return result;
    }
    
    result.status = 1;
    return result;
}

/* coordinator_boot: Boot all systems */
ResultT coordinator_boot(Coordinator *coord) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    /* Initialize all pages */
    ResultT r = page_registry_init_all(&coord->page_registry);
    if (!r.status) {
        result.status = 0;
        result.error = r.error;
        return result;
    }
    
    coord->running = TRUE;
    result.status = 1;
    return result;
}

/* coordinator_register_pages: Register all page modules */
ResultT coordinator_register_pages(Coordinator *coord) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    /* Register pages */
    PageModule *search_page = page_search_create();
    if (search_page) {
        page_registry_register(&coord->page_registry, search_page);
    }
    
    PageModule *scan_page = page_scan_create();
    if (scan_page) {
        page_registry_register(&coord->page_registry, scan_page);
    }
    
    PageModule *config_page = page_config_create();
    if (config_page) {
        page_registry_register(&coord->page_registry, config_page);
    }
    
    PageModule *logs_page = page_logs_create();
    if (logs_page) {
        page_registry_register(&coord->page_registry, logs_page);
    }
    
    result.status = 1;
    return result;
}

/* coordinator_build_ui: Build GTK UI */
ResultT coordinator_build_ui(Coordinator *coord, GtkApplication *app) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    /* Create main window */
    coord->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(coord->window), "CASCADE Workstation");
    gtk_window_set_default_size(GTK_WINDOW(coord->window), 1200, 800);
    
    /* Create main horizontal box */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    
    /* Create sidebar */
    coord->sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(coord->sidebar, 150, -1);
    
    /* Create sidebar buttons */
    const char *pages[] = {"search", "scan", "config", "logs"};
    const char *labels[] = {"Search", "Scan", "Config", "Logs"};
    
    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(labels[i]);
        g_object_set_data(G_OBJECT(btn), "page-name", (gpointer)pages[i]);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_sidebar_button), coord);
        gtk_box_pack_start(GTK_BOX(coord->sidebar), btn, FALSE, FALSE, 0);
    }
    
    /* Create stack for pages */
    coord->stack = gtk_stack_new();
    coord->page_context->stack = GTK_STACK(coord->stack);
    
    /* Add pages to stack */
    for (int i = 0; i < coord->page_registry.page_count; i++) {
        PageModule *page = coord->page_registry.pages[i];
        gtk_stack_add_titled(GTK_STACK(coord->stack), 
                            page->widget, 
                            page->name, 
                            page->title);
    }
    
    /* Assemble UI */
    gtk_box_pack_start(GTK_BOX(root), coord->sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), coord->stack, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(coord->window), root);
    
    coord->page_context->window = coord->window;
    
    result.status = 1;
    return result;
}

/* coordinator_wire_events: Wire event handlers */
ResultT coordinator_wire_events(Coordinator *coord) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    /* Subscribe coordinator to backend events */
    eventbus_subscribe(coord->event_bus, EVENT_SEARCH_REQUEST, on_search_request, coord);
    eventbus_subscribe(coord->event_bus, EVENT_SCAN_START, on_scan_request, coord);
    eventbus_subscribe(coord->event_bus, EVENT_CONFIG_CHANGE, on_config_change, coord);
    
    result.status = 1;
    return result;
}

/* coordinator_run: Run application */
ResultT coordinator_run(Coordinator *coord) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    gtk_window_present(GTK_WINDOW(coord->window));
    
    result.status = 1;
    return result;
}

/* coordinator_shutdown: Shutdown coordinator */
ResultT coordinator_shutdown(Coordinator *coord) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    coord->running = FALSE;
    
    /* Shutdown event bus */
    eventbus_shutdown(coord->event_bus);
    
    /* Close backend */
    backend_close(coord->backend);
    
    result.status = 1;
    return result;
}

/* coordinator_destroy: Destroy coordinator */
ResultT coordinator_destroy(Coordinator *coord) {
    ResultT result = {0, NULL, NULL};
    
    if (!coord) {
        result.status = 0;
        result.error = strdup("Invalid Coordinator");
        return result;
    }
    
    /* Destroy page registry */
    page_registry_destroy(&coord->page_registry);
    
    /* Destroy backend */
    backend_destroy(coord->backend);
    
    /* Destroy event bus */
    eventbus_destroy(coord->event_bus);
    
    /* Free page context */
    free(coord->page_context);
    
    memset(coord, 0, sizeof(Coordinator));
    
    result.status = 1;
    return result;
}
