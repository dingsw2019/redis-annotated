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

// 添加跳跃表节点
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    // 起始节点
    x = zsl->header;

    // 自上向下遍历层,逐层查找符合添加条件的位置
    for (i=zsl->level-1; i>=0; i++) {

        // 跳过的节点数,下层会继承上层跳过的节点数
        rank[i] = (i == (zsl->level-1)) ? 0 : rank[i+1];

        // 符合已下条件, 继续遍历节点
        // 1. 下一个节点的 score 小于传入 score
        // 2. 下一个节点的 score 等于传入 score, obj 小于传入 obj
        while (x->level[i].forward && 
                (x->level[i].forward->score < score ||
                (x->level[i].forward->score ==score &&
                    compareStringObject(x->level[i].forward->obj,obj)))) 
        {
            // 更新跳过节点数
            rank[i] += x->level[i].span;
            // 处理下一个节点
            x = x->level[i].forward;
        }

        // 遍历节点完成,记录待插入节点的前一个节点
        update[i] = x;
    }

    // 生成随机层数
    level = zslRandomLevel();

    // 初始化未设置层的属性
    if (level > zsl->level) {
        
        for (i=zsl->level; i<level; i++) {

            // 跳过 0 个节点
            rank[i] = 0;
            // 新节点的前一个节点为首节点
            update[i] = zsl->header;
            // span 为跳跃表节点数
            update[i]->level[i].span = zsl->length;
        }

        // 更新跳跃表最大层
        zsl->level = level;
    }

    // 创建新节点
    x = zslCreateNode(level,score,obj);

    // 设置新节点各层的属性
    for (i=0; i<level; i++) {

        // 更新"新节点"的前进指针指向
        x->level[i].forward = update[i]->level[i].forward;

        // 更新"新节点前一个节点"的前进指针指向
        update[i]->level[i].forward = x;

        // 更新"新节点"的跨度
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);

        // 更新"新节点前一个节点"的跨度
        update[i]->level[i].span = (rank[0] - rank[1]) + 1;
    }

    // 未设置层,跨度增加
    for (i=level; i<zsl->level; i++) {
        update[i]->level[i].span++;
    }

    // 设置"新节点"的后退指针指向
    x->backward = (update[0] == zsl->header) ? NULL : update[0];

    // 更新"新节点下一个节点"的后退指针指向
    if (x->level[0].forward)
        x->level[0].forward->backward = x;
    // 跳跃表尾节点是否更新
    else 
        zsl->tail = x;

    // 更新跳跃表节点数量
    zsl->length++;

    return x;
}