#include <stdio.h>
#include <stdlib.h>

static int malloc_num = 0;

void* smalloc (unsigned int size) {
    if (size == 0) {
        return NULL;
    }
    void *ptr = malloc(size);
    if (ptr != NULL) {
        ++malloc_num;
    }
    return ptr;
}

void sfree (void *ptr) {
    if (ptr == NULL) {
        return;
    }
    --malloc_num;
    free(ptr);
}

int GetMallocNum () {
    return malloc_num;
}
