#ifndef __SMALLOC_H__
#define __SMALLOC_H__

#ifdef DEBUG
void* smalloc(uint32_t size, char *filename, uint32_t line);
void sfree (void *ptr, char *filename, uint32_t line);
int GetMallocNum ();
int setmemcheck ();
#else
#define smalloc(size, filename, line)   malloc(size)
#define sfree(ptr, filename, line)      free(ptr)
#endif

#endif
