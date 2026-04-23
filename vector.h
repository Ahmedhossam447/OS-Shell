#ifndef VECTOR_H
#define VECTOR_H

#define VECTOR_INITIAL_CAPACITY 8

typedef struct {
    char **data;
    int    size;
    int    capacity;
} Vector;

void   vector_init(Vector *v);
void   vector_push(Vector *v, char *item);
char  *vector_get(Vector *v, int index);
void   vector_clear(Vector *v);
void   vector_free(Vector *v);
char **vector_to_argv(Vector *v);

#endif
