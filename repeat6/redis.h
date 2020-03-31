#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zmalloc.h"
#include "sds.h"

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
typedef struct zskiplistspec {

    double min, max;

    int minex, maxex;
} zskiplistspec;
