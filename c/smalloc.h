#ifndef __SMALLOC_H__
#define __SMALLOC_H__

void* smalloc(unsigned int size, char *filename, unsigned int line);
void sfree (void *ptr, char *filename, unsigned int line);
int GetMallocNum ();

#endif
