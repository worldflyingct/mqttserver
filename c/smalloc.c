#include <stdio.h>
#include <stdlib.h>

static long malloc_num = 0;

void* smalloc (unsigned long size) {
    malloc_num++;
    return malloc(size);
}

void sfree (void *ptr) {
    malloc_num--;
    free(ptr);
}

long GetMallocNum () {
    return malloc_num;
}
