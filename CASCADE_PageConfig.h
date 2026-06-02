#ifndef CASCADE_PAGE_CONFIG_H
#define CASCADE_PAGE_CONFIG_H

#include "CASCADE_PageModule.h"

/* Config page private data */
typedef struct {
    GtkWidget *key_entry;
    GtkWidget *value_entry;
    GtkWidget *set_button;
    GtkWidget *get_button;
    GtkWidget *list_button;
    GtkWidget *output_text;
    GtkWidget *status_label;
} ConfigPageData;

/* Function signatures */
PageModule* page_config_create(void);

#endif /* CASCADE_PAGE_CONFIG_H */
