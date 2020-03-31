#include "redis.h"

/*--------------------- private ---------------------*/
// 获取随机层数
int zslRandomLevel(void) {
    int level = 1;
    // 随机数大于 1/4的 int 值
    while((rand() & 0xFFFF) < (ZSKIPLIST_P * 0xFFFF)) {
        level++;
    }

    return (level < ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

// value 大于范围的最小值
static int zslValueGteMin(double value, zrangespec *spec) {

    return (spec->minex) ? (value > spec->min) : (value >= spec->min);
}

// value 小于范围的最大值
static int zslValueLteMax(double value, zrangespec *spec) {
    return (spec->maxex) ? (value < spec->max) : (value <= spec->max);
}

// 跳跃表的值是否在取值范围内
// 符合返回 1, 不符合返回 0
int zslIsInRange(zskiplist *zsl, zrangespec *range) {

    zskiplistNode *x;
    // range 的范围正确性
    if (range->min > range->max || 
        (range->min == range->max && (range->maxex || range->minex)))
    {
        return 0;
    }

    // zsl最大值小于range的最小值
    x = zsl->tail;
    if (x == NULL || !zslValueGteMin(x->score, range)) {
        return 0;
    }

    // zsl最小值大于range的最大值
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score, range)) {
        return 0;
    }

    return 1;
}

/*--------------------- API ---------------------*/

// 创建并返回节点
zskiplistNode *zslCreateNode(int level, double score, sds ele) {

    // 申请内存空间
    zskiplistNode *zn = zmalloc(sizeof(*zn) + level*sizeof(struct zskiplistLevel));

    // 设置属性
    zn->score = score;
    zn->ele = ele;

    return zn;
}

// 创建并返回一个空跳跃表
zskiplist *zslCreate(void) {

    // 申请内存
    int i;
    zskiplist *zsl = zmalloc(sizeof(*zsl));

    // 初始化跳跃表属性
    zsl->level = 1;
    zsl->length = 0;

    // 申请 header 节点
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);
    // 初始化 header 节点的 level
    for(i=0; i<ZSKIPLIST_MAXLEVEL; i++) {
        zsl->header->level[i].forward = NULL;
        zsl->header->level[i].span = 0;
    }

    // 设置 header 节点的后退指针
    zsl->header->backward = NULL;

    // 尾节点
    zsl->tail = NULL;

    // 返回
    return zsl;
}

// 释放跳跃表节点
void zslFreeNode(zskiplistNode *node) {

    sdsfree(node->ele);
    zfree(node);
}

// 释放跳跃表
void zslFree(zskiplist *zsl) {

    zskiplistNode *x,*next;
    x = zsl->header->level[0].forward;
    // 释放首节点
    zfree(zsl->header);

    // 遍历释放节点
    while (x) {
        next = x->level[0].forward;
        zslFreeNode(x);
        x = next;
    }

    // 释放跳跃表
    zfree(zsl);
}

// 删除节点 x
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update) {

    int i;
    // 层修改
    for (i=0; i<zsl->level; i++) {

        if (update[i]->level[i].forward == x) {

            // 跨度
            update[i]->level[i].span += x->level[i].span - 1;
            // 前进指针
            update[i]->level[i].forward = x->level[i].forward;
        } else {
            update[i]->level[i].span -= 1;   
        }
    }

    // 后退指针修改
    if (x->level[0].forward) {

        x->level[0].forward->backward = x->backward;
    } else {
        zsl->tail = x->backward;
    }

    // 尝试更新最大层数
    while (zsl->level>1 && zsl->header->level[zsl->level-1].forward == NULL) 
        zsl->level--;

    // 更新节点数
    zsl->length--;
}

// 通过 score 和 ele查找节点并删除
// 删除成功返回 1, 否则返回 0
int zslDelete(zskiplist *zsl, double score, sds ele, zskiplistNode **node) {

    zskiplistNode *update[ZSKIPLIST_MAXLEVEL],*x;
    int i;

    // 查找节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && 
                (x->level[i].forward->score < score || 
                 (x->level[i].forward->score == score && 
                    sdscmp(x->level[i].forward->ele,ele)<0 )))
        {
            x = x->level[i].forward;
        }

        update[i] = x;
    }

    // 目标节点
    x = x->level[0].forward;

    // 删除节点
    if (x && x->score == score && sdscmp(x->ele, ele)==0) {

        zslDeleteNode(zsl, x, update);
        
        // 未设置 node, 释放节点
        if (!node) 
            zslFreeNode(x);
        else 
            *node = x;
        return 1;
    }

    return 0;
}

// 添加节点, 添加成功返回节点, 否则返回 NULL
zskiplistNode *zslInsert(zskiplist *zsl, double score, sds ele) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    // 记录每一个层适合添加新节点的前一个节点
    x = zsl->header;
    for(i=zsl->level-1; i>=0; i--) {

        // 获取上层已经完成的跨度
        rank[i] = (i == (zsl->level-1)) ? 0 : rank[i+1];

        while (x->level[i].forward && 
                (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score
                 && sdscmp(ele, x->level[i].forward->ele)<0))) 
        {
            rank[i] += x->level[i].span;
            x = x->level[i].forward;
        }

        update[i] = x;
    }

    // 随机层数
    level = zslRandomLevel();

    // 超出最大层数的层, 准备配置信息
    if (level > zsl->level) {
        for(i=zsl->level; i<level; i++) {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }

        zsl->level = level;
    }

    // 创建新节点
    x = zslCreateNode(level, score, ele);

    // 设置新节点的前进指针和跨度
    for (i=0; i<level; i++) {

        // 新节点的前进指针
        x->level[i].forward = update[i]->level[i].forward;

        // 新节点的后置节点的前进指针指向新节点
        update[i]->level[i].forward = x;

        // 新节点的跨度
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);

        // 新节点的后置节点的跨度
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    // 更新大于新节点最大层数的层数的跨度
    for(i=level; i<zsl->level; i++) {
        update[i]->level[i].span++;
    }

    // 设置新节点的后退指针
    x->backward = (update[0] == zsl->header) ? NULL : update[0];

    // 设置新节点的后置节点的后退指针
    // 或者跳跃表的尾节点
    if (x->level[0].forward) {
        x->level[0].forward->backward = x;
    } else {
        zsl->tail = x;
    }

    // 更新节点数量
    zsl->length++;

    // 返回
    return x;
}







// 返回符合搜索条件的第一个节点
// 如果不存在返回 NULL
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range) {

    zskiplistNode *x;
    int i;
    // 检查 range 是否符合 zsl 的值范围
    if (!zslIsInRange(zsl, range))
        return NULL;

    // 查找大于最小值的前置节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        
        while (x->level[i].forward && 
            !zslValueGteMin(x->level[i].forward->score,range)) 
        {
            x = x->level[i].forward;    
        }
    }

    // x 是符合搜索条件的前置节点
    x = x->level[0].forward;

    // 确认节点值小于最大值
    if (!x || !zslValueLteMax(x->score, range)) {
        return NULL;
    }

    // 返回节点
    return x;
}

// 返回符合搜索条件的最后一个节点
// 未找到返回 NULL
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range) {
    
    zskiplistNode *x;
    int i;

    // 检查搜索条件是否符合 zsl
    if (!zslIsInRange(zsl, range))
        return NULL;

    // 遍历获取第一个小于搜索条件最大值的节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward &&
                zslValueLteMax(x->level[i].forward->score, range)) 
        {
            x = x->level[i].forward;
        }
    }

    // x 节点不可小于搜索条件的最小值
    if (x == NULL || !zslValueGteMin(x->score, range))
        return NULL;

    // 返回
    return x;
}

// 通过分值和对象查找到节点, 并返回节点的索引
// 未找到节点, 返回 0
unsigned long zslGetRank(zskiplist *zsl, double score, sds ele) {
    zskiplistNode *x;
    int i, rank = 0;

    // 跳跃查找目标节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && 
                (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score && sdscmp(x->level[i].forward->ele,ele) <= 0))) 
        {
            rank += x->level[i].span;

            x = x->level[i].forward;
        }

        if (x->ele && sdscmp(x->ele,ele)==0) {
            return rank;
        }
    }

    // 未找到节点
    return 0;
}

// 通过索引返回节点
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank) {

    zskiplistNode *x;
    unsigned long traversed = 0;
    int i;

    // 查找目标节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && (traversed + x->level[i].span) <= rank) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }

        if (traversed == rank) {
            return x;
        }
    }

    return NULL;
}

// 删除指定范围的节点
// 返回删除节点数量
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;

    // 找到要删除的起始节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && (range->minex ? 
                    x->level[i].forward->score <= range->min :
                    x->level[i].forward->score < range->min))
        {
            x = x->level[i].forward;
        }

        update[i] = x;
    }

    // 遍历删除节点
    x = x->level[0].forward;
    while (x && zslValueLteMax(x->score, range)) {
        zskiplistNode *next = x->level[0].forward;
        zslDeleteNode(zsl,x,update);
        zslFreeNode(x);
        removed++;
        x = next;
    }

    return removed;
}

// 删除索引范围内的节点
// 返回删除节点数
unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end) {

    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long traversed = 0, removed = 0;
    int i;
    // 查找起始节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && (x->level[i].span + traversed)<start) {

            traversed += x->level[i].span;
            x = x->level[i].forward;
        }

        update[i] = x;
    }

    // 删除节点
    traversed++;
    x = x->level[0].forward;
    while (x && traversed <= end) {

        zskiplistNode *next = x->level[0].forward;
        zslDeleteNode(zsl, x, update);
        zslFreeNode(x);
        removed++;
        traversed++;
        x = next;
    }

    return removed;
}

//gcc -g zmalloc.c sds.c t_zset.c
int main(void) {

    srand((unsigned)time(NULL));

    unsigned long ret;
    zskiplistNode *node;
    zskiplist *zsl = zslCreate();

    // 添加节点
    zslInsert(zsl, 65.5, sdsnew("tom"));     // 3
    zslInsert(zsl, 69.5, sdsnew("wangwu"));  // 4
    zslInsert(zsl, 87.5, sdsnew("jack"));    // 6
    zslInsert(zsl, 20.5, sdsnew("zhangsan"));// 1
    zslInsert(zsl, 70.0, sdsnew("alice"));   // 5
    zslInsert(zsl, 39.5, sdsnew("lisi"));    // 2
    zslInsert(zsl, 95.0, sdsnew("tony"));    // 7

    //定义一个区间， 70.0 <= x <= 90.0
    zrangespec range1 = {       
        .min = 70.0,
        .max = 90.0,
        .minex = 0,
        .maxex = 0
    };

    // 找到符合区间的最小值
    printf("zslFirstInRange 70.0 <= x <= 90.0, x is:");
    node = zslFirstInRange(zsl, &range1);
    printf("%s->%f\n", node->ele, node->score);

    // 找到符合区间的最大值
    printf("zslLastInRange 70.0 <= x <= 90.0, x is:");
    node = zslLastInRange(zsl, &range1);
    printf("%s->%f\n", node->ele, node->score);

    // 根据分数获取排名
    printf("tony's Ranking is :");
    ret = zslGetRank(zsl, 95.0, sdsnew("tony"));
    printf("%lu\n", ret);

    // 根据排名获取分数
    printf("The Rank equal 4 is :");
    node = zslGetElementByRank(zsl, 4);
    printf("%s->%f\n", node->ele, node->score);

    // 分值范围删除节点
    zrangespec range2 = {       
        .min = 20.0,
        .max = 40.0,
        .minex = 0,
        .maxex = 0
    };
    ret = zslDeleteRangeByScore(zsl,&range2);
    printf("zslDeleteRangeByScore 20.0 <= x <= 40.0, removed : %d\n",ret);

    // 索引范围删除节点
    // ret = zslDeleteRangeByRank(zsl,1,2);
    // printf("zslDeleteRangeByRank 1 & 2 index, removed : %d\n",ret);

    ret = zslDelete(zsl, 70.0, sdsnew("alice"), &node);  // 删除元素
    if (ret == 1) {
        printf("Delete node:%s->%f success!\n", node->ele, node->score);
    }
    
    return 0;
}