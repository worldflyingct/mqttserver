#ifndef __SMALLOC_H__
#define __SMALLOC_H__

void* smalloc(unsigned long size);
void sfree (void *ptr);
long GetMallocNum ();

#endif
