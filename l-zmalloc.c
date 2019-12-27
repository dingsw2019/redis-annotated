#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

//8 = sizeof(size_t)
#define PREFIX_SIZE (sizeof(size_t))

//加锁完成,加操作
//加数的过程: 1.从缓存放到寄存器; 2.在寄存器+1; 3.存入缓存
//3个动作,需要加锁完成
#define update_zmalloc_stat_add(__n) do { \
   pthread_mutex_lock(&used_memory_mutex); \
   used_memory += (__n); \
   pthread_mutex_unlock(&used_memory_mutex); \
} while(0)

//加锁完成,减操作
#define update_zmalloc_stat_sub(__n) do { \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= (__n); \
    pthread_mutex_unlock(&used_memory_mutex); \
} while(0)

//内容使用量统计,增加操作
//(_n&(sizeof(long)-1))将__n值填充至可被4整除
#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
} while(0)

//内容使用量统计,减少操作
#define update_zmalloc_stat_free(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
}while(0)

static size_t used_memory = 0;
static int zmalloc_thread_safe = 0;
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

//提示内存申请失败,并退出程序
static void zmalloc_default_oom(size_t size){
   fprintf(stderr,"zmalloc: Out of memory trying to allocate %lu bytes\n",
      size);
   fflush(stderr);
   abort();
}

//todo ? 为什么要这样调用
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

//以malloc申请空间,不初始化内容
void *zmalloc(size_t size){
   void *ptr = malloc(size+PREFIX_SIZE);

   if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
   //todo 待完成
   // update_zmalloc_stat_alloc()
   return ptr;
#else
   *((size_t*)ptr) = size;
   update_zmalloc_stat_alloc(size+PREFIX_SIZE);
   return (char*)ptr+PREFIX_SIZE;
#endif
}

//以calloc申请空间,初始化内容,为0
void *zcalloc(size_t size){
    void *ptr = calloc(1,size+PREFIX_SIZE);

    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
   //todo 待完成
   // update_zmalloc_stat_alloc()
   return ptr;
#else
   *((size_t*)ptr) = size;
   update_zmalloc_stat_alloc(size+PREFIX_SIZE);
   return (char*)ptr+PREFIX_SIZE;
#endif
}

void zfree(void *ptr){
    void *realptr;
    size_t oldsize;

    if (ptr == NULL) return;

    //ptr指针向前偏移8字节,回退到malloc返回的地址
    realptr = (char*)ptr-PREFIX_SIZE;
    //先类型转换再取指针所指向的值,取的是ptr的size
    oldsize = *((size_t*)realptr);
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);
    free(realptr);
}