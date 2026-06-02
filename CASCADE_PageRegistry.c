#include "CASCADE_PageRegistry.h"
#include <stdlib.h>
#include <string.h>

/* page_registry_create: Create page registry */
ResultT page_registry_create(PageRegistry *registry, PageContext *ctx) {
    ResultT result = {0, NULL, NULL};
    
    if (!registry || !ctx) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    memset(registry, 0, sizeof(PageRegistry));
    registry->context = ctx;
    
    result.status = 1;
    return result;
}

/* page_registry_register: Register page module */
ResultT page_registry_register(PageRegistry *registry, PageModule *page) {
    ResultT result = {0, NULL, NULL};
    
    if (!registry || !page) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    if (registry->page_count >= MAX_PAGES) {
        result.status = 0;
        result.error = strdup("Page registry full");
        return result;
    }
    
    /* Check for duplicate name */
    for (int i = 0; i < registry->page_count; i++) {
        if (strcmp(registry->pages[i]->name, page->name) == 0) {
            result.status = 0;
            result.error = strdup("Page name already registered");
            return result;
        }
    }
    
    registry->pages[registry->page_count++] = page;
    
    result.status = 1;
    return result;
}

/* page_registry_unregister: Unregister page module */
ResultT page_registry_unregister(PageRegistry *registry, const char *name) {
    ResultT result = {0, NULL, NULL};
    
    if (!registry || !name) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    for (int i = 0; i < registry->page_count; i++) {
        if (strcmp(registry->pages[i]->name, name) == 0) {
            page_module_destroy(registry->pages[i]);
            free(registry->pages[i]);
            
            /* Shift remaining pages */
            for (int j = i; j < registry->page_count - 1; j++) {
                registry->pages[j] = registry->pages[j + 1];
            }
            registry->page_count--;
            
            result.status = 1;
            return result;
        }
    }
    
    result.status = 0;
    result.error = strdup("Page not found");
    return result;
}

/* page_registry_get: Get page by name */
PageModule* page_registry_get(PageRegistry *registry, const char *name) {
    if (!registry || !name) return NULL;
    
    for (int i = 0; i < registry->page_count; i++) {
        if (strcmp(registry->pages[i]->name, name) == 0) {
            return registry->pages[i];
        }
    }
    
    return NULL;
}

/* page_registry_init_all: Initialize all registered pages */
ResultT page_registry_init_all(PageRegistry *registry) {
    ResultT result = {0, NULL, NULL};
    
    if (!registry) {
        result.status = 0;
        result.error = strdup("Invalid PageRegistry");
        return result;
    }
    
    for (int i = 0; i < registry->page_count; i++) {
        ResultT r = page_module_init(registry->pages[i], registry->context);
        if (!r.status) {
            result.status = 0;
            result.error = r.error;
            return result;
        }
    }
    
    result.status = 1;
    return result;
}

/* page_registry_activate: Activate page by name */
ResultT page_registry_activate(PageRegistry *registry, const char *name) {
    ResultT result = {0, NULL, NULL};
    
    if (!registry || !name) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    PageModule *page = page_registry_get(registry, name);
    if (!page) {
        result.status = 0;
        result.error = strdup("Page not found");
        return result;
    }
    
    result = page_module_activate(page);
    return result;
}

/* page_registry_deactivate: Deactivate page by name */
ResultT page_registry_deactivate(PageRegistry *registry, const char *name) {
    ResultT result = {0, NULL, NULL};
    
    if (!registry || !name) {
        result.status = 0;
        result.error = strdup("Invalid parameters");
        return result;
    }
    
    PageModule *page = page_registry_get(registry, name);
    if (!page) {
        result.status = 0;
        result.error = strdup("Page not found");
        return result;
    }
    
    result = page_module_deactivate(page);
    return result;
}

/* page_registry_destroy: Destroy page registry */
ResultT page_registry_destroy(PageRegistry *registry) {
    ResultT result = {0, NULL, NULL};
    
    if (!registry) {
        result.status = 0;
        result.error = strdup("Invalid PageRegistry");
        return result;
    }
    
    for (int i = 0; i < registry->page_count; i++) {
        page_module_destroy(registry->pages[i]);
        free(registry->pages[i]);
    }
    
    registry->page_count = 0;
    
    result.status = 1;
    return result;
}
