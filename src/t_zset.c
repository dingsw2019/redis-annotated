#include <stdlib.h>
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