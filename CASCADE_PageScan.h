#ifndef CASCADE_PAGE_SCAN_H
#define CASCADE_PAGE_SCAN_H

#include "CASCADE_PageModule.h"

/* Scan page private data */
typedef struct {
    GtkWidget *path_entry;
    GtkWidget *scan_button;
    GtkWidget *progress_bar;
    GtkWidget *status_label;
    GtkWidget *stats_label;
    gboolean scan_running;
} ScanPageData;

/* Function signatures */
PageModule* page_scan_create(void);

#endif /* CASCADE_PAGE_SCAN_H */
