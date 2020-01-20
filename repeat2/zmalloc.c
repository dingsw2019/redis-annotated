#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "zmalloc.h"

//头空间,存储空间实际大小
#define PREFIX_SIZE sizeof(size_t)

#define update_zmalloc_stat_add(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += (_n); \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

#define update_zmalloc_stat_sub(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= (_n); \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

// #define update_zmalloc_stat_alloc(__n) do{ \
//     int _n = (__n); \
//     if(_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
//     if (thread_safe_mutex){ \
//         add(_n); \
//     } else { \
//         used_memory += _n; \
//     } \
// }while(0)
#define update_zmalloc_stat_alloc(__n) do{ \
    size_t _n = (__n); \
    if(_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe){ \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
}while(0)

#define update_zmalloc_stat_free(__n) do{ \
    size_t _n = (__n);\
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
}while(0)

// #define used_memory = 0;
// #define thread_safe_mutex = 0;
// #define p_mutex = PT
static size_t used_memory = 0;
static int zmalloc_thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

//申请空间失败,抛异常
// 写错的
// static inline oom(size_t n){
//     fprintf(stderr,"out of memory try to apply %d byte",n);
//     fflush(stderr);
//     abort();
// }

//fun类型写成inline,
static void zmalloc_default_oom(size_t size){
    fprintf(stderr,"zmalloc:out of memory trying to allocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

// #define oom_handler(size_t n) oom
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

//申请内存空间
void *zmalloc(size_t size){

    void *ptr = malloc(size+PREFIX_SIZE);

    if(!ptr) zmalloc_default_oom(size);

    //头部分添加大小
    *((size_t*)ptr) = size;
    //添加内存使用量
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //返回sdshdr部分
    return (char*)ptr+PREFIX_SIZE;
}

//已calloc方式申请空间
void *zcalloc(size_t size){
    //申请空间
    void *ptr = calloc(1,size+PREFIX_SIZE);
    if (!ptr) zmalloc_oom_handler(size);

    //记录size
    *((size_t*)ptr) = size;

    //统计内存使用量
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);

    //返回sdshdr部分
    return (char*)ptr+PREFIX_SIZE;
}

//释放sdshdr
void zfree(void *ptr){

    void *realptr;
    size_t oldsize;

    if (ptr == NULL) return ;

    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);

    //减少内存使用量
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);

    //释放
    free(realptr);
}

//重新分配内存
void *zrealloc(void *ptr,size_t size){
    void *realptr,*newptr;
    size_t oldsize;

    if (ptr == NULL) return zmalloc(size);
    
    //旧指针起始地址和size大小
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);

    //扩容,生成新地址
    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_oom_handler(size);
    //记录size
    *((size_t*)newptr) = size;

    //内存统计,减少旧size,增加新size
    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(size);

    //返回新地址
    return (char*)newptr+PREFIX_SIZE;
}