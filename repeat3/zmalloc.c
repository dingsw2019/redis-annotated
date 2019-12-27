#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "zmalloc.h"

#define PREFIX_SIZE sizeof(size_t)

#define update_zmalloc_stat_add(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

#define update_zmalloc_stat_alloc(__n) do{ \
    size_t _n = (__n); \
    if(_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if(thread_safe) { \
        update_zmalloc_stat_add(_n); \
    }else{ \
        used_memory += _n; \
    } \
}while(0)

static size_t used_memory = 0;
static size_t thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

static void zmalloc_default_oom(size_t size){
    fprintf(stderr,"zmalloc:out of memory try to allocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

void *zmalloc(size_t size){

    //头部+指定长度
    void * ptr = malloc(size+PREFIX_SIZE);

    if(!ptr) zmalloc_oom_handler(size);

    //头部记录整体长度
    *((size_t*)ptr) = size;
    //更新整体使用内存大小
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //返回sdshdr部分
    return (char*)ptr+PREFIX_SIZE;
}