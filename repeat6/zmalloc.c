#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "zmalloc.h"

#define PREFIX_SIZE sizeof(size_t)

// 已锁形式增加统计
#define update_zmalloc_stat_add(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

// 已锁形式减少统计
#define update_zmalloc_stat_sub(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

// 字节对齐,增加使用量统计
#define update_zmalloc_stat_alloc(__n) do{ \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
}while(0)

// 字节对齐,减少使用量统计
#define update_zmalloc_stat_free(__n) do{ \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
}while(0)

static void zmalloc_default_oom(size_t size) {
    fprintf(stderr,"zmalloc:out of memory trying to allocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

static size_t used_memory = 0;
static int zmalloc_thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

void *zmalloc(size_t size){

    // 申请内存
    void *ptr = malloc(size+PREFIX_SIZE);
    if (!ptr) zmalloc_oom_handler(size);
    // 记录内存大小
    *((size_t*)ptr) = size;
    // 内存使用量统计增加
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    // 返回首地址
    return (char*)ptr+PREFIX_SIZE;
}

void *zcalloc(size_t size){

    // 申请内存
    void *ptr = calloc(1,size+PREFIX_SIZE);
    if (!ptr) zmalloc_oom_handler(size);
    // 记录内存大小
    *((size_t*)ptr) = size;
    // 内存使用量统计增加
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    // 返回首地址
    return (char*)ptr+PREFIX_SIZE;
}

void *zrealloc(void *ptr,size_t size){

    // 内存的真是地址，原地址内存空间大小
    void *newptr,*realptr;
    size_t oldsize;

    if (ptr == NULL) return zmalloc(size);

    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);

    // 重新分配内存
    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_oom_handler(size);
    *((size_t*)newptr) = size;

    // 减少原内存大小，增加新内存大小
    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_free(size);

    return (char*)newptr+PREFIX_SIZE;
}

void zfree(void *ptr){

    void *realptr;
    size_t size;

    if (ptr == NULL) return;

    realptr = (char*)ptr-PREFIX_SIZE;
    size = *((size_t*)realptr);

    // 减少内存使用量统计
    update_zmalloc_stat_free(size+PREFIX_SIZE);
    // 释放
    free(realptr);
}