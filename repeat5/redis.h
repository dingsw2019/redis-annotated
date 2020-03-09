#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zmalloc.h"
#include "sds.h"

#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

// 跳跃表节点
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
    // 首尾节点指针
    struct zskiplistNode *header, *tail;
    // 最大层数
    int level;
    // 节点数
    unsigned long length;
} zskiplist;

// 跳跃表搜索条件
typedef struct zrangespec 
{
    // 范围值
    double min,max;
    // 开闭合, 1不包含边界值, 0包含边界值
    int minex,maxex;
} zrangespec;