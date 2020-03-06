#include <stdlib.h>
#include <time.h>
#include "zmalloc.h"
#include "redis.h"

// 创建一个指定层数的跳跃表节点
zskiplistNode *zslCreateNode(int level, double score, robj *obj)
{
    // 申请内存
    zskiplistNode *zn = zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));
    // 设置属性
    zn->score = score;
    zn->obj = obj;

    return zn;
}


// 创建一个跳跃表
zskiplist *zslCreate()
{
    int j;
    // 申请内存
    zskiplist *zsl = zmalloc(sizeof(*zsl));

    // 设置跳跃表属性
    zsl->level = 1;
    zsl->length = 0;

    // 申请首节点
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);

    // 首节点的前进指针和跨度赋值
    for(j=0; j<ZSKIPLIST_MAXLEVEL; j++){
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }
    zsl->header->backward = NULL;

    // 尾节点赋值
    zsl->tail = NULL;

    // 返回
    return zsl;
}

/**
 * 释放给定跳跃表节点
 * 
 * T = O(1)
 */
void zslFreeNode(zskiplistNode *node)
{
    // 更新引用计数,如果引用计数为0,再释放实际值的内存
    // decrRefcount(node->obj);

    // 释放节点
    zfree(node);
}

/**
 * 释放给定跳跃表
 * 
 * T = O(N)
 */
void zslFree(zskiplist *zsl)
{
    zskiplistNode *node = zsl->header->level[0].forward, *next;
    
    // 释放首节点
    zfree(zsl->header);

    // 释放所有节点
    while(node){
        next = node->level[0].forward;
        zslFreeNode(node);
        node = next;
    }

    // 释放跳跃表
    zfree(zsl);
}

// 生成随机值
int customRandom()
{
    srand(time(NULL));
    return rand();
}

/**
 * 返回一个随机值，用作新跳跃表节点的层数
 * 
 * 返回值介于 1 和 ZSKIPLIST_MAXLEVEL 之间 (包含ZSKIPLIST_MAXLEVEL)
 * 根据随机算法所使用的幂次定律，越大的值生成的几率越小
 *
 * T = O(N)
 */
int zslRandomLevel(void)
{
    int level = 1;
    // 随机值小于 1/4 的int值, level增加
    while((customRandom()&0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        level += 1;

    return (level<ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}