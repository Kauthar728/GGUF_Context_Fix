#ifndef CASCADE_PAGE_REGISTRY_H
#define CASCADE_PAGE_REGISTRY_H

#include "CASCADE_PageModule.h"
#include "CASCADE_Types.h"

#define MAX_PAGES 16

/* Page registry structure */
typedef struct {
    PageModule *pages[MAX_PAGES];
    int page_count;
    PageContext *context;
} PageRegistry;

/* Function signatures */
ResultT page_registry_create(PageRegistry *registry, PageContext *ctx);
ResultT page_registry_register(PageRegistry *registry, PageModule *page);
ResultT page_registry_unregister(PageRegistry *registry, const char *name);
PageModule* page_registry_get(PageRegistry *registry, const char *name);
ResultT page_registry_init_all(PageRegistry *registry);
ResultT page_registry_activate(PageRegistry *registry, const char *name);
ResultT page_registry_deactivate(PageRegistry *registry, const char *name);
ResultT page_registry_destroy(PageRegistry *registry);

#endif /* CASCADE_PAGE_REGISTRY_H */
