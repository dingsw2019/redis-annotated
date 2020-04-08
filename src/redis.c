#include "redis.h"
// #include "cluster.h"
// #include "slowlog.h"
// #include "bio.h"

#include <time.h>
#include <signal.h>
// #include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
// #include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
// #include <sys/resource.h>
// #include <sys/uio.h>
#include <limits.h>
#include <float.h>
#include <math.h>
// #include <sys/resource.h>
// #include <sys/utsname.h>
#include <locale.h>

dictType setDictType = {
    NULL,            /* dictEncObjHash hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    NULL,      /* dictEncObjKeyCompare key compare */
    NULL,                      /* dictRedisObjectDestructor key destructor */
    NULL                       /* val destructor */
};

dictType zsetDictType = {
    NULL,            /* dictEncObjHash hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    NULL,      /* dictEncObjKeyCompare key compare */
    NULL,                      /* dictRedisObjectDestructor key destructor */
    NULL                       /* val destructor */
};

/**
 * 返回微秒格式的 UNIX 时间
 * 1 秒 = 1 000 000 微妙
 */
long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}

/**
 * 返回毫秒格式的 UNIX 时间
 * 1 秒 = 1 000 毫秒
 */
long long mstime(void) {
    return ustime()/1000;
}

// 获取 LRU 时间
unsigned int getLRUClock(void) {
    return (mstime()/REDIS_LRU_CLOCK_RESOLUTION) & REDIS_LRU_CLOCK_MAX;
}