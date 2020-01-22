#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "zmalloc.h"

#define PREFIX_SIZE sizeof(size_t)

#define update_zmalloc_stat_add(_n)do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

#define update_zmalloc_stat_sub(_n)do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

#define update_zmalloc_stat_alloc(__n)do{ \
    size_t _n = __n; \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
}while(0)

#define update_zmalloc_stat_free(__n)do{ \
    size_t _n = __n; \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
}while(0)

static size_t used_memory = 0;
static int zmalloc_thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

static void zmalloc_oom_default(size_t size){
    fprintf(stderr,"Out of memory,trying to allocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

static void (*zmalloc_oom_handler)(size_t) = zmalloc_oom_default;

void *zmalloc(size_t size){

    // 申请内存
    void *ptr = malloc(size+PREFIX_SIZE);

    // 内存溢出
    if (!ptr) zmalloc_oom_default(size);

    // 记录申请内存大小
    *((size_t*)ptr) = size;

    // 内存统计，增加
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);

    // 返回 sdshdr部分的内存
    return (char*)ptr+PREFIX_SIZE;
}

void *zcalloc(size_t size){

    void *ptr = calloc(1,size+PREFIX_SIZE);
    if (!ptr) zmalloc_oom_handler(size);

    *((size_t*)ptr) = size;

    update_zmalloc_stat_alloc(size+PREFIX_SIZE);

    return (char*)ptr+PREFIX_SIZE;
}

void zfree(void *ptr){

    void *realptr = (char*)ptr-PREFIX_SIZE;
    size_t oldsize;

    if (ptr == NULL) return ;

    // 获取申请内存大小
    oldsize = *((size_t*)realptr);

    // 内存统计，减少
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);

    // 释放内存
    free(realptr);
}

void *zrealloc(void *ptr,size_t size){

    void *newptr,*realptr;
    size_t oldsize;

    if (ptr == NULL) return zmalloc(size);

    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);

    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_oom_handler(size);

    // 记录申请内存大小
    *((size_t*)newptr) = size;

    // 内存统计,减少原size,增加新size
    // 不需要PREFIX,一增一减就抵消了
    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(size);

    return (char*)newptr+PREFIX_SIZE;
}