#include "redis.h"

/**
 * 创建并返回一个指定层数的跳跃表节点
 */
zskiplistNode *zslCreateNode(int level, double score, sds ele)
{
    // 申请节点内存空间
    zskiplistNode *zn = zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));
    // 初始化属性
    zn->score = score;
    zn->ele = ele;

    return zn;
}

/**
 * 创建并返回一个空跳跃表
 */
zskiplist *zslCreate(void)
{
    int i;
    // 申请跳跃表的内存空间
    zskiplist *zsl = zmalloc(sizeof(*zsl));

    // 初始化层数和节点数
    zsl->level = 1;
    zsl->length = 0;

    // 初始化头节点的层
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);
    for (i=0; i<ZSKIPLIST_MAXLEVEL; i++) {
        zsl->header->level[i].forward = NULL;
        zsl->header->level[i].span = 0;
    }

    // 初始化头节点的后退指针
    zsl->header->backward = NULL;

    // 初始化尾指针
    zsl->tail = NULL;

    // 返回
    return zsl;
}

int zslRandomLevel()
{
    int level = 1;
    while(rand()&0xFFFF < ZSKIPLIST_P*0xFFFF)
        level++;
    return level<ZSKIPLIST_MAXLEVEL ? level : ZSKIPLIST_MAXLEVEL;
}

// 添加节点
zskiplistNode *zslInsert(zskiplist *zsl, double score, sds ele)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i,level;

    // 自上而下遍历层
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 继承之前跳过的节点数
        rank[i] = (i == (zsl->level-1)) ? 0 : rank[i+1];

        // 找出每层待添加节点位置的前一个节点
        // 遍历节点,跳过分值小的节点
        while (x->level[i].forward &&
                (x->level[i].forward->score < score || 
                (x->level[i].forward->score == score && 
                 sdscmp(x->level[i].forward->ele,ele)<0)))
        {
            // 记录当前层跳过的节点数
            rank[i] += x->level[i].span;

            // 处理下一个节点
            x = x->level[i].forward;
        }

        // 记录当前层待添加节点位置的前一个节点
        update[i] = x;
    }

    // 获取随机层
    level = zslRandomLevel();

    // 处理超出当前最大层的层
    if (level > zsl->level) {
        // 初始化大于最大层数的层属性
        for (i=zsl->level; i<level; i++) {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }

        // 更新最大层数
        zsl->level = level;
    }

    // 创建新节点
    x = zslCreateNode(level,score,ele);

    // "新节点的"层关系处理
    for (i=0; i<level; i++) {
        // 更新"新节点"的前进指针指向
        x->level[i].forward = update[i]->level[i].forward;
        // 更新"新节点前一个节点"的前进指针指向
        update[i]->level[i].forward = x;
        // 更新"新节点"的跨度
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);
        // 更新"新节点前一个节点"的跨度
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    // 更新"未接触新节点"的节点的跨度
    for (i=level; i<zsl->level; i++) {
        update[i]->level[i].span++;
    }

    // 设置节点的后退指针
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    if (x->level[0].forward) 
        x->level[0].forward->backward = x;
    // 更新尾节点
    else 
        zsl->tail = x;

    // 更新节点数
    zsl->length++;

    return x;
}

// 删除节点
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update)
{
    int i;
    // 处理待删节点两侧节点的跨度和前进指针指向
    for (i=0; i<zsl->level; i++) {

        if (update[i]->level[i].forward == x) {
            // 更新前进指针指向
            update[i]->level[i].forward = x->level[i].forward;
            // 更新跨度
            update[i]->level[i].span = x->level[i].span - 1;
        }
        // "未接触"的节点, 更新跨度
        else {
            update[i]->level[i].span -= 1;
        }
    }


    // 更新后退指针
    if (x->level[0].forward) {
        x->level[0].forward->backward = x->backward;
    } 
    // 更新跳跃表尾节点
    else {
        zsl->tail = x->backward;
    }

    // 尝试更新最大层数
    while(zsl->level>1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;

    // 更新节点数
    zsl->length--;
}

// 释放节点
void zslFreeNode(zskiplistNode *zn)
{
    // 释放对象成员
    sdsfree(zn->ele);

    // 释放节点
    zfree(zn);
}

// 释放跳跃表
void zslFree(zskiplist *zsl)
{   
    zskiplistNode *zn = zsl->header->level[0].forward,*next;
    
    // 释放首节点
    zfree(zsl->header);

    // 遍历节点
    while (zn) {
        next = zn->level[0].forward;
        zslFreeNode(zn);
        zn = next;
    }

    // 释放跳跃表
    zfree(zsl);
}

// 查找并删除节点
// 删除成功返回 1 , 否则返回 0
int zslDelete(zskiplist *zsl, double score, sds ele,zskiplistNode **node)
{   
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;
    x = zsl->header;
    // 自上而下遍历层
    for (i=zsl->level-1; i>=0; i--) {

        // 查找目标节点前的节点
        while (x->level[i].forward && 
                (x->level[i].forward->score < score || 
                (x->level[i].forward->score == score && 
                sdscmp(x->level[i].forward->ele,ele)<0 )))
        {
            x = x->level[i].forward;
        }

        // 记录每层目标节点前的节点
        update[i] = x;
    }

    // 目标节点
    x = update[0]->level[0].forward;

    // 确认目标节点
    if (x && x->score==score && sdscmp(x->ele,ele)==0) {

        // 删除节点
        zslDeleteNode(zsl,x,update); 

        if (!node) {
            // 释放节点
            zslFreeNode(x);
        } else {
            *node = x;
        }
        // 返回
        return 1;
    }

    // 未找到
    return 0;
}

// 分值大于最小值
int zslValueGteMin(double value, zrangespec *range)
{
    return range->minex ? value > range->min : value >= range->min;
}

// 分值小于最大值
int zslValueLteMax(double value, zrangespec *range)
{
    return range->maxex ? value < range->max : value <= range->max;
}

// 检查跳跃表是否满足搜索条件
// 不满足返回 0, 满足返回 1
int zslIsInRange(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *x;
    // 搜索条件合理性检查
    if (range->min > range->max || 
        (range->min == range->max && (range->maxex || range->minex)))
        return 0;

    // 边界检查,最小的节点分值与搜索条件的最大值
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score,range))
        return 0;

    // 边界检查,最大的节点分值与搜索条件的最小值
    x = zsl->tail;
    if (x == NULL || !zslValueGteMin(x->score,range))
        return 0;

    // 返回
    return 1;
}

// 满足搜索条件的第一个节点
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *x;
    int i;

    if (!zslIsInRange(zsl,range)) return NULL;

    x = zsl->header;
    // 遍历搜索第一个大于 range 最小值的节点
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && 
                !zslValueGteMin(x->level[i].forward->score,range))
            x = x->level[i].forward;
    }

    // 目标节点
    x = x->level[0].forward;
    // 检查节点是否超过最大值
    if (x == NULL || !zslValueLteMax(x->score,range)) {
        return NULL;
    }

    return x;
}

// 满足搜索条件的最后一个节点
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range)
{   
    zskiplistNode *x;
    int i;

    if (!zslIsInRange(zsl,range)) return NULL;

    x = zsl->header;
    // 遍历查找第一个超过最大值的节点
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && 
                zslValueLteMax(x->level[i].forward->score,range))
            x = x->level[i].forward;
    }

    // 节点是否小于最小值
    if (x == NULL || !zslValueGteMin(x->score,range))
        return NULL;

    return x;
}

// 根据分值成员获取节点的索引值
// 找到返回索引值, 未找到返回 0
unsigned long zslGetRank(zskiplist *zsl,double score, sds ele)
{   
    zskiplistNode *x;
    unsigned long rank = 0;
    int i;

    // 遍历查找节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过分值小的节点
        while (x->level[i].forward && 
                (x->level[i].forward->score < score || 
                 (x->level[i].forward->score == score && 
                  sdscmp(x->level[i].forward->ele,ele)<=0 )))
        {
            rank += x->level[i].span;
            x = x->level[i].forward;
        }

        // 比对当前节点是否为目标节点
        if (x && x->score==score && sdscmp(x->ele,ele)==0) {
            return rank;
        }
    }

    // 未找到
    return 0;
}

// 根据索引返回节点
// 成功返回节点, 失败返回 NULL
zskiplistNode *zslGetElementByRank(zskiplist *zsl, int rank)
{
    zskiplistNode *x;
    unsigned long traversed = 0;
    int i;

    // 遍历层查找目标索引
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过小于目标索引的节点
        while (x->level[i].forward && (traversed + x->level[i].span) <= rank) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }

        // 比对当前节点索引与目标索引是否相同
        if (x && traversed == rank) {
            return x;
        }
    }

    // 未找到
    return NULL;
}

// 删除搜索条件范围内的节点
// 返回删除的节点数量
int zslDeleteRangeByScore(zskiplist *zsl,zrangespec *range)
{   
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x, *next;
    int i,removed=0;

    x = zsl->header;
    // 跳过小于起始范围的节点
    for (i=zsl->level-1; i>=0; i--) {

        while(x->level[i].forward && 
                !zslValueGteMin(x->level[i].forward->score,range))
            x = x->level[i].forward;

        update[i] = x;
    }

    // 目标节点
    x = x->level[0].forward;
    // 遍历删除范围内的节点
    while (x && zslValueLteMax(x->score,range)) {
        next = x->level[0].forward;
        zslDeleteNode(zsl,x,update);
        zslFreeNode(x);
        removed++;
        x = next;
    }

    return removed;
}

// 按索引范围删除节点
// 返回删除节点数量
unsigned long zslDeleteRangeByRank(zskiplist *zsl,unsigned int start, unsigned int end)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x, *next;
    unsigned long traversed = 0, removed = 0;
    int i;

    // 跳过小于起始索引的节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && (traversed + x->level[i].span) < start) {

            traversed += x->level[i].span;
            x = x->level[i].forward;
        }

        update[i] = x;
    }

    traversed++;
    x = x->level[0].forward;
    // 删除结束索引范围内的节点
    while(x && traversed <= end) {

        next = x->level[0].forward;
        zslDeleteNode(zsl,x,update);
        zslFreeNode(x);
        traversed++;
        removed++;
        x = next;
    }

    // 返回
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
    ret = zslDeleteRangeByRank(zsl,1,2);
    printf("zslDeleteRangeByRank 1 & 2 index, removed : %d\n",ret);

    ret = zslDelete(zsl, 70.0, sdsnew("alice"), &node);  // 删除元素
    if (ret == 1) {
        printf("Delete node:%s->%f success!\n", node->ele, node->score);
    }
    
    return 0;
}