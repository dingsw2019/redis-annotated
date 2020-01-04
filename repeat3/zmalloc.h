#ifndef __ZMALLOC_H
#define __ZMALLOC_H

void *zmalloc(size_t);
void *zcalloc(size_t);
void *zrealloc(void *ptr,size_t size);
void zfree(void *);

#endif
