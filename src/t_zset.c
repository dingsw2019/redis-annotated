#include "redis.h"
#include <math.h>

/*
 * 创建一个层数为 level 的跳跃表节点，
 * 并将节点的成员对象设置为 obj ，分值设置为 score 。
 *
 * 返回值为新创建的跳跃表节点
 *
 * T = O(1)
 */
zskiplistNode *zslCreateNode(int level, double score, robj *obj) {
    
    // 分配空间
    zskiplistNode *zn = zmalloc(sizeof(*zn)+level*sizeof(struct zskiplistLevel));

    // 设置属性
    zn->score = score;
    zn->obj = obj;

    return zn;
}

/*
 * 创建并返回一个新的跳跃表
 *
 * T = O(1)
 */
zskiplist *zslCreate(void) {
    int j;
    zskiplist *zsl;

    // 分配空间
    zsl = zmalloc(sizeof(*zsl));

    // 设置高度和起始层数
    zsl->level = 1;
    zsl->length = 0;

    // 初始化表头节点
    // T = O(1)
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);
    for (j = 0; j < ZSKIPLIST_MAXLEVEL; j++) {
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }
    zsl->header->backward = NULL;

    // 设置表尾
    zsl->tail = NULL;

    return zsl;
}

/*
 * 释放给定的跳跃表节点
 *
 * T = O(1)
 */
void zslFreeNode(zskiplistNode *node) {

    decrRefCount(node->obj);

    zfree(node);
}

/*
 * 释放给定跳跃表，以及表中的所有节点
 *
 * T = O(N)
 */
void zslFree(zskiplist *zsl) {

    zskiplistNode *node = zsl->header->level[0].forward, *next;

    // 释放表头
    zfree(zsl->header);

    // 释放表中所有节点
    // T = O(N)
    while(node) {

        next = node->level[0].forward;

        zslFreeNode(node);

        node = next;
    }
    
    // 释放跳跃表结构
    zfree(zsl);
}

/* Returns a random level for the new skiplist node we are going to create.
 *
 * 返回一个随机值，用作新跳跃表节点的层数。
 *
 * The return value of this function is between 1 and ZSKIPLIST_MAXLEVEL
 * (both inclusive), with a powerlaw-alike distribution where higher
 * levels are less likely to be returned. 
 *
 * 返回值介乎 1 和 ZSKIPLIST_MAXLEVEL 之间（包含 ZSKIPLIST_MAXLEVEL），
 * 根据随机算法所使用的幂次定律，越大的值生成的几率越小。
 *
 * T = O(N)
 */
int zslRandomLevel(void) {
    int level = 1;

    // while ((random()&0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
    while ((rand()&0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        level += 1;

    return (level<ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

/*
 * 创建一个成员为 obj ，分值为 score 的新节点，
 * 并将这个新节点插入到跳跃表 zsl 中。
 * 
 * 函数的返回值为新节点。
 *
 * T_wrost = O(N^2), T_avg = O(N log N)
 */
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    // redisAssert(!isnan(score));

    // 在各个层查找节点的插入位置
    // T_wrost = O(N^2), T_avg = O(N log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        /* store rank that is crossed to reach the insert position */
        // 如果 i 不是 zsl->level-1 层
        // 那么 i 层的起始 rank 值为 i+1 层的 rank 值
        // 各个层的 rank 值一层层累积
        // 最终 rank[0] 的值加一就是新节点的前置节点的排位
        // rank[0] 会在后面成为计算 span 值和 rank 值的基础
        rank[i] = i == (zsl->level-1) ? 0 : rank[i+1];

        // 沿着前进指针遍历跳跃表
        // T_wrost = O(N^2), T_avg = O(N log N)
        while (x->level[i].forward &&
            (x->level[i].forward->score < score ||
                // 比对分值
                (x->level[i].forward->score == score &&
                // 比对成员， T = O(N)
                compareStringObjects(x->level[i].forward->obj,obj) < 0))) {

            // 记录沿途跨越了多少个节点
            rank[i] += x->level[i].span;

            // 移动至下一指针
            x = x->level[i].forward;
        }
        // 记录将要和新节点相连接的节点
        update[i] = x;
    }

    /* we assume the key is not already inside, since we allow duplicated
     * scores, and the re-insertion of score and redis object should never
     * happen since the caller of zslInsert() should test in the hash table
     * if the element is already inside or not. 
     *
     * zslInsert() 的调用者会确保同分值且同成员的元素不会出现，
     * 所以这里不需要进一步进行检查，可以直接创建新元素。
     */

    // 获取一个随机值作为新节点的层数
    // T = O(N)
    level = zslRandomLevel();

    // 如果新节点的层数比表中其他节点的层数都要大
    // 那么初始化表头节点中未使用的层，并将它们记录到 update 数组中
    // 将来也指向新节点
    if (level > zsl->level) {

        // 初始化未使用层
        // T = O(1)
        for (i = zsl->level; i < level; i++) {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }

        // 更新表中节点最大层数
        zsl->level = level;
    }

    // 创建新节点
    x = zslCreateNode(level,score,obj);

    // 将前面记录的指针指向新节点，并做相应的设置
    // T = O(1)
    for (i = 0; i < level; i++) {
        
        // 设置新节点的 forward 指针
        x->level[i].forward = update[i]->level[i].forward;
        
        // 将沿途记录的各个节点的 forward 指针指向新节点
        update[i]->level[i].forward = x;

        /* update span covered by update[i] as x is inserted here */
        // 计算新节点跨越的节点数量
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);

        // 更新新节点插入之后，沿途节点的 span 值
        // 其中的 +1 计算的是新节点
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    /* increment span for untouched levels */
    // 未接触的节点的 span 值也需要增一，这些节点直接从表头指向新节点
    // T = O(1)
    for (i = level; i < zsl->level; i++) {
        update[i]->level[i].span++;
    }

    // 设置新节点的后退指针
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    if (x->level[0].forward)
        x->level[0].forward->backward = x;
    else
        zsl->tail = x;

    // 跳跃表的节点计数增一
    zsl->length++;

    return x;
}

/* Internal function used by zslDelete, zslDeleteByScore and zslDeleteByRank 
 * 
 * 内部删除函数，
 * 被 zslDelete 、 zslDeleteRangeByScore 和 zslDeleteByRank 等函数调用。
 *
 * T = O(1)
 */
void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update) {
    int i;

    // 更新所有和被删除节点 x 有关的节点的指针，解除它们之间的关系
    // T = O(1)
    for (i = 0; i < zsl->level; i++) {
        if (update[i]->level[i].forward == x) {
            update[i]->level[i].span += x->level[i].span - 1;
            update[i]->level[i].forward = x->level[i].forward;
        } else {
            update[i]->level[i].span -= 1;
        }
    }

    // 更新被删除节点 x 的前进和后退指针
    if (x->level[0].forward) {
        x->level[0].forward->backward = x->backward;
    } else {
        zsl->tail = x->backward;
    }

    // 更新跳跃表最大层数（只在被删除节点是跳跃表中最高的节点时才执行）
    // T = O(1)
    while(zsl->level > 1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;

    // 跳跃表节点计数器减一
    zsl->length--;
}

/* Delete an element with matching score/object from the skiplist. 
 *
 * 从跳跃表 zsl 中删除包含给定节点 score 并且带有指定对象 obj 的节点。
 *
 * T_wrost = O(N^2), T_avg = O(N log N)
 */
int zslDelete(zskiplist *zsl, double score, robj *obj) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    // 遍历跳跃表，查找目标节点，并记录所有沿途节点
    // T_wrost = O(N^2), T_avg = O(N log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        // 遍历跳跃表的复杂度为 T_wrost = O(N), T_avg = O(log N)
        while (x->level[i].forward &&
            (x->level[i].forward->score < score ||
                // 比对分值
                (x->level[i].forward->score == score &&
                // 比对对象，T = O(N)
                compareStringObjects(x->level[i].forward->obj,obj) < 0)))

            // 沿着前进指针移动
            x = x->level[i].forward;

        // 记录沿途节点
        update[i] = x;
    }

    /* We may have multiple elements with the same score, what we need
     * is to find the element with both the right score and object. 
     *
     * 检查找到的元素 x ，只有在它的分值和对象都相同时，才将它删除。
     */
    x = x->level[0].forward;
    if (x && score == x->score && equalStringObjects(x->obj,obj)) {
        // T = O(1)
        zslDeleteNode(zsl, x, update);
        // T = O(1)
        zslFreeNode(x);
        return 1;
    } else {
        return 0; /* not found */
    }

    return 0; /* not found */
}

/*
 * 检测给定值 value 是否大于（或大于等于）范围 spec 中的 min 项。
 *
 * 返回 1 表示 value 大于等于 min 项，否则返回 0 。
 *
 * T = O(1)
 */
static int zslValueGteMin(double value, zrangespec *spec) {
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}

/*
 * 检测给定值 value 是否小于（或小于等于）范围 spec 中的 max 项。
 *
 * 返回 1 表示 value 小于等于 max 项，否则返回 0 。
 *
 * T = O(1)
 */
static int zslValueLteMax(double value, zrangespec *spec) {
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

/* Returns if there is a part of the zset is in range.
 *
 * 如果给定的分值范围包含在跳跃表的分值范围之内，
 * 那么返回 1 ，否则返回 0 。
 *
 * T = O(1)
 */
int zslIsInRange(zskiplist *zsl, zrangespec *range) {
    zskiplistNode *x;

    /* Test for ranges that will always be empty. */
    // 先排除总为空的范围值
    if (range->min > range->max ||
            (range->min == range->max && (range->minex || range->maxex)))
        return 0;

    // 检查最大分值
    x = zsl->tail;
    if (x == NULL || !zslValueGteMin(x->score,range))
        return 0;

    // 检查最小分值
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score,range))
        return 0;

    return 1;
}

/* Find the first node that is contained in the specified range.
 *
 * 返回 zsl 中第一个分值符合 range 中指定范围的节点。
 * Returns NULL when no element is contained in the range.
 *
 * 如果 zsl 中没有符合范围的节点，返回 NULL 。
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range) {
    zskiplistNode *x;
    int i;

    /* If everything is out of range, return early. */
    if (!zslIsInRange(zsl,range)) return NULL;

    // 遍历跳跃表，查找符合范围 min 项的节点
    // T_wrost = O(N), T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        /* Go forward while *OUT* of range. */
        while (x->level[i].forward &&
            !zslValueGteMin(x->level[i].forward->score,range))
                x = x->level[i].forward;
    }

    /* This is an inner range, so the next node cannot be NULL. */
    x = x->level[0].forward;
    // redisAssert(x != NULL);

    /* Check if score <= max. */
    // 检查节点是否符合范围的 max 项
    // T = O(1)
    if (!zslValueLteMax(x->score,range)) return NULL;
    return x;
}

/* Find the last node that is contained in the specified range.
 * Returns NULL when no element is contained in the range.
 *
 * 返回 zsl 中最后一个分值符合 range 中指定范围的节点。
 *
 * 如果 zsl 中没有符合范围的节点，返回 NULL 。
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range) {
    zskiplistNode *x;
    int i;

    /* If everything is out of range, return early. */
    // 先确保跳跃表中至少有一个节点符合 range 指定的范围，
    // 否则直接失败
    // T = O(1)
    if (!zslIsInRange(zsl,range)) return NULL;

    // 遍历跳跃表，查找符合范围 max 项的节点
    // T_wrost = O(N), T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        /* Go forward while *IN* range. */
        while (x->level[i].forward &&
            zslValueLteMax(x->level[i].forward->score,range))
                x = x->level[i].forward;
    }

    /* This is an inner range, so this node cannot be NULL. */
    // redisAssert(x != NULL);

    /* Check if score >= min. */
    // 检查节点是否符合范围的 min 项
    // T = O(1)
    if (!zslValueGteMin(x->score,range)) return NULL;

    // 返回节点
    return x;
}

/* Delete all the elements with score between min and max from the skiplist.
 *
 * 删除所有分值在给定范围之内的节点。
 *
 * Min and max are inclusive, so a score >= min || score <= max is deleted.
 * 
 * min 和 max 参数都是包含在范围之内的，所以分值 >= min 或 <= max 的节点都会被删除。
 *
 * Note that this function takes the reference to the hash table view of the
 * sorted set, in order to remove the elements from the hash table too.
 *
 * 节点不仅会从跳跃表中删除，而且会从相应的字典中删除。
 *
 * 返回值为被删除节点的数量
 *
 * T = O(N)
 */
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;

    // 记录所有和被删除节点（们）有关的节点
    // T_wrost = O(N) , T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward && (range->minex ?
            x->level[i].forward->score <= range->min :
            x->level[i].forward->score < range->min))
                x = x->level[i].forward;
        update[i] = x;
    }

    /* Current node is the last with score < or <= min. */
    // 定位到给定范围开始的第一个节点
    x = x->level[0].forward;

    /* Delete nodes while in range. */
    // 删除范围中的所有节点
    // T = O(N)
    while (x &&
           (range->maxex ? x->score < range->max : x->score <= range->max))
    {
        // 记录下个节点的指针
        zskiplistNode *next = x->level[0].forward;
        zslDeleteNode(zsl,x,update);
        dictDelete(dict,x->obj);
        zslFreeNode(x);
        removed++;
        x = next;
    }
    return removed;
}

unsigned long zslDeleteRangeByLex(zskiplist *zsl, zlexrangespec *range, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;


    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward &&
            !zslLexValueGteMin(x->level[i].forward->obj,range))
                x = x->level[i].forward;
        update[i] = x;
    }

    /* Current node is the last with score < or <= min. */
    x = x->level[0].forward;

    /* Delete nodes while in range. */
    while (x && zslLexValueLteMax(x->obj,range)) {
        zskiplistNode *next = x->level[0].forward;

        // 从跳跃表中删除当前节点
        zslDeleteNode(zsl,x,update);
        // 从字典中删除当前节点
        dictDelete(dict,x->obj);
        // 释放当前跳跃表节点的结构
        zslFreeNode(x);

        // 增加删除计数器
        removed++;

        // 继续处理下个节点
        x = next;
    }

    // 返回被删除节点的数量
    return removed;
}

/* Delete all the elements with rank between start and end from the skiplist.
 *
 * 从跳跃表中删除所有给定排位内的节点。
 *
 * Start and end are inclusive. Note that start and end need to be 1-based 
 *
 * start 和 end 两个位置都是包含在内的。注意它们都是以 1 为起始值。
 *
 * 函数的返回值为被删除节点的数量。
 *
 * T = O(N)
 */
unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long traversed = 0, removed = 0;
    int i;

    // 沿着前进指针移动到指定排位的起始位置，并记录所有沿途指针
    // T_wrost = O(N) , T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward && (traversed + x->level[i].span) < start) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    // 移动到排位的起始的第一个节点
    traversed++;
    x = x->level[0].forward;
    // 删除所有在给定排位范围内的节点
    // T = O(N)
    while (x && traversed <= end) {

        // 记录下一节点的指针
        zskiplistNode *next = x->level[0].forward;

        // 从跳跃表中删除节点
        zslDeleteNode(zsl,x,update);
        // 从字典中删除节点
        dictDelete(dict,x->obj);
        // 释放节点结构
        zslFreeNode(x);

        // 为删除计数器增一
        removed++;

        // 为排位计数器增一
        traversed++;

        // 处理下个节点
        x = next;
    }

    // 返回被删除节点的数量
    return removed;
}

/* Find the rank for an element by both score and key.
 *
 * 查找包含给定分值和成员对象的节点在跳跃表中的排位。
 *
 * Returns 0 when the element cannot be found, rank otherwise.
 *
 * 如果没有包含给定分值和成员对象的节点，返回 0 ，否则返回排位。
 *
 * Note that the rank is 1-based due to the span of zsl->header to the
 * first element. 
 *
 * 注意，因为跳跃表的表头也被计算在内，所以返回的排位以 1 为起始值。
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o) {
    zskiplistNode *x;
    unsigned long rank = 0;
    int i;

    // 遍历整个跳跃表
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        // 遍历节点并对比元素
        while (x->level[i].forward &&
            (x->level[i].forward->score < score ||
                // 比对分值
                (x->level[i].forward->score == score &&
                // 比对成员对象
                compareStringObjects(x->level[i].forward->obj,o) <= 0))) {

            // 累积跨越的节点数量
            rank += x->level[i].span;

            // 沿着前进指针遍历跳跃表
            x = x->level[i].forward;
        }

        /* x might be equal to zsl->header, so test if obj is non-NULL */
        // 必须确保不仅分值相等，而且成员对象也要相等
        // T = O(N)
        if (x->obj && equalStringObjects(x->obj,o)) {
            return rank;
        }
    }

    // 没找到
    return 0;
}

/* Finds an element by its rank. The rank argument needs to be 1-based. 
 * 
 * 根据排位在跳跃表中查找元素。排位的起始值为 1 。
 *
 * 成功查找返回相应的跳跃表节点，没找到则返回 NULL 。
 *
 * T_wrost = O(N), T_avg = O(log N)
 */
zskiplistNode* zslGetElementByRank(zskiplist *zsl, unsigned long rank) {
    zskiplistNode *x;
    unsigned long traversed = 0;
    int i;

    // T_wrost = O(N), T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        // 遍历跳跃表并累积越过的节点数量
        while (x->level[i].forward && (traversed + x->level[i].span) <= rank)
        {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }

        // 如果越过的节点数量已经等于 rank
        // 那么说明已经到达要找的节点
        if (traversed == rank) {
            return x;
        }

    }

    // 没找到目标节点
    return NULL;
}


/* Populate the rangespec according to the objects min and max. 
 *
 * 对 min 和 max 进行分析，并将区间的值保存在 spec 中。
 *
 * 分析成功返回 REDIS_OK ，分析出错导致失败返回 REDIS_ERR 。
 *
 * T = O(N)
 */
static int zslParseRange(robj *min, robj *max, zrangespec *spec) {  
    char *eptr;

    // 默认为闭区间
    spec->minex = spec->maxex = 0;

    /* Parse the min-max interval. If one of the values is prefixed
     * by the "(" character, it's considered "open". For instance
     * ZRANGEBYSCORE zset (1.5 (2.5 will match min < x < max
     * ZRANGEBYSCORE zset 1.5 2.5 will instead match min <= x <= max */
    if (min->encoding == REDIS_ENCODING_INT) {
        // min 的值为整数，开区间
        spec->min = (long)min->ptr;
    } else {
        // min 对象为字符串，分析 min 的值并决定区间
        if (((char*)min->ptr)[0] == '(') {
            // T = O(N)
            spec->min = strtod((char*)min->ptr+1,&eptr);
            if (eptr[0] != '\0' || isnan(spec->min)) return REDIS_ERR;
            spec->minex = 1;
        } else {
            // T = O(N)
            spec->min = strtod((char*)min->ptr,&eptr);
            if (eptr[0] != '\0' || isnan(spec->min)) return REDIS_ERR;
        }
    }

    if (max->encoding == REDIS_ENCODING_INT) {
        // max 的值为整数，开区间
        spec->max = (long)max->ptr;
    } else {
        // max 对象为字符串，分析 max 的值并决定区间
        if (((char*)max->ptr)[0] == '(') {
            // T = O(N)
            spec->max = strtod((char*)max->ptr+1,&eptr);
            if (eptr[0] != '\0' || isnan(spec->max)) return REDIS_ERR;
            spec->maxex = 1;
        } else {
            // T = O(N)
            spec->max = strtod((char*)max->ptr,&eptr);
            if (eptr[0] != '\0' || isnan(spec->max)) return REDIS_ERR;
        }
    }

    return REDIS_OK;
}

/*-------------------------- 字符范围搜索器 API -----------------------------*/
/* Parse max or min argument of ZRANGEBYLEX.
  * (foo means foo (open interval)
  * [foo means foo (closed interval)
  * - means the min string possible
  * + means the max string possible
  *
  * If the string is valid the *dest pointer is set to the redis object
  * that will be used for the comparision, and ex will be set to 0 or 1
  * respectively if the item is exclusive or inclusive. REDIS_OK will be
  * returned.
  *
  * If the string is not a valid range REDIS_ERR is returned, and the value
  * of *dest and *ex is undefined. */
int zslParseLexRangeItem(robj *item, robj **dest, int *ex) {
    char *c = item->ptr;

    switch(c[0]) {
    case '+':
        if (c[1] != '\0') return REDIS_ERR;
        *ex = 0;
        *dest = shared.maxstring;
        incrRefCount(shared.maxstring);
        return REDIS_OK;
    case '-':
        if (c[1] != '\0') return REDIS_ERR;
        *ex = 0;
        *dest = shared.minstring;
        incrRefCount(shared.minstring);
        return REDIS_OK;
    case '(':
        *ex = 1;
        *dest = createStringObject(c+1,sdslen(c)-1);
        return REDIS_OK;
    case '[':
        *ex = 0;
        *dest = createStringObject(c+1,sdslen(c)-1);
        return REDIS_OK;
    default:
        return REDIS_ERR;
    }
}

/* Populate the rangespec according to the objects min and max.
 *
 * Return REDIS_OK on success. On error REDIS_ERR is returned.
 * When OK is returned the structure must be freed with zslFreeLexRange(),
 * otherwise no release is needed. */
static int zslParseLexRange(robj *min, robj *max, zlexrangespec *spec) {
    /* The range can't be valid if objects are integer encoded.
     * Every item must start with ( or [. */
    if (min->encoding == REDIS_ENCODING_INT ||
        max->encoding == REDIS_ENCODING_INT) return REDIS_ERR;

    spec->min = spec->max = NULL;
    if (zslParseLexRangeItem(min, &spec->min, &spec->minex) == REDIS_ERR ||
        zslParseLexRangeItem(max, &spec->max, &spec->maxex) == REDIS_ERR) {
        if (spec->min) decrRefCount(spec->min);
        if (spec->max) decrRefCount(spec->max);
        return REDIS_ERR;
    } else {
        return REDIS_OK;
    }
}

/* Free a lex range structure, must be called only after zelParseLexRange()
 * populated the structure with success (REDIS_OK returned). */
void zslFreeLexRange(zlexrangespec *spec) {
    decrRefCount(spec->min);
    decrRefCount(spec->max);
}

/**
 * 字符串比较, 包含对字符串长度的检查
 */
int compareStringObjectsForLexRange(robj *a, robj *b) {
    if (a == b) return 0;

    if (a == shared.minstring || b == shared.maxstring) return -1;
    if (a == shared.maxstring || b == shared.minstring) return 1;

    return compareStringObjects(a,b);
}

/**
 * 字符串 value 大于(或等于) spec 的最小值
 * 大于, 返回 1
 * 小于, 返回 0
 */
static int zslLexValueGteMin(robj *value, zlexrangespec *spec) {
    return spec->minex ?
        (compareStringObjectsForLexRange(value,spec->min) > 0)  :
        (compareStringObjectsForLexRange(value,spec->min) >= 0) ;
}

/**
 * 字符串 value 小于(或等于) spec 的最大值
 * 小于, 返回 1
 * 大于, 返回 0
 */
static int zslLexValueLteMax(robj *value, zlexrangespec *spec) {
    return spec->maxex ?
        (compareStringObjectsForLexRange(value,spec->max) < 0) :
        (compareStringObjectsForLexRange(value,spec->max) <= 0) ;
}

/**
 * 如果 zsl 在 range 指定范围内, 是否存在节点
 * 存在节点, 返回 1
 * 不存在, 返回 0
 */
int zslIsInLexRange(zskiplist *zsl, zlexrangespec *range) {
    zskiplistNode *x;

    // 范围搜索器校验
    if (compareStringObjectsForLexRange(range->min,range->maxex) > 1 ||
        (compareStringObjectsForLexRange(range->min,range->max) == 0 && 
            (range->minex || range->maxex)))
            return 0;

    // 超出范围,zsl 在范围左侧
    x = zsl->tail;
    if (x == NULL || !zslLexValueGteMin(x->obj,range))
        return 0;

    // 超出范围,zsl 在范围右侧
    x = zsl->header;
    if (x == NULL || !zslLexValueLteMax(x->obj,range))
        return 0;

    return 1;
}

/**
 * 返回 range 范围内的第一个节点的地址
 * 不存在返回 NULL
 */
zskiplistNode *zslFirstInLexRange(zskiplist *zsl, zlexrangespec *range) {
    zskiplistNode *x;
    int i;

    // 在 range 范围内, 无节点
    if (!zslIsInLexRange(zsl,range)) return NULL;

    // 迭代节点
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        // 跳过不在范围内的节点
        while (x->level[i].forward && 
                !zslLexValueGteMin(x->level[i].forward->obj,range)) {
                    x = x->level[i].forward;
        }
    }

    // 找到第一个满足范围的节点
    x = x->level[0].forward;
    redisAssert(x != NULL);

    // 是否满足小于范围的最大值
    if (!zslLexValueLteMax(x->obj,range)) return NULL;

    return x;
}

/**
 * 返回 range 范围内的最后一个节点的地址
 * 不存在返回 NULL
 */
zskiplistNode *zslLastInLexRange(zskiplist *zsl, zlexrangespec *range) {
    zskiplistNode *x;
    int i;

    // 无节点在 range 范围内
    if (!zslIsInLexRange(zsl,range)) return NULL;

    // 遍历查找最后一个节点
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        
        while (x->level[i].forward && 
            zslLexValueLteMax(x->level[i].forward->obj,range)) {
                x = x->level[i].forward;
        }
    }

    redisAssert(x != NULL);

    // 检查最后一个节点是否大于 范围最小值
    if (!zslLexValueGteMin(x->obj,range)) return NULL;
    return x;
}

/*-------------------------- ziplist 编码的有序集合 API -----------------------------*/

/**
 * 取出并返回 sptr 指向的有序集合的元素的分值
 */
double zzlGetScore(unsigned char *sptr) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    char buf[128];
    double score;

    redisAssert(sptr != NULL);

    // 提取 sptr 的值
    redisAssert(ziplistGet(sptr,&vstr,&vlen,&vlong));

    // 提取整数
    if (vstr) {
        // 将字符串值转换成数值
        memcpy(buf,vstr,vlen);
        buf[vlen] = '\0';
        score = strtod(buf,NULL);

    } else {
        score = vlong;
    }

    return score;
}

/**
 * 取出并以 robj 格式返回 sptr 指向的有序集合的元素的分值
 */
robj *ziplistGetObject(unsigned char *sptr) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;

    redisAssert(sptr != NULL);

    // 提取值
    redisAssert(ziplistGet(sptr,&vstr,&vlen,&vlong));

    if (vstr) {
        return createStringObject(vstr,vlen);
    } else {
        return createStringObjectFromLongLong(vlong);
    }
}

/**
 * 将 eptr 中的元素值和 cstr 进行比对
 * 相等返回 0
 * eptr 的字符串比 cstr 大时, 返回正整数
 * eptr 的字符串比 cstr 小时, 返回负整数
 */
int zzlCompareElements(unsigned char *eptr, unsigned char *cstr, unsigned int clen) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    unsigned char vbuf[32];
    int cmp, minlen;

    // 提取 eptr 的值
    redisAssert(ziplistGet(eptr,&vstr,&vlen,&vlong));

    // 如果值是整数, 转成字符串
    if (vstr == NULL) {
        vlen = ll2string((char*)vbuf,sizeof(vbuf),vlong);
        vstr = vbuf;
    }

    // 比对
    minlen = (vlen < clen) ? vlen : clen;
    cmp = memcmp(vstr,cstr,minlen);
    if (cmp == 0) return vlen - clen;
    return cmp;
}

/*
 * 返回跳跃表包含的元素数量
 */
unsigned int zzlLength(unsigned char *zl) {
    return ziplistLen(zl)/2;
}

/**
 * 分别获取 eptr 和 sptr 的下一个成员和分值
 * 
 * 如果后面没有元素了, 两个指针都返回 NULL
 */
void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr) {
    unsigned char *_eptr, *_sptr;

    redisAssert(*eptr != NULL && *sptr != NULL);

    // 指向下一个成员
    _eptr = ziplistNext(zl,*sptr);
    if (_eptr != NULL) {
        // 指向下一个分值
        _sptr = ziplistNext(zl,_eptr);
        redisAssert(_sptr != NULL);
    } else {
        _sptr = NULL;
    }

    *eptr = _eptr;
    *sptr = _sptr;
}

/**
 * 分别获取 eptr 和 sptr 的前一个成员和分值
 * 
 * 如果前面没有元素了, 两个之后都返回 NULL
 */
void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr) {
    unsigned char *_eptr, *_sptr;

    redisAssert(*eptr != NULL && *sptr != NULL);

    // 指向前一个分值
    _sptr = ziplistPrev(zl,*eptr);
    if (_sptr != NULL) {
        // 指向前一个成员
        _eptr = ziplistPrev(zl,_sptr);
        redisAssert(_eptr != NULL);

    } else {
        _eptr = NULL;
    }

    *eptr = _eptr;
    *sptr = _sptr;
}

/**
 * 只要有一个节点在 range 指定的范围内
 * 函数返回 1, 否则返回 0
 */
int zzlIsInRange(unsigned char *zl, zrangespec *range) {
    double score;
    unsigned char *p;

    // range 范围检查
    if (range->min > range->max || 
        (range->min == range->max && (range->maxex || range->minex)))
        return 0;

    // 超范围, 最大值小于 range 最小值
    p = ziplistIndex(zl,-1);
    if (p == NULL) return 0;
    score = zzlGetScore(p);
    if (!zslValueGteMin(score,range)) 
        return 0;

    // 超范围, 最小值大于 range 最大值
    p = ziplistIndex(zl,1);
    redisAssert(p != NULL);
    score = zzlGetScore(p);
    if (!zslValueLteMax(score,range)) 
        return 0;

    return 1;
}

/**
 * 返回值在给定范围的第一个节点
 * 找到返回 成员节点的指针
 * 未找到返回 NULL
 */
unsigned char *zzlFirstInRange(unsigned char *zl, zrangespec *range) {
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;
    double score;

    redisAssert(eptr != NULL);

    // 是否存在 range 范围的节点
    if (!zzlIsInRange(zl,range)) return NULL;

    // 遍历节点
    while (eptr != NULL) {

        // 定位分值
        sptr = ziplistNext(zl,eptr);
        redisAssert(sptr != NULL);

        // 提取分值
        score = zzlGetScore(sptr);

        // 大于range的节点
        if (zslValueGteMin(score,range)) {
            // 小于 range的最大值
            if (zslValueLteMax(score,range))
                return eptr;
            return NULL;
        }

        // 下一个节点
        eptr = ziplistNext(zl,sptr);
    }

    return NULL;
}

/**
 * 返回值在给定范围的最后一个节点
 * 找到返回成员节点的指针
 * 未找到返回 NULL
 */
unsigned char *zzlLastInRange(unsigned char *zl, zrangespec *range) {
    unsigned char *eptr = ziplistIndex(zl,-2), *sptr;
    double score;

    // 判断 range 范围内是否有节点
    if (!zzlIsInRange(zl,range)) return NULL;

    // 遍历节点
    while (eptr != NULL) {

        // 分值节点
        sptr = ziplistNext(zl,eptr);
        redisAssert(sptr != NULL);

        // 提取分值
        score = zzlGetScore(sptr);

        // 比对分值, 是否在 range 范围内
        if (zslValueLteMax(score,range)) {

            if (zslValueGteMin(score,range))
                return eptr;
            return NULL;
        }

        // 下一个成员节点
        sptr = ziplistPrev(zl,eptr);
        if (sptr != NULL) {
            redisAssert(ziplistPrev(eptr = ziplistPrev(zl,sptr)) != NULL);
        } else {
            eptr = NULL;
        }
    }

    return NULL;
}

/**
 * 节点字符串是否大于 spec.min 的字符串
 * 大于, 返回 1
 * 小于, 返回 0
 */
static int zzlLexValueGteMin(unsigned char *p, zlexrangespec *spec) {
    // 获取 p 节点
    robj *value = ziplistGetObject(p);

    // 比对
    int res = zslLexValueGteMin(value,spec);
    decrRefCount(value);
    return res;
}

/**
 * 节点字符串是否小于 spec.max 的字符串
 * 小于, 返回 1
 * 大于, 返回 0
 */
static int zzlLexValueLteMax(unsigned char *p, zlexrangespec *spec) {
    robj *value = ziplistGetObject(p);
    int res = zslLexValueLteMax(value,spec);
    decrRefCount(value);
    return res;
}

/**
 * zl 的成员节点 在 range 指定范围是否存在节点
 * 按成员进行范围查询时, 使用
 * 存在, 返回 1
 * 不存在, 返回 0
 */
int zzlIsInLexRange(unsigned char *zl, zlexrangespec *range) {
    unsigned char *p;
    // 范围搜索器检查
    if (compareStringObjectsForLexRange(range->min,range->max) > 0 ||
        (compareStringObjectsForLexRange(range->min,range->max) == 0 && 
            (range->minex || range->maxex))) {
                return 0;
    }

    // 超范围, zl 在范围左侧
    p = ziplistIndex(zl,-2);
    if (p == NULL || !zzlLexValueGteMin(p,range))
        return 0;

    // 超范围, zl 在范围右侧
    p = ziplistIndex(zl,0);
    if (p == NULL || !zzlLexValueLteMax(p,range))
        return 0;

    return 1;
}

/**
 * 在 range 范围内的第一个成员节点
 * 未找到返回 NULL
 */
unsigned char *zzlFirstInLexRange(unsigned char *zl, zlexrangespec *range) {
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;

    // zl 在 range 范围内无节点
    if (!zzlIsInLexRange(zl,range)) return NULL;

    // 查找第一个满足 range 的节点
    while (eptr != NULL) {

        // 确定边界
        if (zzlLexValueGteMin(eptr,range)) {
            if (zzlLexValueLteMax(eptr,range))
                return eptr;
            return NULL;
        }

        // 指向下一个成员节点
        sptr = ziplistNext(zl,eptr);
        redisAssert(sptr != NULL);
        eptr = ziplistNext(zl,sptr);
    }

    return NULL;
}

/**
 * 在 range 范围内的最后一个成员节点
 * 未找到返回 NULL
 */
unsigned char *zzlLastInLexRange(unsigned char *zl, zlexrangespec *range) {
    unsigned char *eptr = ziplistIndex(zl,-2), *sptr;

    // zl 在 range 范围内无节点
    if (!zzlIsInLexRange(zl,range)) return NULL;

    while (eptr != NULL) {

        if (zzlLexValueLteMax(eptr,range)) {
            if (zzlLexValueGteMin(eptr,range)) {
                return eptr;
            }
            return NULL;
        }

        // 指向下一个成员节点
        sptr = ziplistPrev(zl,eptr);
        if (sptr != NULL) {
            redisAssert((eptr = ziplistPrev(zl,sptr)) != NULL);
        } else {
            eptr = NULL;
        }
    }

    return NULL;
}

/**
 * 从 ziplist 编码的有序集合中查找 ele 成员, 并将其分值保存到 score
 * 查找成功, 返回 ele 的指针
 * 查找失败, 返回 NULL
 */
unsigned char *zzlFind(unsigned char *zl, robj *ele, double *score) {
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;

    // ele 转换成字符串
    ele = getDecodedObject(ele);

    // 遍历查找 ele 节点
    while (eptr != NULL) {

        // 确定 ele 的分值
        sptr = ziplistNext(zl,eptr);
        redisAssertWithInfo(NULL,ele,sptr != NULL);

        // 找到 ele
        if (ziplistCompare(eptr,ele->ptr,sdslen(ele->ptr))) {
            if (score != NULL) *score = zzlGetScore(eptr);
            decrRefCount(ele);
            return eptr;
        }

        // 移动到下一个 ele
        eptr = ziplistNext(zl,sptr);
    }

    decrRefCount(ele);

    return NULL;
}

/**
 * 从 ziplist 中删除 eptr 说指向的有序集合的元素 (包括成员和分值)
 */
unsigned char *zzlDelete(unsigned char *zl, unsigned char *eptr) {
    unsigned char *p = eptr;

    zl = ziplistDelete(zl,&p);
    zl = ziplistDelete(zl,&p);
    return zl;
}

/**
 * 将成员和分值(ele,score), 添加到 eptr 之前
 * 如果 eptr 为空, 添加到 zl 末端
 */
unsigned char *zzlInsertAt(unsigned char *zl, unsigned char *eptr, robj *ele, double score) {
    unsigned char *sptr;
    char scorebuf[128];
    int scorelen;
    size_t offset;

    // score 转换成字符串
    redisAssertWithInfo(NULL,ele,sdsEncodedObject(ele));
    scorelen = d2string(scorebuf,sizeof(scorebuf),score);

    // 末端添加
    if (eptr == NULL) {
        zl = ziplistPush(zl,ele->ptr,sdslen(ele->ptr),ZIPLIST_TAIL);
        zl = ziplistPush(zl,(unsigned char*)scorebuf,scorelen,ZIPLIST_TAIL);

    // 添加到 eptr 之前
    } else {
        // 添加成员
        offset = eptr - zl;
        zl = ziplistInsert(zl,eptr,ele->ptr,sdslen(ele->ptr));
        eptr = zl + offset;

        // 添加分值
        redisAssertWithInfo(NULL,ele,(sptr = ziplistNext(zl,eptr)) != NULL);
        zl = ziplistInsert(zl,sptr,(unsigned char*)scorebuf,scorelen);
    }

    return zl;
}

/**
 * 将 ele 和 score 添加到 ziplist 里面
 * 
 * 按照 score 从小到大的顺序添加
 */
unsigned char *zzlInsert(unsigned char *zl, robj *ele, double score) {
    unsigned char *eptr = ziplistIndex(zl,0), *sptr;
    double s;

    ele = getDecodedObject(ele);

    // 遍历查找 score
    while (eptr != NULL) {
        // 节点分值
        sptr = ziplistNext(zl,eptr);
        redisAssertWithInfo(NULL,ele,sptr != NULL);
        // 提取分值
        s = zzlGetScore(sptr);

        // 添加
        if (s > score) {
            zl = zzlInsertAt(zl,eptr,ele,score);
            break;

        // 按成员添加
        } else if (s == score) {
            if (zzlCompareElements(eptr,ele->ptr,sdslen(ele->ptr)) > 0) {
                zl = zzlInsertAt(zl,eptr,ele,score);
                break;
            }
        }

        // score 大于当前节点分值, 跳过
        eptr = ziplistNext(zl,sptr);
    }

    if (eptr == NULL) 
        zl = zzlInsertAt(zl,NULL,ele,score);

    decrRefCount(ele);
    return zl;
}

/**
 * 删除 ziplist 中分值在指定范围的元素
 * 
 * 将删除元素数量写入 deleted
 * 
 * 返回处理完成的 zl 首地址
 */
unsigned char *zzlDeleteRangeByScore(unsigned char *zl, zrangespec *range, unsigned long *deleted) {
    unsigned char *eptr, *sptr;
    double score;
    unsigned long num = 0;

    if (deleted != NULL) *deleted = 0;

    // 找到第一个大于 range 的节点
    eptr = zzlFirstInRange(zl,range);
    if (eptr == NULL) return zl;

    while ((sptr = ziplistNext(zl,eptr)) != NULL) {

        // 提取分值
        score = zzlGetScore(sptr);

        // 删除 range 范围内的值
        if (zslValueLteMax(score,range)) {
            zl = ziplistDelete(zl,&eptr);
            zl = ziplistDelete(zl,&eptr);

            num++;
        } else {
            break;
        }
    }

    if (deleted != NULL) *deleted = num;
    return zl;
}

/**
 * 删除 ziplist 中成员在指定范围的元素
 * 
 * 将删除元素数量写入 deleted
 * 
 * 返回处理完成的 zl 首地址
 */
unsigned char *zzlDeleteRangeByLex(unsigned char *zl, zlexrangespec *range, unsigned long *deleted) {
    unsigned char *eptr, *sptr;
    unsigned long num = 0;

    if (deleted != NULL) *deleted = 0;

    // range 范围内的第一个节点
    eptr = zzlFirstInLexRange(zl,range);
    if (eptr == NULL) return zl;

    // 迭代删除 range 范围内的节点
    while ((sptr = ziplistNext(zl,eptr)) != NULL) {

        if (zzlLexValueLteMax(eptr,range)) {
            zl = ziplistDelete(zl,&eptr);
            zl = ziplistDelete(zl,&eptr);
            num++;
        } else {
            break;
        }
    }

    if (deleted != NULL) *deleted = num;
    return zl;
}

/**
 * 按元素的索引 删除 ziplist 中的成员
 * 
 * 删除元素数量写入 deleted
 * 
 * 返回处理完成的 zl 首地址
 */
unsigned char *zzlDeleteRangeByRank(unsigned char *zl, unsigned int start, unsigned int end, unsigned long *deleted) {
    // 计算要删除的节点数量
    unsigned int num = (end-start)+1;

    if (deleted) *deleted = num;

    // rank 第一位索引是 1, ziplist 第一位索引是 0, 所以 start-1 才是ziplist的索引位
    // 索引,元素数量分别 *2, 是因为 ziplist 的 ele,score 存在两个节点中
    zl = ziplistDeleteRange(zl,2*(start-1),num*2);

    return zl;
}


/*-------------------------- 通用 sorted set 方法 -----------------------------*/
/**
 * 有序集合节点数量
 */
unsigned int zsetLength(robj *zobj) {
    int length = -1;

    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        length = zzlLength(zobj->ptr);

    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        length = ((zset*)zobj->ptr)->zsl->length;

    } else {
        redisPanic("Unknown sorted set encoding");
    }

    return length;
}

/**
 * 内部编码转换
 */
void zsetConvert(robj *zobj, int encoding) {
    zset *zs;
    zskiplistNode *node, *next;
    robj *ele;
    double score;

    // ZIPLIST 转 SKIPLIST
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        
        if (encoding != REDIS_ENCODING_SKIPLIST)
            redisPanic("Unknown target encoding");

        // 创建空 skiplist
        zs = zmalloc(sizeof(*zs));
        zs->dict = dictCreate(&zsetDictType,NULL);
        zs->zsl = zslCreate();

        // 第一个节点的成员和分值
        eptr = ziplistIndex(zobj->ptr,0);
        redisAssertWithInfo(NULL,zobj,eptr != NULL);
        sptr = ziplistNext(zobj->ptr,eptr);
        redisAssertWithInfo(NULL,zobj,sptr != NULL);

        // 拷贝节点
        while (eptr != NULL) {
            // 提取分值
            score = zzlGetScore(sptr);

            // 提取成员
            redisAssertWithInfo(NULL,zobj,ziplistGet(eptr,&vstr,&vlen,&vlong));
            if (vstr) {
                ele = createStringObject(vstr,vlen);
            } else {
                ele = createStringObjectFromLongLong(vlong);
            }

            // 添加到 skiplist
            node = zslInsert(zs->zsl,score,ele);
            redisAssertWithInfo(NULL,zobj,dictAdd(zs->dict,ele,&node->score) == DICT_OK);
            incrRefCount(ele);

            // 下一个节点
            zzlNext(zl,&eptr,&sptr);
        }

        // 更新编码
        zobj->encoding = REDIS_ENCODING_SKIPLIST;

        // 释放 ziplist 结构
        zfree(zobj->ptr);

        // 绑定 skiplist 结构
        zobj->ptr = zs;

    // SKIPLIST 转 ZIPLIST
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {

        // 创建 ziplist
        unsigned char *zl = ziplistNew();

        if (encoding != REDIS_ENCODING_ZIPLIST)
            redisPanic("Unknown target encoding");

        zs = zobj->ptr;

        // 释放字典
        dictRelease(zs->dict);

        // 指向第一个节点
        node = zs->zsl->header->level[0].forward;

        // 释放跳跃表表头
        zfree(zs->zsl->header);
        zfree(zs->zsl);

        // 拷贝节点
        while (node) {

            ele = getDecodedObject(node->obj);

            // 添加节点到 ziplist
            zl = zzlInsertAt(zl,NULL,ele,node->score);
            decrRefCount(ele);

            // 下一个节点, 释放当前节点
            next = node->level[0].forward;
            zslFreeNode(node);
            node = next;
        }

        // 更新编码
        zobj->encoding = REDIS_ENCODING_ZIPLIST;

        // 释放 skiplist
        zfree(zs);

        // 绑定 ziplist
        zobj->ptr = zl;

    } else {
        redisPanic("Unknown sorted set encoding");
    }
}

/*-------------------------- sorted set 命令 -----------------------------*/

// ZADD 和 ZINCRBY 的通用函数
void zaddGenericCommand(redisClient *c, int incr) {
    
    static char *nanerr = "resulting score is not a number (NaN)";

    robj *key = c->argv[1];
    robj *zobj, *ele, *curobj;
    double score = 0, curscore = 0.0, *scores = NULL;
    int j, elements = (c->argc-2)/2;
    int added = 0, updated = 0;

    // 检查参数数量
    if (c->argc % 2) {
        addReply(c,shared.syntaxerr);
        return;
    }

    // 取出所有 score 值加到数组
    scores = zmalloc(sizeof(double*)*elements);
    for (j = 0; j < elements; j++) {
        if (getDoubleFromObjectOrReply(c,c->argv[2+j*2],&scores[j],NULL) 
            != REDIS_OK) goto cleanup;
    }

    // 提取有序集合对象
    zobj = lookupKeyWrite(c->db,key);
    if (zobj == NULL) {

        // 有序集合不存在, 创建一个
        if (server.zset_max_ziplist_entries == 0 ||
            server.zset_max_ziplist_value < sdslen(c->argv[3]->ptr)) 
        {    
            zobj = createZsetObject();
        } else {
            zobj = createZsetZiplistObject();
        }

        dbAdd(c->db,key,zobj);

    } else {

        // 有序集合存在, 检查类型
        if (zobj->type != REDIS_ZSET) {
            addReply(c,shared.wrongtypeerr);
            goto cleanup;
        }
    }

    // 添加元素
    for (j = 0; j < elements; j++) {
        // 提取分数
        score = scores[j];

        // ziplist
        if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
            unsigned char *eptr;
            
            // 查找成员
            ele = c->argv[3+j*2];
            if (eptr = zzlFind(zobj->ptr,ele,&curscore)) {
                // 元素存在

                // 计算累加后的值
                if (incr) {
                    score += curscore;
                    if (isnan(score)) {
                        addReplyError(c,nanerr);
                        goto cleanup;
                    }
                }

                // 更新分值
                if (score != curscore) {
                    // 删除原元素
                    zobj->ptr = zzlDelete(zobj->ptr,eptr);
                    // 添加新元素
                    zobj->ptr = zzlInsert(zobj->ptr,ele,score);

                    server.dirty++;
                    updated++;
                }

            } else {
                // 元素不存在
                // 添加新元素
                zobj->ptr = zzlInsert(zobj->ptr,ele,score);

                // 是否需要转码
                if (sdslen(ele->ptr) > server.zset_max_ziplist_value)
                    zsetConvert(zobj,REDIS_ENCODING_SKIPLIST);

                if (zzlLength(zobj->ptr) > server.zset_max_ziplist_entries)
                    zsetConvert(zobj,REDIS_ENCODING_SKIPLIST);

                server.dirty++;
                added++;
            }

        // zset
        } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
            // 
            zset *zs = zobj->ptr;
            zskiplistNode *znode;
            dictEntry *de;
            ele = c->argv[3+j*2] = tryObjectEncoding(c->argv[3+j*2]);

            // 查找元素
            de = dictFind(zs->dict,ele);
            if (de != NULL) {
                // 元素已存在

                // 取出成员
                curobj = dictGetKey(de);
                // 取出分值
                curscore = *(double*)dictGetVal(de);

                // 计算 incr 后的分值, ZINCRBY时执行
                if (incr) {
                    score += curscore;
                    if (isnan(score)) {
                        addReplyError(c,nanerr);
                        goto cleanup;
                    }
                }

                // 更新元素
                if (score != curscore) {
                    // 删除旧元素从 skiplist
                    redisAssertWithInfo(c,curobj,zslDelete(zs->zsl,curscore,curobj));
                    
                    // 添加新元素到 skiplist
                    znode = zslInsert(zs->zsl,score,curobj);
                    incrRefCount(curobj);

                    // 更新元素的分值, 从 dict
                    dictGetVal(de) = &znode->score;

                    server.dirty++;
                    updated++;
                }

            } else {

                // 元素不存在

                // 添加元素到 skiplist
                znode = zslInsert(zs->zsl,score,ele);
                incrRefCount(ele);

                // 添加元素到 dict
                redisAssertWithInfo(c,NULL,dictAdd(zs->dict,ele,&znode->score) == DICT_OK);
                incrRefCount(ele);

                server.dirty++;
                added++;
            }

        } else {
            redisPanic("Unknown sorted set encoding");
        }
    }

    // 回复客户端
    if (incr) {
        addReplyDouble(c,score);
    } else {
        addReplyLongLong(c,added);
    }

    // 异常退出程序
cleanup:
    zfree(scores);
    if (added || updated) {
        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_ZSET,
            incr ? "zincrby" : "zadd", key, c->db->id);
    }
}

void zaddCommand(redisClient *c) {
    zaddGenericCommand(c,0);
}

void zincrbyCommand(redisClient *c) {
    zaddGenericCommand(c,1);
}

// ZREM key member [member ...]
void zremCommand(redisClient *c) {
    robj *key = c->argv[1];
    robj *zobj;
    int deleted = 0, keyremoved = 0, j;

    // 取出有序集合对象
    if ((zobj = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) return;

    // 删除元素
    // ziplist
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *eptr;

        for (j = 2; j < c->argc; j++) {
            // 查找
            if (eptr = zzlFind(zobj->ptr,c->argv[j],NULL)) {

                zobj->ptr = zzlDelete(zobj->ptr,eptr);
                
                deleted++;

                // 删空了
                if (zzlLength(zobj->ptr) == 0) {
                    dbDelete(c->db,key);
                    break;
                }
            }
        }

    // skiplist
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        dictEntry *de;
        double score;

        for (j = 2; j < c->argc; j++) {
        
            
            de = dictFind(zs->dict,c->argv[j]);

            if (de != NULL) {

                deleted++;

                // 取出分值
                score = *(double*)dictGetVal(de);

                // 删除 skiplist 中节点
                redisAssertWithInfo(c,c->argv[j],zslDelete(zs->zsl,score,c->argv[j]));
                // 删除 dict 中节点
                dictDelete(zs->dict,c->argv[j]);

                // 检查是否需要缩小字典
                if (htNeedsResize(zs->dict)) dictResize(zs->dict);

                // 删空了
                if (dictSize(zs->dict) == 0) {
                    dbDelete(c->db,key);
                    break;
                }
            }
        }

    } else {
        redisPanic("Unknown sorted set encoding");
    }


    // 发出通知
    if (deleted) {
        signalModifiedKey(c->db,key);

        notifyKeyspaceEvent(REDIS_NOTIFY_ZSET,"zrem",key,c->db->id);

        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",key,c->db->id);

        server.dirty += deleted;
    }

    // 回复客户端
    addReplyLongLong(c,deleted);
}

#define ZRANGE_RANK 0
#define ZRANGE_SCORE 1
#define ZRANGE_LEX 2


void zremrangeGenericCommand(redisClient *c, int rangetype) {
    robj *key = c->argv[1];
    robj *zobj;
    int keyremoved = 0;
    unsigned long deleted;
    zrangespec range;
    zlexrangespec lexrange;
    long start, end, llen;

    // 解析搜索条件, 获取范围搜索器
    if (rangetype == ZRANGE_RANK) {
        if ((getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != REDIS_OK) ||
            (getLongFromObjectOrReply(c,c->argv[3],&end,NULL) != REDIS_OK))
            return;
    } else if (rangetype == ZRANGE_SCORE) {
        if (zslParseRange(c->argv[2],c->argv[3],&range) != REDIS_OK) {
            addReplyError(c,"min or max is not a float");
            return;
        }
    } else if (rangetype == ZRANGE_LEX) {
        if (zslParseLexRange(c->argv[2],c->argv[3],&lexrange) != REDIS_OK) {
            addReplyError(c,"min or max not valid string range item");
            return;
        }
    }

    // 取出有序集合对象
    if ((zobj = lookupKeyReadOrReply(c,key,shared.czero)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) goto cleanup;

    // 索引搜索, 将负索引转正索引
    if (rangetype == ZRANGE_RANK) {

        llen = zsetLength(zobj);
        if (start < 0) start = llen+start;
        if (end < 0) end = llen+end;
        if (start < 0) start = 0;

        if (start > end || start >= llen) {
            addReply(c,shared.czero);
            goto cleanup;
        }

        if (end >= llen) end = llen-1;
    }

    // 删除节点
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        switch(rangetype){
        case ZRANGE_RANK: 
            zobj->ptr = zzlDeleteRangeByRank(zobj->ptr,start+1,end+1,&deleted); 
            break;
        case ZRANGE_SCORE: 
            zobj->ptr = zzlDeleteRangeByScore(zobj->ptr,&range,&deleted); 
            break;
        case ZRANGE_LEX: 
            zobj->ptr = zzlDeleteRangeByLex(zobj->ptr,&lexrange,&deleted); 
            break;
        }

        // 删空了
        if (zzlLength(zobj->ptr) == 0) {
            dbDelete(c->db,key);
            keyremoved = 1;
        }

    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;

        switch(rangetype){
        case ZRANGE_RANK: 
            deleted = zslDeleteRangeByRank(zs->zsl,start+1,end+1,zs->dict); 
            break;
        case ZRANGE_SCORE: 
            deleted = zslDeleteRangeByScore(zs->zsl,&range,zs->dict); 
            break;
        case ZRANGE_LEX: 
            deleted = zslDeleteRangeByLex(zs->zsl,&lexrange,zs->dict); 
            break;
        }

        if (htNeedsResize(zs->dict)) dictResize(zs->dict);

        // 删空了
        if (dictSize(zs->dict) == 0) {
            dbDelete(c->db,key);
            keyremoved = 1;
        }

    } else {
        redisPanic("Unknown sorted set encoding");
    }

    // 发送通知
    if (deleted) {
        char *event[3] = {"zremrangebyrank","zremrangebyscore","zremrangebylex"};
        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_ZSET,event[rangetype],key,c->db->id);

        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",key,c->db->id);
    }

    server.dirty += deleted;

    // 回复客户端
    addReplyLongLong(c,deleted);

cleanup:
    if (rangetype == ZRANGE_LEX) zslFreeLexRange(&lexrange);
}

// ZREMRANGEBYRANK key start stop
void zremrangebyrankCommand(redisClient *c) {
    zremrangeGenericCommand(c,ZRANGE_RANK);
}

// ZREMRANGEBYSCORE key min max
void zremrangebyscoreCommand(redisClient *c) {
    zremrangeGenericCommand(c,ZRANGE_SCORE);
}

// ZREMRANGEBYLEX key min max
void zremrangebylexCommand(redisClient *c) {
    zremrangeGenericCommand(c,ZRANGE_LEX);
}

/*-------------------------- uninstore interstore 命令 -----------------------------*/

void zrangeGenericCommand(redisClient *c, int reverse) {
    robj *key = c->argv[1];
    robj *zobj;
    int withscores = 0;
    long start, end;
    int llen, rangelen;

    // 取范围
    if ((getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != REDIS_OK) ||
        (getLongFromObjectOrReply(c,c->argv[3],&end,NULL) != REDIS_OK))
        return;

    // 是否带分值
    if (c->argc == 5 && !strcasecmp(c->argv[4]->ptr,"withscores")) {
        withscores = 1;
    } else if (c->argc >= 5) {
        addReply(c,shared.syntaxerr);
        return;
    }

    // 取出有序集合对象
    if ((zobj = lookupKeyReadOrReply(c,key,shared.emptymultibulk) == NULL ||
        checkType(c,zobj,REDIS_ZSET))) return;

    // 清洗索引,计算索引长度
    llen = zsetLength(zobj);
    if (start < 0) start += llen;
    if (end < 0) end += llen;
    if (start < 0) start = 0;

    if (start > end || start >= llen) {
        addReply(c,shared.outofrangeerr);
        return;
    }
    
    if (end >= llen) end = llen-1;
    rangelen = (end-start)+1;

    
    // 取出范围内的节点, 发送给客户端
    addReplyMultiBulkLen(c, withscores ? (rangelen*2) : rangelen);
    // ziplist
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        // 起始节点的成员和分值
        if (reverse)
            eptr = ziplistIndex(zl,-2-(start*2));
        else 
            eptr = ziplistIndex(zl,start*2);
        
        redisAssertWithInfo(c,zobj,eptr != NULL);
        sptr = ziplistNext(zl,eptr);

        // 遍历取节点
        while (rangelen--) {
            redisAssertWithInfo(c,zobj,eptr != NULL && sptr != NULL);
            redisAssertWithInfo(c,zobj,ziplistGet(eptr,&vstr,&vlen,&vlong));

            // 回复客户端
            if (vstr) {
                addReplyBulkCBuffer(c,vstr,vlen);
            } else {
                addReplyLongLong(c,vlong);
            }

            if (withscores)
                addReplyDouble(c,zzlGetScore(sptr));

            // 处理下一个节点
            if (reverse)
                zzlPrev(zl,&eptr,&sptr);
            else
                zzlNext(zl,&eptr,&sptr);
        }


    // skiplist
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *ln;
        robj *ele;

        // 起始节点
        if (reverse) {
            ln = zsl->tail;
            if (start > 0)
                ln = zslGetElementByRank(zsl,llen-start);
        } else {
            ln = zsl->header->level[0].forward;
            if (start > 0)
                ln = zslGetElementByRank(zsl,start+1);
        }

        // 遍历节点
        while (rangelen--) {
            redisAssertWithInfo(c,zobj,ln != NULL);

            // 回复客户端
            ele = ln->obj;
            addReplyBulk(c,ele);

            if (withscores)
                addReplyLongLong(c,ln->score);

            // 下一个节点
            ln = reverse ? ln->backward : ln->level[0].forward;
        }

    } else {
        redisPanic("Unknown sorted set encoding");
    }
}

void zrangeCommand(redisClient *c) {
    zrangeGenericCommand(c,0);
}

void zrevrangeCommand(redisClient *c) {
    zrangeGenericCommand(c,1);
}

void genericZrangebyscoreCommand(redisClient *c, int reverse) {
    zrangespec range;
    robj *key = c->argv[1];
    robj *zobj;
    long offset = 0, limit = -1;
    int withscores  = 0;
    unsigned long rangelen = 0;
    void *replylen = NULL;
    int minidx, maxidx;

    // 确定 min 和 max 的位置, 因为 ZRANGEBYSCORE 和 ZREVRANGEBYSCORE
    // 的 min 和 max 位置不一样
    if (reverse) {
        minidx = 3; maxidx = 2;
    } else {
        minidx = 2; maxidx = 3;
    }

    // 创建范围搜索器
    if (zslParseRange(c->argv[minidx],c->argv[maxidx],&range) != REDIS_OK) {
        addReplyError(c,"min or max is not a float");
        return;
    }

    // 取出其他参数
    if (c->argc > 4) {
        int remaining = c->argc - 4;
        int pos = 4;

        while (remaining) {
            if (remaining >= 1 && !strcasecmp(c->argv[pos]->ptr,"withscores")) {
                pos++; remaining--;
                withscores = 1;

            } else if (remaining >= 3 && !strcasecmp(c->argv[pos]->ptr,"limit")) {
                if ((getLongFromObjectOrReply(c,c->argv[pos+1],&offset,NULL) != REDIS_OK) ||
                    (getLongFromObjectOrReply(c,c->argv[pos+2],&limit,NULL) != REDIS_OK)) return;
                pos += 3; remaining -= 3;

            } else {
                addReply(c,shared.syntaxerr);
                return;
            }
        }
    }

    // 取出有序集合对象
    if ((zobj = lookupKeyReadOrReply(c,key,shared.emptymultibulk)) == NULL ||
        checkType(c,zobj,REDIS_ZSET)) return;

    // 提取范围内节点, 回复客户端
    // ziplist
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        double score;

        // 起始节点
        if (reverse)
            eptr = zzlLastInRange(zl,&range);
        else
            eptr = zzlFirstInRange(zl,&range);

        if (eptr == NULL) {
            addReply(c,shared.emptymultibulk);
            return;
        }
        redisAssertWithInfo(c,zobj,eptr != NULL);
        sptr = ziplistNext(zl,eptr);

        replylen = addDeferredMultiBulkLength(c);

        // 跳过 offset 节点
        while (eptr && offset--) {
            if (reverse)
                zzlPrev(zl,&eptr,&sptr);
            else
                zzlNext(zl,&eptr,&sptr);
        }

        // 提取节点
        while (eptr && limit--) {

            score = zzlGetScore(sptr);

            // 范围检查
            if (reverse) {
                if (!zslValueGteMin(score,&range)) break;
            } else {
                if (!zslValueLteMax(score,&range)) break;
            }

            // 回复客户端
            redisAssertWithInfo(c,zobj,ziplistGet(eptr,&vstr,&vlen,&vlong));
            rangelen++;

            if (vstr) {
                addReplyBulkCBuffer(c,vstr,vlen);
            } else {
                addReplyLongLong(c,vlong);
            }

            if (withscores)
                addReplyDouble(c,score);
            
            // 处理下一个节点
            if (reverse)
                zzlPrev(zl,&eptr,&sptr);
            else
                zzlNext(zl,&eptr,&sptr);

        }

    // skiplist
    } else if (zobj->encoding == REDIS_ENCODING_SKIPLIST) {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *ln;

        // 起始节点
        if (reverse) {
            ln = zslLastInRange(zsl,&range);
        } else {
            ln = zslFirstInRange(zsl,&range);
        }

        if (ln == NULL) {
            addReply(c,shared.emptymultibulk);
            return;
        }

        replylen = addDeferredMultiBulkLength(c);

        // 跳过 offset 节点
        while (ln && offset--) {
            if (reverse)
                ln = ln->backward;
            else
                ln = ln->level[0].forward;
        }

        // 提取节点
        while (ln && limit--) {

            // 范围检查
            if (reverse) {
                if (!zslValueGteMin(ln->score,&range)) break;
            } else {
                if (!zslValueLteMax(ln->score,&range)) break;
            }

            rangelen++;

            // 回复客户端
            addReplyBulk(c,ln->obj);

            if (withscores)
                addReplyDouble(c,ln->score);

            // 处理下一个节点
            if (reverse)
                ln = ln->backward;
            else
                ln = ln->level[0].forward;
        }

    } else {
        redisPanic("Unknown sorted set encoding");
    }

    if (withscores)
        rangelen *= 2;

    setDeferredMultiBulkLength(c, replylen, rangelen);
}

// ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]
void zrangebyscoreCommand(redisClient *c) {
    genericZrangebyscoreCommand(c,0);
}

// ZREVRANGEBYSCORE key max min [WITHSCORES] [LIMIT offset count]
void zrevrangebyscoreCommand(redisClient *c) {
    genericZrangebyscoreCommand(c,1);
}