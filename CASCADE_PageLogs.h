#ifndef CASCADE_PAGE_LOGS_H
#define CASCADE_PAGE_LOGS_H

#include "CASCADE_PageModule.h"

/* Logs page private data */
typedef struct {
    GtkWidget *log_text;
    GtkWidget *clear_button;
    GtkWidget *status_label;
} LogsPageData;

/* Function signatures */
PageModule* page_logs_create(void);

#endif /* CASCADE_PAGE_LOGS_H */
