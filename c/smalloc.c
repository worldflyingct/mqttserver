#include <stdio.h>
#include <stdlib.h>

static int malloc_num = 0;

void* smalloc (unsigned int size) {
    ++malloc_num;
    return malloc(size);
}

void sfree (void *ptr) {
    --malloc_num;
    free(ptr);
}

int GetMallocNum () {
    return malloc_num;
}
