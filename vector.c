#include <stdio.h>
#include <stdlib.h>
#include "vector.h"

void vector_init(Vector *v) {
    v->size = 0;
    v->capacity = VECTOR_INITIAL_CAPACITY;
    v->data = malloc(sizeof(char *) * v->capacity);
    if (v->data == NULL) {
        perror("malloc");
        exit(1);
    }
}

void vector_push(Vector *v, char *item) {
    if (v->size >= v->capacity) {
        v->capacity *= 2;
        v->data = realloc(v->data, sizeof(char *) * v->capacity);
        if (v->data == NULL) {
            perror("realloc");
            exit(1);
        }
    }
    v->data[v->size++] = item;
}

char *vector_get(Vector *v, int index) {
    if (index < 0 || index >= v->size) {
        return NULL;
    }
    return v->data[index];
}

void vector_clear(Vector *v) {
    v->size = 0;
}

void vector_free(Vector *v) {
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

char **vector_to_argv(Vector *v) {
    vector_push(v, NULL);
    v->size--;
    return v->data;
}
