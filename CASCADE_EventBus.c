#include "CASCADE_EventBus.h"
#include <stdlib.h>
#include <string.h>

/* Handler list structure */
typedef struct {
    EventHandler handler;
    void *user_data;
} HandlerEntry;

/* Dispatch thread function */
static gpointer dispatch_thread_func(gpointer data) {
    EventBus *bus = (EventBus *)data;
    
    while (bus->running) {
        Event *event = g_async_queue_pop(bus->queue);
        
        if (event->type == EVENT_SHUTDOWN) {
            free(event);
            break;
        }
        
        g_mutex_lock(&bus->lock);
        GList *handlers = g_hash_table_lookup(bus->handlers, GINT_TO_POINTER(event->type));
        handlers = g_list_copy(handlers);
        g_mutex_unlock(&bus->lock);
        
        for (GList *l = handlers; l != NULL; l = l->next) {
            HandlerEntry *entry = (HandlerEntry *)l->data;
            if (entry->handler) {
                entry->handler(event, entry->user_data);
            }
        }
        
        g_list_free(handlers);
        free(event);
    }
    
    return NULL;
}

/* eventbus_create: Create event bus */
ResultT eventbus_create(void) {
    EventBus *bus = calloc(1, sizeof(EventBus));
    ResultT result = {0, NULL, NULL};
    
    if (!bus) {
        result.status = 0;
        result.error = strdup("Failed to allocate EventBus");
        return result;
    }
    
    bus->handlers = g_hash_table_new(g_direct_hash, g_direct_equal);
    bus->queue = g_async_queue_new();
    g_mutex_init(&bus->lock);
    bus->running = TRUE;
    
    bus->dispatch_thread = g_thread_new("eventbus-dispatch", dispatch_thread_func, bus);
    
    result.status = 1;
    result.data = bus;
    return result;
}

/* eventbus_subscribe: Subscribe to event type */
ResultT eventbus_subscribe(EventBus *bus, EventType type, EventHandler handler, void *user_data) {
    ResultT result = {0, NULL, NULL};
    
    if (!bus || !handler) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    HandlerEntry *entry = malloc(sizeof(HandlerEntry));
    entry->handler = handler;
    entry->user_data = user_data;
    
    g_mutex_lock(&bus->lock);
    GList *handlers = g_hash_table_lookup(bus->handlers, GINT_TO_POINTER(type));
    handlers = g_list_append(handlers, entry);
    g_hash_table_insert(bus->handlers, GINT_TO_POINTER(type), handlers);
    g_mutex_unlock(&bus->lock);
    
    result.status = 1;
    return result;
}

/* eventbus_publish: Publish event to bus */
ResultT eventbus_publish(EventBus *bus, Event *event) {
    ResultT result = {0, NULL, NULL};
    
    if (!bus || !event) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    Event *event_copy = malloc(sizeof(Event));
    memcpy(event_copy, event, sizeof(Event));
    
    g_async_queue_push(bus->queue, event_copy);
    
    result.status = 1;
    return result;
}

/* eventbus_shutdown: Shutdown event bus */
ResultT eventbus_shutdown(EventBus *bus) {
    ResultT result = {0, NULL, NULL};
    
    if (!bus) {
        result.status = 0;
        result.error = strdup("Invalid EventBus");
        return result;
    }
    
    Event shutdown_event = {EVENT_SHUTDOWN, {0}, NULL, NULL};
    g_async_queue_push(bus->queue, &shutdown_event);
    
    bus->running = FALSE;
    g_thread_join(bus->dispatch_thread);
    
    result.status = 1;
    return result;
}

/* eventbus_destroy: Destroy event bus */
void eventbus_destroy(EventBus *bus) {
    if (!bus) return;
    
    g_hash_table_destroy(bus->handlers);
    g_async_queue_unref(bus->queue);
    g_mutex_clear(&bus->lock);
    free(bus);
}
