#include <gtk/gtk.h>
#include "CASCADE_Coordinator.h"
#include <stdlib.h>
#include <string.h>

/* Global coordinator */
static Coordinator *g_coord = NULL;

/* activate: GTK application activate callback */
static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    
    /* Set application on window */
    gtk_window_set_application(GTK_WINDOW(g_coord->window), app);
    
    /* Run coordinator */
    ResultT r = coordinator_run(g_coord);
    if (!r.status) {
        fprintf(stderr, "Coordinator run failed: %s\n", r.error ? r.error : "unknown");
        if (r.error) free(r.error);
    }
}

/* shutdown: GTK application shutdown callback */
static void shutdown(GtkApplication *app, gpointer user_data) {
    (void)app;
    (void)user_data;
    
    if (g_coord) {
        coordinator_shutdown(g_coord);
        coordinator_destroy(g_coord);
        g_coord = NULL;
    }
}

/* main: Application entry point */
int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    
    /* Create coordinator */
    g_coord = calloc(1, sizeof(Coordinator));
    ResultT r = coordinator_create(g_coord);
    if (!r.status) {
        fprintf(stderr, "Coordinator creation failed: %s\n", r.error ? r.error : "unknown");
        if (r.error) free(r.error);
        free(g_coord);
        return 1;
    }
    
    /* Initialize coordinator */
    r = coordinator_init(g_coord, NULL);
    if (!r.status) {
        fprintf(stderr, "Coordinator initialization failed: %s\n", r.error ? r.error : "unknown");
        if (r.error) free(r.error);
        coordinator_destroy(g_coord);
        free(g_coord);
        return 1;
    }
    
    /* Register pages */
    r = coordinator_register_pages(g_coord);
    if (!r.status) {
        fprintf(stderr, "Page registration failed: %s\n", r.error ? r.error : "unknown");
        if (r.error) free(r.error);
        coordinator_destroy(g_coord);
        free(g_coord);
        return 1;
    }
    
    /* Create GTK application */
    app = gtk_application_new("com.cascade.workstation", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), NULL);
    
    /* Build UI with application */
    r = coordinator_build_ui(g_coord, app);
    if (!r.status) {
        fprintf(stderr, "UI build failed: %s\n", r.error ? r.error : "unknown");
        if (r.error) free(r.error);
        coordinator_destroy(g_coord);
        free(g_coord);
        g_object_unref(app);
        return 1;
    }
    
    /* Wire events */
    r = coordinator_wire_events(g_coord);
    if (!r.status) {
        fprintf(stderr, "Event wiring failed: %s\n", r.error ? r.error : "unknown");
        if (r.error) free(r.error);
        coordinator_destroy(g_coord);
        free(g_coord);
        g_object_unref(app);
        return 1;
    }
    
    /* Boot systems */
    r = coordinator_boot(g_coord);
    if (!r.status) {
        fprintf(stderr, "Coordinator boot failed: %s\n", r.error ? r.error : "unknown");
        if (r.error) free(r.error);
        coordinator_destroy(g_coord);
        free(g_coord);
        g_object_unref(app);
        return 1;
    }
    
    /* Run application */
    status = g_application_run(G_APPLICATION(app), argc, argv);
    
    /* Cleanup */
    g_object_unref(app);
    
    return status;
}
