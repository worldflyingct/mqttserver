#ifndef __SMALLOC_H__
#define __SMALLOC_H__

#ifdef DEBUG
void* smalloc(unsigned int size, char *filename, unsigned int line);
void sfree (void *ptr, char *filename, unsigned int line);
int GetMallocNum ();
int setmemcheck ();
#else
#define smalloc(size, filename, line)   malloc(size)
#define sfree(ptr, filename, line)      free(ptr)
#endif

#endif
