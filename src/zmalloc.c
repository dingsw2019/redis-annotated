#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "zmalloc.h"

//由于malloc函数申请的内存不会标识内存块的大小，
//而我们需要统计内存大小，所以需要在多申请PREFIX_SIZE
#define PREFIX_SIZE sizeof(size_t)

/**
 * 累加内存使用容量(互斥锁保证线程安全)
 */
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

/**
 * 统计使用的内存容量
 */
#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    /*本次分配内存的大小是否为sizeof(long)的整数倍*/ \
    /*如果不是,添加占位空间,已满足内存对齐(4/8字节)的要求*/ \
    /*sizeof(long)，64位机对应着8字节的内存对齐；32位机则对应着4字节的内存对齐*/ \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
}while(0)

#define update_zmalloc_stat_free(__n) do { \
    size_t _n = __n; \
    if (_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
}while(0)

/**
 * 内存申请失败,抛出异常(内存溢出)
 */
static void zmalloc_default_oom(size_t size){
    fprintf(stderr,"zmalloc:out of memory trying to allocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

//内存溢出函数可自定义,所以封装入口
//默认使用default_oom
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

//使用内存的容量
static size_t used_memory = 0;
//是否线程安全方式运行
static int zmalloc_thread_safe = 0;
//线程互斥锁
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * 以malloc方式,申请内存空间
 * void 是没有任何返回值
 * void *是返回任意类型的值的指针
 * @link https://juejin.im/entry/5b2b4993e51d4553156bdb30 
 */
void *zmalloc(size_t size){

    //多申请的PREFIX_SIZE空间,用来存储实际分配的数据块大小
    void *ptr = malloc(size+PREFIX_SIZE);
    //内存分配失败,抛出异常
    if (!ptr) zmalloc_oom_handler(size);

    //存储实际分配的数据块大小,PREFIX_SIZE申请的空间就是为了这里用
    *((size_t*)ptr) = size;
    //统计使用的内存大小
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //去除数据头,才是结构体部分
    return (char*)ptr+PREFIX_SIZE;
}

/**
 * 以calloc方式,申请内存空间,
 * calloc对分配的空间做初始化,初始化为0
 */
void *zcalloc(size_t size){
    //申请空间
    void *ptr = calloc(1,size+PREFIX_SIZE);
    //内存溢出,抛出异常
    if(!ptr) zmalloc_oom_handler(size);
    //存储实际分配的数据块大小,PREFIX_SIZE申请的空间就是为了这里用
    *((size_t*)ptr) = size;
    //统计使用的内存大小
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //去除数据头,返回sdshdr结构体部分
    return (char*)ptr+PREFIX_SIZE;
}

/**
 * 重新分配内存大小
 */
void *zrealloc(void *ptr,size_t size){
    
    void *realptr;
    size_t oldsize;
    void *newptr;

    //原地址不存在,直接申请空间
    if (ptr == NULL) return zmalloc(size);

    //真实起始地址
    realptr = (char*)ptr-PREFIX_SIZE;
    //获取sdshdr结构体的大小
    oldsize = *((size_t*)realptr);
    //新的起始地址
    newptr = realloc(realptr,size+PREFIX_SIZE);
    //内存不足,抛出异常
    if(!newptr) zmalloc_oom_handler(size);
    //存储size
    *((size_t*)newptr) = size;

    //内存用量统计,减旧size,增新size
    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(size);

    //去除数据头,返回sdshdr结构体部分
    return (char*)newptr+PREFIX_SIZE;
}

/**
 * 释放空间
 * @param ptr 块的sdshdr结构体的开始地址
 * @return void 无返回 
 */
void zfree(void *ptr){
    //块开始的地址
    void *realptr;
    //结构体的大小,不是块的大小,块=数据头+sdshdr结构体
    size_t oldsize;

    //找到分配空间的开始位置
    realptr = (char*)ptr-PREFIX_SIZE;
    //获取sdshdr结构体的大小
    oldsize = *((size_t*)realptr);

    //统计使用的内存大小(减少)
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);

    //释放内存
    free(realptr);
}

