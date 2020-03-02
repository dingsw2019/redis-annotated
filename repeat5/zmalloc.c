
#include <pthread.h>
#include "zmalloc.h"

#define PREFIX_SIZE sizeof(long)

#define update_zmalloc_stat_add(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

#define update_zmalloc_stat_sub(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

#define update_zmalloc_stat_alloc(__n) do{ \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
}while(0)

#define update_zmalloc_stat_free(__n) do{ \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
}while(0)

static void zmalloc_default_oom(size_t size){
    fprintf(stderr,"out of memory,trying to callocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

static void (*zmalloc_handler_oom)(size_t size) = zmalloc_default_oom;

static int used_memory = 0;
static size_t zmalloc_thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;


void *zmalloc(size_t size){
    // 申请内存
    void *ptr = malloc(size+PREFIX_SIZE);
    // 内存不足，抛出异常
    if (!ptr) zmalloc_handler_oom(size);

    // 记录申请的内存大小
    *((size_t*)ptr) = size;

    // 增加内存统计量
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);

    // 返回 结构体起始地址
    return (char*)ptr+PREFIX_SIZE;
}

void *zcalloc(size_t size){

    void *ptr = calloc(1,size+PREFIX_SIZE);
    if (!ptr) zmalloc_handler_oom(size);

    *((size_t*)ptr) = size;

    update_zmalloc_stat_alloc(size+PREFIX_SIZE);

    return (char*)ptr+PREFIX_SIZE;
}

void *zrealloc(void *ptr,size_t size){

    void *realptr,*newptr;
    size_t old_size;

    if (ptr == NULL) return zmalloc(size);

    realptr = ptr - PREFIX_SIZE;

    newptr = realloc(realptr,size);
    if (!newptr) zmalloc_handler_oom(size);

    *((size_t*)newptr) = size;
    old_size = *((size_t*)realptr);

    // 更新统计内存量
    update_zmalloc_stat_free(old_size);
    update_zmalloc_stat_alloc(size);

    return (char*)newptr+PREFIX_SIZE;
}

void zfree(void *ptr){
    void *realptr;
    size_t old_size;

    if (ptr == NULL) return;

    // 获取申请的内存大小
    realptr = ptr-PREFIX_SIZE;
    old_size = *((size_t*)realptr);

    // 减少内存统计量
    update_zmalloc_stat_free(old_size+PREFIX_SIZE);

    // 释放内存
    free(realptr);
}
