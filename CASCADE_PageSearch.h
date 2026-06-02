#ifndef CASCADE_PAGE_SEARCH_H
#define CASCADE_PAGE_SEARCH_H

#include "CASCADE_PageModule.h"

/* Search page private data */
typedef struct {
    GtkWidget *search_entry;
    GtkWidget *search_button;
    GtkWidget *tree_view;
    GtkListStore *list_store;
    GtkWidget *preview_label;
    GtkWidget *status_label;
    int result_limit;
} SearchPageData;

/* Function signatures */
PageModule* page_search_create(void);

#endif /* CASCADE_PAGE_SEARCH_H */
