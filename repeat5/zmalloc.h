#ifndef ZMALLOC_H_
#define ZMALLOC_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

void *zmalloc(size_t size);
void *zcalloc(size_t size);
void *zrealloc(void *ptr,size_t size);
void zfree(void *ptr);


#endif