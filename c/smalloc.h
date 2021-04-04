#ifndef __SMALLOC_H__
#define __SMALLOC_H__

void* smalloc(unsigned int size);
void sfree (void *ptr);
int GetMallocNum ();

#endif
