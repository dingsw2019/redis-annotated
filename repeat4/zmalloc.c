#include <stdio.h>
#include <string.h>
#include <pthread.h>

//每个块的头信息大小,用来存放块的内存大小
//块分为头信息大小和结构体大小，两部分。
#define PREFIX_SIZE sizeof(size_t)

//线程安全的更新内存使用量,线程互斥锁
#define update_zmalloc_stat_add(_n) do{ \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += _n; \
    pthread_mutex_unlock(&used_memory_mutex); \
}while(0)

//统计内存使用量,n不是4或8的倍数,补足剩余数量.
#define update_zmalloc_stat_alloc(__n) do{ \
    size_t _n = (__n); \
    if(_n&(sizeof(long)-1)) _n += sizeof(long) - (_n&(sizeof(long)-1)); \
    if(zmalloc_thread_safe){ \
        update_zmalloc_stat_add(_n); \
    }else{ \
        used_memory += _n; \
    } \
}while(0)

//内存溢出,默认异常抛出方法
static void zmalloc_default_oom(size_t size) {
    fprintf(stderr,"zmalloc:out of memory trying to allocate %lu bytes\n",size);
    fflush(stderr);
    abort();
}

static size_t used_memory = 0;
static size_t zmalloc_thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

//内存溢出调用的自定义方法,可根据用户喜好自定义使用
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

//申请内存空间
void *zmalloc(size_t size){
    void *ptr = malloc(size+PREFIX_SIZE);
    if(!ptr) zmalloc_oom_handler(size);

    //头信息,存储结构体大小
    *((size_t*)ptr) = size;
    //更新内存使用量
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    //返回sdshdr结构体
    return (char*)ptr+PREFIX_SIZE;
}

