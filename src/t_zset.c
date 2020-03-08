#include "redis.h"

// 创建一个指定层数的跳跃表节点
zskiplistNode *zslCreateNode(int level, double score, sds ele)
{
    // 申请内存
    zskiplistNode *zn = zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));
    // 设置属性
    zn->score = score;
    zn->ele = ele;

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

    // 释放实际值
    sdsfree(node->ele);

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
    while((rand() & 0xFFFF) < (ZSKIPLIST_P * 0xFFFF)){
        level += 1;
    }

    return (level<ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

/**
 * 添加并返回跳跃表新节点
 */
zskiplistNode *zslInsert(zskiplist *zsl, double score, sds ele)
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
                (x->level[i].forward->score == score 
                 && sdscmp(x->level[i].forward->ele,ele)<0 )))
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
    x = zslCreateNode(level,score,ele);

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

/**
 * 删除指定节点 x
 * 
 * T = O(1)
 */
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update)
{   
    int i;
    // 更新 x 的前一个节点的跨度和前进指针指向
    for (i=0; i<zsl->level; i++) {
        // 当前层与 x 有接触, 更新跨度和指针指向
        if (update[i]->level[i].forward == x) {
            // 更新跨度
            update[i]->level[i].span += x->level[i].span - 1;
            // 更新指针
            update[i]->level[i].forward = x->level[i].forward;
        } 
        // 当前层未与 x 接触, 只更新跨度
        else {
            // 更新跨度
            update[i]->level[i].span -= 1;
        }
    }

    // 更新 x 的下一个节点的后退指针指向
    if (x->level[0].forward)
        x->level[0].forward->backward = x->backward;
    // 如果 x 是尾节点, 更新尾节点
    else 
        zsl->tail = x->backward;

    // 尝试更新最大层数
    while(zsl->level > 1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;

    // 更新节点数
    zsl->length--;
}

/**
 * 在哈希表中删除 score 和 ele 相同的节点
 * 
 * 删除成功, 返回 1
 * 删除失败或未找到, 返回 0
 * 
 * T = O(N)
 */
int zslDelete(zskiplist *zsl, double score, sds ele, zskiplistNode **node)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    // 初始化头节点
    x = zsl->header;

    // 逐层查找目标节点,并记录跨度和目标节点的前一个节点
    for (i=zsl->level-1; i>=0; i--) {

        // 遍历节点查找目标节点
        while(x->level[i].forward &&
                (x->level[i].forward->score < score ||
                 (x->level[i].forward->score == score && 
                  sdscmp(x->level[i].forward->ele,ele) < 0 )))
        {
            // 处理下一个节点
            x = x->level[i].forward;
        }

        // 记录目标节点的前一个节点
        update[i] = x;
    }

    // x 是目标节点的前一个节点, 跳到目标节点
    x = x->level[0].forward;

    // 确认 x 是目标节点
    if (x && x->score == score && sdscmp(x->ele,ele)==0) {
        // 删除节点
        zslDeleteNode(zsl,x,update);
        // 未设置 node ,释放节点
        if (!node)
            zslFreeNode(x);
        // 设置 node, 不释放节点, 并赋值给 node
        else 
            *node = x;

        return 1;
    }

    // 未找到
    return 0;
}

/**
 * 检查给定值 value 是否大于或大于等于, 搜索条件(spec)的最小值(min)
 * 
 * 大于等于 min 返回 1, 否则返回 0
 */
static int zslValueGteMin(double value,zrangespec *spec)
{
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}

/**
 * 检查给定值 value 是否小于或小于等于, 搜索条件(spec)的最大值(max)
 * 
 * 小于等于 max 返回 1 , 否则返回 0
 */
static int zslValueLteMax(double value, zrangespec *spec)
{
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

/**
 * 检查搜索条件(spec) 能否正确查询
 * 
 * 正确查询条件, 返回 1
 * 错误查询条件, 返回 0
 */
int zslIsInRange(zskiplist *zsl, zrangespec *range) 
{
    zskiplistNode *x;
    // 检查搜索条件(spec)正确性
    if (range->min > range->max ||
        (range->min == range->max && (range->maxex || range->minex)))
        return 0;

    // 超范围检查(zsl 最大值与 spec 最小值)
    x = zsl->tail;
    if (x == NULL || !zslValueGteMin(x->score,range))
        return 0;

    // 超范围检查(zsl 最小值与 spec 最大值)
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score,range))
        return 0;

    // 搜索条件(spec) 可正常查询
    return 1;
}

//gcc -g zmalloc.c sds.c t_zset.c
int main(void) {

    srand((unsigned)time(NULL));

    unsigned long ret;
    zskiplistNode *node;
    zskiplist *zsl = zslCreate();


    zslInsert(zsl, 65.5, sdsnew("tom"));    //level = 1
    zslInsert(zsl, 87.5, sdsnew("jack"));   //level = 4
    zslInsert(zsl, 70.0, sdsnew("alice"));  //level = 3
    zslInsert(zsl, 95.0, sdsnew("tony"));   //level = 2

    ret = zslDelete(zsl, 70.0, sdsnew("alice"), &node);  // 删除元素
    if (ret == 1) {
        printf("Delete node:%s->%f success!\n", node->ele, node->score);
    }
    
    return 0;
}