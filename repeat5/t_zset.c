#include "redis.h"

// 创建并返回一个指定层数的跳跃表节点
zskiplistNode *zslCreateNode(int level, double score, sds ele)
{
    // 申请内存空间
    zskiplistNode *zn = zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));
    // 初始化属性
    zn->score = score;
    zn->ele = ele;

    return zn;
}

// 创建并返回一个空跳跃表
zskiplist *zslCreate()
{
    // 申请内存空间
    zskiplist *zsl = zmalloc(sizeof(*zsl));
    int i;

    // 初始化跳跃表节点数和最大层数
    zsl->level = 1;
    zsl->length = 0;

    // 申请头节点
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);

    // 初始化头节点层属性
    for (i=0; i<ZSKIPLIST_MAXLEVEL; i++) {
        zsl->header->level[i].forward = NULL;
        zsl->header->level[i].span = 0;
    }

    // 头节点后退指针
    zsl->header->backward = NULL;

    // 跳跃表尾节点
    zsl->tail = NULL;

    return zsl;
}

// 释放节点
void zslFreeNode(zskiplistNode *node)
{
    // 释放对象成员
    sdsfree(node->ele);
    // 释放节点
    zfree(node);
}

// 释放跳跃表
void zslFree(zskiplist *zsl)
{
    zskiplistNode *zn = zsl->header->level[0].forward, *next;

    // 释放首节点 myerr
    // zslFreeNode(zsl->header);
    zfree(zsl->header);

    // 释放其他节点
    while (zn) {
        next = zn->level[0].forward;
        zslFreeNode(zn);
        zn = next;
    }

    // 释放跳跃表
    zfree(zsl);
}

// 删除节点
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update)
{
    int i;

    // 更新删除节点的相邻节点的前进指针和跨度 myerr
    // for (i=zsl->level-1; i>=0; i--) {
    for (i=0; i<zsl->level; i++) {

        // 接触节点,更新指针和跨度
        if (update[i]->level[i].forward == x) {
            update[i]->level[i].forward = x->level[i].forward;
            update[i]->level[i].span += x->level[i].span - 1;
        }
        // 未接触节点,更新跨度
        else {
            update[i]->level[i].span -= 1;
        }

    }

    // 更新后退指针
    if (x->level[0].forward)
        x->level[0].forward->backward = x->backward;
    // 尝试更新尾节点指针
    else
        zsl->tail = x->backward;

    // 尝试更新最大层数
    while(zsl->level>1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;

    // 更新节点数
    zsl->length--;
}


// 删除跳跃表指定节点
// 删除成功返回 1, 否则返回 0
// 如果 **node不为空, 不释放目标节点, 赋值给 node
// 如果 **node 为空, 释放目标节点
int zslDelete(zskiplist *zsl, double score, sds ele, zskiplistNode **node)
{   
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    // 遍历查找目标节点
    x = zsl->header;
    // for (i=zsl->level-1; i>=0; i--) { myerr

    //     while (x->level[i].forward && 
    //             (x->level[i].forward->score < score) ||
    //              (x->level[i].forward->score == score && 
    //               sdscmp(x->level[i].forward->ele,ele)< = 0 ))
    //     {
    //         x = x->level[i].forward;
    //     }
    //     update[i] = x;
    // }
    for (i=zsl->level-1; i>=0; i--) {

        while (x->level[i].forward && 
                (x->level[i].forward->score < score ||
                 (x->level[i].forward->score == score && 
                  sdscmp(x->level[i].forward->ele,ele)< 0 )))
        {
            x = x->level[i].forward;
        }
        update[i] = x;
    }
    // 比对代码前,没有的代码 myerr
    x = x->level[0].forward;


    // 找到目标节点
    // if (x && x->score == score && sdscmp(x->ele,ele)) {
    if (x && x->score == score && sdscmp(x->ele,ele)==0) {
        zslDeleteNode(zsl,x,update);
        if (!node) 
            zslFreeNode(x);
        else 
            *node = x;
        return 1;
    }

    // 未找到
    return 0;
}

// 随机生成层数
unsigned int zslRandomLevel(void)
{
    int level = 1;
    while ((rand()&0xFFFF) < (ZSKIPLIST_P*0xFFFF))
        level++;
    return (level<ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

// 添加节点
zskiplistNode *zslInsert(zskiplist *zsl, double score, sds ele)
{   
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    x = zsl->header;
    // 遍历每层适合添加节点的位置,并记录下该外置的前一个节点
    for (i=zsl->level-1; i>=0; i--) {
        
        rank[i] = i == (zsl->level-1) ? 0 : rank[i+1];
        // 跳过小于目标节点的节点
        while (x->level[i].forward && 
                (x->level[i].forward->score < score || 
                (x->level[i].forward->score == score && 
                 sdscmp(x->level[i].forward->ele,ele)<0 )))
        {
            rank[i] += x->level[i].span;
            x = x->level[i].forward;
        }
        // 记录前一个节点
        update[i] = x;
    }

    // 随机层数
    level = zslRandomLevel();

    // 超出最大层数部分初始化前一个节点数据 myerr:放zslCreateNode上面
    if (level > zsl->level) {
        for (i=zsl->level; i<level; i++) {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }
        zsl->level = level;
    }

        // 创建新节点
    x = zslCreateNode(level,score,ele);

    // 更新新节点的指针和跨度
    for (i=0; i<level; i++) {
        x->level[i].forward = update[i]->level[i].forward;
        update[i]->level[i].forward = x;
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    // "未接触"的节点,更新跨度
    for (i=level; i<zsl->level; i++) {
        update[i]->level[i].span += 1;
    }

    // 新节点的后退指针
    // x->backward = update[0] ? update[0] : NULL;
    x->backward = (update[0] == zsl->header) ? NULL : update[0];

    // 临近节点的后退指针
    if (x->level[0].forward)
        x->level[0].forward->backward = x;
    // 尝试修改跳跃表尾节点
    else
        zsl->tail = x;

    // 更改跳跃表节点数
    zsl->length++;

    return x;
}

// value大于最小值
int zslValueGteMin(double value, zrangespec *range)
{
    return range->minex ? (value > range->min) : (value >= range->min);
}

// value小于最大值
int zslValueLteMax(double value, zrangespec *range)
{
    return range->maxex ? (value < range->max) : (value <= range->max);
}

// 跳跃表是否满足搜索条件
// 满足返回 1, 否则返回 0
int zslIsInRange(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *x;
    // range合理性检查
    if (range->min > range->max || 
            (range->min==range->max && (range->minex || range->maxex)))
        return 0;

    // 边界检查,跳跃表最大值小于range最小值
    x = zsl->tail;
    if (x == NULL || !zslValueGteMin(x->score,range))
        return 0;

    // 边界检查,跳跃表最小值大于range最大值
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score,range))
        return 0;

    // 通过检查
    return 1;
}

// 符合搜索条件的第一个节点
// 找到返回节点, 否则返回 NULL
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range)
{   
    zskiplistNode *x;
    int i;

    if(!zslIsInRange(zsl,range)) return NULL;

    x = zsl->header;
    // 自上而下,遍历层
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过不符合搜索条件的节点 myerr
        // while (x->level[i].forward && 
        //         zslValueGteMin(x->level[i].forward->score,range))
        while (x->level[i].forward && 
                !zslValueGteMin(x->level[i].forward->score,range))
        {
            x = x->level[i].forward;
        }
    }
    // 当前节点的下一个节点为目标节点
    x = x->level[0].forward;

    // 节点不能大于最大值范围
    if (x == NULL || !zslValueLteMax(x->score,range))
        return NULL;

    // 返回节点
    return x;
}

// 符合搜索条件的最后一个节点
// 找到返回节点, 否则返回 NULL
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range)
{   
    zskiplistNode *x;
    int i;

    // 范围合理性检查
    if (!zslIsInRange(zsl,range)) return NULL;

    // 自上而下遍历层
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过小于最大值的节点
        while (x->level[i].forward && 
                zslValueLteMax(x->level[i].forward->score,range))
        {
            x = x->level[i].forward;
        }
    }

    // 当前节点是否小于最小值
    if (x == NULL || !zslValueGteMin(x->score,range))
        return NULL;

    return x;
}

// 查找节点并返回索引,未找到返回 0
unsigned long zslGetRank(zskiplist *zsl, double score, sds ele)
{
    zskiplistNode *x;
    unsigned long rank = 0;
    int i;

    // 自上而下遍历层
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过非目标节点
        while (x->level[i].forward && 
                (x->level[i].forward->score < score ||
                 (x->level[i].forward->score == score && 
                  sdscmp(x->level[i].forward->ele,ele)<=0 )))
        {
            rank += x->level[i].span;
            x = x->level[i].forward;
        }
        // 检查是否为目标节点 myerr
        // if (x && x->score == score && sdscmp(x->ele,ele)) {
        if (x->ele && sdscmp(x->ele,ele)==0) {
            return rank;
        }
    }
            
    return 0;
}

// 根据索引值获取节点
// 找到返回节点, 否则返回 NULL
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank)
{
    zskiplistNode *x;
    unsigned long traversed = 0;
    int i;

    x = zsl->header;
    // 自上而下遍历层
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过累加跨度小于rank的节点
        while (x->level[i].forward && (traversed + x->level[i].span) <= rank ) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }

        // 比对 rank 是否相同
        if (x != NULL && traversed == rank){
            return x;
        }
    }

    // 未找到
    return NULL;
}

// 节点分值符合搜索条件的全部删除
// 返回删除节点数
// int zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range) myerr
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x, *next;
    unsigned long removed = 0;
    int i;

    // 范围是否适合当前跳跃表
    if (!zslIsInRange(zsl, range)) return 0;

    // 自上而下遍历层
    x = zsl->header;
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过分值小于范围最小值的节点
        while (x->level[i].forward && 
                !zslValueGteMin(x->level[i].forward->score,range))
        {
            x = x->level[i].forward;
        }
        // 记录该层待删节点的前一个节点
        update[i] = x;
    }

    // 遍历删除小于范围最大值的节点
    x = x->level[0].forward;
    while (x && zslValueLteMax(x->score,range))
    {
        next = x->level[0].forward;
        zslDeleteNode(zsl,x,update);
        zslFreeNode(x);
        // 更新删除数量
        removed++;
        // 处理下一个节点
        x = next;
    }

    return removed;
}

// 按索引范围删除节点
// 返回删除节点数量
unsigned long zslDeleteRangeByRank(zskiplist *zsl,unsigned int start, unsigned int end)
{   
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x, *next;
    unsigned long traversed = 0,removed = 0;
    int i;

    x = zsl->header;
    // 自上而下遍历层
    for (i=zsl->level-1; i>=0; i--) {

        // 跳过小于最小值索引的节点
        while (x->level[i].forward && (traversed + x->level[i].span) < start) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        // 记录每层的终止节点
        update[i] = x;
    }

    // 定位目标起始节点
    traversed++;
    x = x->level[0].forward;

    // 遍历删除小于最大值索引的节点
    while (x && traversed <= end) {
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