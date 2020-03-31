#include "redis.h"

/*--------------------- private ---------------------*/
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

/*--------------------- API ---------------------*/

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
 * 符合搜索条件(range) 的第一个节点
 * 
 * 成功, 返回节点
 * 失败或未找到, 返回 NULL
 */
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *x;
    int i;

    // 边界判断,如果所有节点都不在范围内存, 返回 NULL
    if (!zslIsInRange(zsl,range)) return NULL;

    // 自上而下遍历层
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {
        // 遍历节点,跳过 score 小于 range 最小值的节点
        while(x->level[i].forward && 
                !zslValueGteMin(x->level[i].forward->score,range))
            // 处理下一个节点
            x = x->level[i].forward;
    }

    // x 的下一个节点是大于 range 最小值的节点
    x = x->level[0].forward;

    // 判断 x 的下一个节点不能是 NULL
    // redisAssert(x != NULL);
    if (x == NULL) return NULL;

    // x 的分值不能超过 range 的最大值
    if (!zslValueLteMax(x->score,range)) return NULL;

    // 返回节点
    return x;
}

/**
 * 查找符合搜索条件(range)的最大的节点
 * 
 * 成功返回节点, 否则返回 NULL
 */
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *x;
    int i;

    // 边界判断,如果所有节点都不在范围内存, 返回 NULL
    if (!zslIsInRange(zsl,range)) return NULL;

    // 自上向下查找, 第一个节点分值大于 range 的最大值的节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        while(x->level[i].forward && 
                zslValueLteMax(x->level[i].forward->score,range))
            x = x->level[i].forward;
    }

    if (x == NULL) return NULL;

    // 检查节点分值是否大于 min
    if (!zslValueGteMin(x->score,range)) return NULL;

    return x;
}

/**
 * 根据分值和对象成员查找节点的索引值
 * 
 * 找到返回 索引值, 未找到返回 0
 */
unsigned long zslGetRank(zskiplist *zsl, double score, sds ele)
{
    zskiplistNode *x;
    unsigned long rank = 0;
    int i;

    // 直上向下遍历层
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过不符合的节点
        while (x->level[i].forward && 
                (x->level[i].forward->score < score ||
                 (x->level[i].forward->score == score && 
                 sdscmp(x->level[i].forward->ele,ele)<=0 )))
        {
            // 记录跳过的节点数
            rank += x->level[i].span;

            // 处理下一个节点
            x = x->level[i].forward;
        }

        // 确认当前节点是否为目标节点
        if (x->ele && sdscmp(x->ele,ele) == 0) {
            return rank;
        }
    }

    // 未找到
    return 0;
}

/**
 * 根据索引值查找节点
 * 
 * 找到返回 节点, 未找到返回 NULL
 */
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank)
{
    zskiplistNode *x;
    unsigned long traversed = 0;
    int i;

    // 直上向下遍历层
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 按 rank 跳过节点
        while (x->level[i].forward && (traversed + x->level[i].span) <= rank) {
            // 记录跳过的节点数
            traversed += x->level[i].span;
            // 处理下一个节点
            x = x->level[i].forward;
        }

        // 检查当前索引是否为目标索引
        if (traversed == rank) {
            // 返回节点
            return x;
        }
    }

    // 未找到
    return NULL;
}

/**
 * 删除指定范围内的节点
 * 返回删除节点数量
 */
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;

    // 跳过小于最小值的节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {
        while (x->level[i].forward && (range->minex ? 
                x->level[i].forward->score <= range->min : 
                x->level[i].forward->score < range->min))
            x = x->level[i].forward;
        update[i] = x;
    }

    // 第一个大于 range 最小值的节点
    x = x->level[0].forward;

    // 找出所有小于 range 最大值的节点,并删除
    while (x && 
            (range->maxex ? x->score < range->max : x->score <= range->max))
    {
        zskiplistNode *next = x->level[0].forward;
        // 删除节点
        zslDeleteNode(zsl,x,update);
        // 释放节点
        zslFreeNode(x);
        // 删除节点数增加
        removed++;

        x = next;
    }

    return removed;
}

/**
 * 删除指定范围索引的节点,包含开始结束的索引值
 * 返回删除节点数量
 */
unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0,traversed=0;
    int i;

    // 跳过节点分值小于起始范围的节点
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {
        
        while(x->level[i].forward && (traversed + x->level[i].span) < start){
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        update[i] = x;
    }
    // 跳到符合范围的节点
    traversed++;
    x = x->level[0].forward;
    // 查找节点分值小于结束范围的节点
    while (x && traversed <= end) {
        zskiplistNode *next = x->level[0].forward;
        // 删除节点
        zslDeleteNode(zsl,x,update);
        // 释放节点
        zslFreeNode(x);
        // 更新删除节点数
        removed++;
        traversed++;
        x = next;
    }

    // 返回删除节点数
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