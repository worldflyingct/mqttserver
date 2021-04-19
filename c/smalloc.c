#include <stdio.h>
#include <stdlib.h>
#include <mcheck.h>

static int malloc_num = 0;

void* smalloc (unsigned int size, char *filename, unsigned int line) {
    printf("malloc, size: %u, in %s, at %d\n", size, filename, line);
    if (size == 0) {
        printf("malloc fail, size: %u, in %s, at %d\n", size, filename, line);
        return NULL;
    }
    void *ptr = malloc(size);
    if (ptr == NULL) {
        printf("malloc fail, size: %u, in %s, at %d\n", size, filename, line);
        return NULL;
    }
    ++malloc_num;
    return ptr;
}

void sfree (void *ptr, char *filename, unsigned int line) {
    printf("free, in %s, at %d\n", filename, line);
    if (ptr == NULL) {
        printf("ptr is NULL, in %s, at %d\n", filename, line);
        return;
    }
    --malloc_num;
    free(ptr);
}

int GetMallocNum () {
    return malloc_num;
}

void abortfun(enum mcheck_status mstatus) {
    switch(mstatus) {
        case MCHECK_FREE: printf("[abortfun]Block freed twice.\n");break;
        case MCHECK_HEAD: printf("[abortfun]Memory before the block was clobbered.\n");break;
        case MCHECK_TAIL: printf("[abortfun]Memory after the block was clobbered.\n");break;
        default: printf("[abortfun]Block is fine.\n");break;
    }
}

int setmemcheck () {
    int res = mcheck(abortfun);
    if (res != 0) {
        printf("set mem check fail.");
    }
    return res;
}
