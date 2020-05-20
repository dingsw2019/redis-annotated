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

struct sharedObjectsStruct shared;

struct redisServer server; 

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

dictType hashDictType = {
    NULL,            /* dictEncObjHash hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    NULL,      /* dictEncObjKeyCompare key compare */
    NULL,                      /* dictRedisObjectDestructor key destructor */
    NULL                       /* val destructor */
};

/* Keylist hash table type has unencoded redis objects as keys and
 * lists as values. It's used for blocking operations (BLPOP) and to
 * map swapped keys to a list of clients waiting for this keys to be loaded. */
dictType keylistDictType = {
    NULL,            /* dictEncObjHash hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    NULL,      /* dictEncObjKeyCompare key compare */
    NULL,                      /* dictRedisObjectDestructor key destructor */
    NULL                       /* val destructor */
};

/* Cluster nodes hash table, mapping nodes addresses 1.2.3.4:6379 to
 * clusterNode structures. */
dictType clusterNodesDictType = {
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

void updateDictResizePolicy(void) {
    if (server.rdb_child_pid == -1 && server.aof_child_pid == -1)
        dictEnableResize();
    else
        dictDisableResize();
}