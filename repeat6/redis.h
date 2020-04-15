#ifndef __REDIS_H
#define __REDIS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
// #include <syslog.h>
// #include <netinet/in.h>
// #include <lua.h>
#include <signal.h>

#include "sds.h"
#include "dict.h"
#include "adlist.h"
#include "zmalloc.h"
#include "ziplist.h"
#include "intset.h"
#include "util.h"

#define REDIS_OK 0
#define REDIS_ERR -1

// 对象类型
#define REDIS_STRING 0
#define REDIS_LIST 1
#define REDIS_SET 2
#define REDIS_ZSET 3
#define REDIS_HASH 4

// 对象编码
#define REDIS_ENCODING_RAW 0
#define REDIS_ENCODING_INT 1
#define REDIS_ENCODING_HT 2
#define REDIS_ENCODING_ZIPMAP 3
#define REDIS_ENCODING_LINKEDLIST 4
#define REDIS_ENCODING_ZIPLIST 5
#define REDIS_ENCODING_INTSET 6
#define REDIS_ENCODING_SKIPLIST 7
#define REDIS_ENCODING_EMBSTR 8

// 报错
#define redisAssertWithInfo(_c, _o, _e) _exit(1)
#define redisAssert(_e) _exit(1)
#define redisPanic(_e) _exit(1)

// LRU
#define REDIS_LRU_BITS 24
#define REDIS_LRU_CLOCK_MAX ((1<<REDIS_LRU_BITS)-1)
#define REDIS_LRU_CLOCK_RESOLUTION 1000
#define REDIS_SHARED_BULKHDR_LEN 32

// redis对象结构
typedef struct redisObject {

    // 类型
    unsigned type:4;

    // 编码
    unsigned encoding:4;

    // LRU
    unsigned lru:REDIS_LRU_BITS;

    int refcount;

    // 值指针
    void *ptr;

} robj;

#define LRU_CLOCK() 1


#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

// 跳跃表节点
typedef struct zskiplistNode{
    sds ele;
    double score;
    struct zskiplistNode *backward;
    struct zskiplistLevel {
        struct zskiplistNode *forward;
        unsigned int span;
    } level[];
} zskiplistNode;


// 跳跃表
typedef struct zskiplist {
    struct zskiplistNode *header, *tail;
    unsigned long length;
    int level;

} zskiplist;

// 范围搜索器
typedef struct zrangespec {

    double min, max;

    // 1 表示不包含 , 0 包含
    int minex, maxex;
} zrangespec;

#endif