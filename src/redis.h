#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zmalloc.h"
#include "sds.h"


#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

// 跳跃表节点 (level必须放在最后一个)
typedef struct zskiplistNode 
{
    // 对象成员
    sds ele;

    // 分值
    double score;

    // 后退指针
    struct zskiplistNode *backward;

    // 层
    struct zskiplistLevel {
        // 前进指针
        struct zskiplistNode *forward;
        // 跨度
        unsigned int span;
    } level[];

} zskiplistNode;

// 跳跃表
typedef struct zskiplist
{
    // 指向首尾节点的指针
    struct zskiplistNode *header, *tail;
    // 节点数量
    unsigned long length;
    // 最大层数的节点的层数
    int level;
} zskiplist;

// 表示开区间/闭区间范围的结构
typedef struct 
{
    // 最小值和最大值
    double min, max;

    // 指示最小值和最大值是否不包含在范围内
    // 值为 1 表示不包含, 值为 0 表示包含
    int minex, maxex;
} zrangespec;

// LRU 是Least Recently Used的缩写
// 即最近最少使用，是一种常用的页面置换算法，选择最近最久未使用的页面予以淘汰。 
#define REDIS_LRU_BITS 24

// redis对象
typedef struct redisObject {

    // 对象类型(从使用者角度定义的类型)
    unsigned type:4;

    // 值对象的编码方式
    unsigned encoding:4;

    // 对象最后一次呗访问的时间
    unsigned lru:REDIS_LRU_BITS;

    // 引用计数
    int refcount;

    // 指向实际指针
    void *ptr;
} redisObject;