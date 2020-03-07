#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zmalloc.h"
#include "sds.h"


#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

#define REDIS_LRU_BITS 24

// redis 对象
// typedef struct redisObject
// {
//     // 类型
//     unsigned type:4;
    
//     // 编码
//     unsigned encoding:4;

//     // 对象最后一次被访问的时间
//     unsigned lru:REDIS_LRU_BITS;

//     // 引用次数
//     int refcount;

//     // 指向实际值的指针
//     void *ptr;
// } robj;


// // 跳跃表节点
// typedef struct zskiplistNode 
// {
//     // 对象成员
//     sds ele;

//     // 分值
//     double score;

//     // 后退指针
//     struct zskiplistNode *backward;

//     // 层
//     struct zskiplistLevel {
//         // 前进指针
//         struct zskiplistNode *forward;
//         // 跨度
//         unsigned int span;
//     } level[];

// } zskiplistNode;

// // 跳跃表
// typedef struct zskiplist
// {
//     // 指向首尾节点的指针
//     struct zskiplistNode *header, *tail;
//     // 节点数量
//     unsigned long length;
//     // 最大层数的节点的层数
//     int level;
// } zskiplist;


typedef struct zskiplistNode {
    sds ele;
    double score;
    struct zskiplistNode *backward;
    struct zskiplistLevel {
        struct zskiplistNode *forward;
        unsigned int span;
    } level[];
} zskiplistNode;

typedef struct zskiplist {
    struct zskiplistNode *header, *tail;
    unsigned long length;
    int level;
} zskiplist;