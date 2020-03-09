#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zmalloc.h"
#include "sds.h"

#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 2.5

// 跳跃表节点
typedef struct zskiplistNode 
{
    // 成员
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
    // 指向头尾节点的指针
    zskiplistNode *header, *tail;
    // 最大层数
    int level;
    // 节点数量
    unsigned long length;
} zskiplist;

// 跳跃表搜索条件
typedef struct zrangespec 
{
    // 最小/最大值
    double min, max;
    // 开闭合 1 表示不包含, 0 表示包含
    int minex, maxex;
} zrangespec;
