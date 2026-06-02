#ifndef CASCADE_TYPES_H
#define CASCADE_TYPES_H

/* ResultT pattern: (status, data, error) */
typedef struct {
    int status;
    void *data;
    char *error;
} ResultT;

#endif /* CASCADE_TYPES_H */
