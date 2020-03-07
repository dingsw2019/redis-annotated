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

zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    x = zsl->header;
    // 遍历层
    for(i=zsl->level-1; i>=0; i--){

        // 设置已跳过的节点数
        // 下面只循环 x ,所以之前楼层跳过的节点,不会再遍历
        // 所以这里直接赋值上面跳过的节点,在此基础上,再查找是否存在跳过的节点
        rank[i] = (i == (zsl->level-1)) ? 0 : rank[i+1];

        // 遍历节点,查找可添加的位置
        // 1.按分值查找,跳过小于 score 的节点
        // 2.如果 score 相同,跳过成员值小的
        while(x->level[i].forward && 
                // 比对分值
               (x->level[i].forward->score < score ||
                // 比对成员
                (x->level[i].forward->score == score && 
                 compareStringObj(x->level[i].forward->obj,obj)<0
             ))
        )
        {
            // 更新跳过节点数
            rank[i] += x->level[i].span;
            // 处理下一个节点
            x = x->level[i].forward;
        }

        // 记录可添加的节点位置之前的节点
        update[i] = x;
    }

    // 生成随机层
    level = zslRandomLevel();

    // 初始化未设置的层
    if (level > zsl->level) {
        for(i=zsl->level; i<level; i++){
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }

        // 更新跳跃表最大层数
        zsl->level = level;
    }

    // 创建新节点
    x = zslCreateNode(level,score,obj);

    // 设置层属性
    for (i=0; i<level; i++){

        // 更新"新节点的前进指针"指向
        x->level[i].forward = update[i]->level[i].forward;

        // 更新"新节点的前一个节点的前进指针"指向
        update[i]->level[i].forward = x;

        // 更新"新节点"的跨度
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);

        // 更新"新节点的前一个节点"的跨度
        // 其中 +1 是新节点的跨度
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    // 更新"未接触的节点"的 span 值
    // 比如:level 小于 zsl->level 可能会出现未接触节点
    // 但是添加节点后,原先span就少了,所以要更新
    for (i = level; i < zsl->level; i++) {
        update[i]->level[i].span++;
    }

    // 设置"新节点"的后退指针
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    // 更新"新节点的下一个节点"的后退指针的指向
    if (x->level[0].forward)
        x->level[0].forward->backward = x;
    // 如果"新节点"是最后一个节点,更新跳跃表的尾节点指针指向
    else
        zsl->tail = x;

    // 更新节点数
    zsl->length++;

    return x;
}