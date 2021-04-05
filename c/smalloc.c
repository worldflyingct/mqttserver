#include <stdio.h>
#include <stdlib.h>

static int malloc_num = 0;

void* smalloc (unsigned int size) {
    void *ptr = malloc(size);
    if (ptr != NULL) {
        ++malloc_num;
    }
    return ptr;
}

void sfree (void *ptr) {
    --malloc_num;
    free(ptr);
}

int GetMallocNum () {
    return malloc_num;
}
