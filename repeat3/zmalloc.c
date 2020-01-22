#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// #define PREFIX_SIZE sizeof(long)
#define PREFIX_SIZE sizeof(size_t)

//线程互斥锁保证,内存使用量累加正确性
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

//被4或8整除的数字,如果不能就进位,然后加入内存使用量中
#define update_zmalloc_stat_alloc(__n) do{ \
    size_t _n = __n; \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
}while(0)

#define update_zmalloc_stat_free(__n) do{ \
    size_t _n = __n; \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
}while(0)

static size_t used_memory = 0;
static size_t thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

//默认内存溢出异常,调用方法
static void zmalloc_default_oom(size_t size){
    fprintf(stderr,"zmalloc: out of memory trying to allocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

//内存溢出,公共入口方法
// static void (*zmalloc_oom_handler)(size_t size) = zmalloc_default_oom;
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

void *zmalloc(size_t size){
    //申请空间,加头信息长度,存储size
    void *ptr = malloc(size+PREFIX_SIZE);
    //内存溢出,抛出异常
    if (!ptr) zmalloc_oom_handler(size);
    //存储size
    *((size_t*)ptr) = size;
    //增加内存使用量
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //返回申请的size部分空间,就是sdshdr部分
    return (char*)ptr+PREFIX_SIZE;
}

void *zcalloc(size_t size){
    //申请空间,空间默认初始化,初始化为0
    void *ptr = calloc(1,size+PREFIX_SIZE);
    //内存溢出,抛出异常
    if (!ptr) zmalloc_oom_handler(size);
    //头信息记录size长度
    *((size_t*)ptr) = size;
    //增加内存使用量
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //返回sdshdr部分的内存地址
    return (char*)ptr+PREFIX_SIZE;
}

//重新分配内存
void *zrealloc(void *ptr,size_t size){

    void *realptr,*newptr;
    size_t oldsize;

    if(ptr == NULL) return zmalloc(size);

    // 获取原内存大小
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);

    // 内存分配
    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_oom_handler(size);

    //漏写，设置新内存大小
    *((size_t*)newptr) = size;

    // 减少原内存大小,增加新内存大小
    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(size);

    // 返回 sdshdr 结构
    // return (char*)newptr-PREFIX_SIZE;
    return (char*)newptr+PREFIX_SIZE;
}

// void zfree(void *sh){
//     size_t oldsize;
//     void *oldptr;

//     //内存起始地址,内存申请大小
//     oldptr = sh-PREFIX_SIZE;
//     oldsize = *((size_t*)oldptr);

//     //减少内存使用量
//     update_zmalloc_stat_free(oldsize+PREFIX_SIZE);

//     //释放内存
//     free(oldptr);
// }

void zfree(void *sh){
    size_t oldsize;
    void *oldptr;

    if (sh == NULL) return;
    //内存起始地址,内存申请大小
    oldptr = (char*)sh-PREFIX_SIZE;
    oldsize = *((size_t*)oldptr);

    //减少内存使用量
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);

    //释放内存
    free(oldptr);
}