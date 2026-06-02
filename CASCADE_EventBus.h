#ifndef CASCADE_EVENT_BUS_H
#define CASCADE_EVENT_BUS_H

#include <gtk/gtk.h>
#include <glib.h>
#include "CASCADE_Types.h"

/* Event types */
typedef enum {
    EVENT_SCAN_START,
    EVENT_SCAN_PROGRESS,
    EVENT_SCAN_COMPLETE,
    EVENT_SEARCH_REQUEST,
    EVENT_SEARCH_RESULTS,
    EVENT_CONFIG_CHANGE,
    EVENT_LOG_MESSAGE,
    EVENT_FILE_SELECTED,
    EVENT_SHUTDOWN
} EventType;

/* Event payload union */
typedef union {
    struct {
        char *path;
        int files_indexed;
        int directories_seen;
    } scan_data;
    
    struct {
        char *query;
        int limit;
    } search_request;
    
    struct {
        char *path;
        char *name;
        long long size;
    } file_data;
    
    struct {
        char *key;
        char *value;
    } config_data;
    
    char *log_message;
} EventPayload;

/* Event structure */
typedef struct {
    EventType type;
    EventPayload payload;
    void *source;
    void (*callback)(void *, void *);
} Event;

/* Event handler signature */
typedef void (*EventHandler)(Event *event, void *user_data);

/* EventBus structure */
typedef struct {
    GHashTable *handlers;
    GMutex lock;
    GAsyncQueue *queue;
    GThread *dispatch_thread;
    gboolean running;
} EventBus;

/* Function signatures */
ResultT eventbus_create(void);
ResultT eventbus_subscribe(EventBus *bus, EventType type, EventHandler handler, void *user_data);
ResultT eventbus_publish(EventBus *bus, Event *event);
ResultT eventbus_shutdown(EventBus *bus);
void eventbus_destroy(EventBus *bus);

#endif /* CASCADE_EVENT_BUS_H */
