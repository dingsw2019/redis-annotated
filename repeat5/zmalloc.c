
#include <pthread.h>
#include "zmalloc.h"

#define PREFIX_SIZE sizeof(size_t)

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

    realptr = (char*)ptr - PREFIX_SIZE;

    old_size = *((size_t*)realptr);

    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_handler_oom(size);

    *((size_t*)newptr) = size;

    // 更新统计内存量
    update_zmalloc_stat_free(old_size);
    update_zmalloc_stat_alloc(size);

    return (char*)newptr+PREFIX_SIZE;
}
// void *zrealloc(void *ptr,size_t size){
    
//     void *realptr;
//     size_t oldsize;
//     void *newptr;

//     //原地址不存在,直接申请空间
//     if (ptr == NULL) return zmalloc(size);

//     //真实起始地址
//     realptr = (char*)ptr-PREFIX_SIZE;
//     //获取sdshdr结构体的大小
//     oldsize = *((size_t*)realptr);
//     //新的起始地址
//     newptr = realloc(realptr,size+PREFIX_SIZE);
//     //内存不足,抛出异常
//     if(!newptr) zmalloc_handler_oom(size);
//     //存储size
//     *((size_t*)newptr) = size;

//     //内存用量统计,减旧size,增新size
//     update_zmalloc_stat_free(oldsize);
//     update_zmalloc_stat_alloc(size);

//     //去除数据头,返回sdshdr结构体部分
//     return (char*)newptr+PREFIX_SIZE;
// }

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