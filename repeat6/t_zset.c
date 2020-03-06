#include <stdlib.h>
#include "zmalloc.h"
#include "redis.h"

// 创建并返回一个指定层数的跳跃表节点
zskiplistNode *zslCreateNode(int level,double score,robj *obj)
{
    // 申请内存
    zskiplistNode *zn = zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));

    // 初始化属性
    zn->score = score;
    zn->obj = obj;

    return zn;
}

// 创建并返回一个空跳跃表
zskiplist *zslCreate()
{
    zskiplist *zsl;
    int j;

    // 申请内存
    zsl = zmalloc(sizeof(*zsl));

    // 创建首节点
    zsl->level = 1;
    zsl->length = 0;

    // 初始化跳跃表属性
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);
    // 初始化首节点的层的属性
    for(j=0; j<ZSKIPLIST_MAXLEVEL; j++){
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }

    // 初始化首节点的后退指针
    zsl->header->backward = NULL;

    // 尾节点
    zsl->tail = NULL;

    // 返回跳跃表
    return zsl;
}

// 释放跳跃表节点
void zslFreeNode(zskiplistNode *node)
{
    // 更新引用计数
    dectRefcount(node->obj);
    // 释放节点
    zfree(node);
}

// 释放跳跃表
void zslFree(zskiplist *zsl)
{   
    zskiplistNode *node = zsl->header->level[0].forward, *next;
    // 释放首节点
    zfree(zsl->header);
    // 遍历释放其他节点
    while(node){
        next = node->level[0].forward;
        zfree(node);
        node = next;
    }

    // 释放跳跃表
    zfree(zsl);
}

// 随机数
int customRandom()
{
    srand(time(NULL));
    return rand();
}

// 获取随机层数,幂等
int zslRandomLevel()
{
    int level = 1;

    while((customRandom()&0xFFFF) < (ZSKIPLIST_P*0xFFFF))
        level++;

    return (level<ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}