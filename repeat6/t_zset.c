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

    // //定义一个区间， 70.0 <= x <= 90.0
    // zrangespec range1 = {       
    //     .min = 70.0,
    //     .max = 90.0,
    //     .minex = 0,
    //     .maxex = 0
    // };

    // // 找到符合区间的最小值
    // printf("zslFirstInRange 70.0 <= x <= 90.0, x is:");
    // node = zslFirstInRange(zsl, &range1);
    // printf("%s->%f\n", node->ele, node->score);

    // // 找到符合区间的最大值
    // printf("zslLastInRange 70.0 <= x <= 90.0, x is:");
    // node = zslLastInRange(zsl, &range1);
    // printf("%s->%f\n", node->ele, node->score);

    // // 根据分数获取排名
    // printf("tony's Ranking is :");
    // ret = zslGetRank(zsl, 95.0, sdsnew("tony"));
    // printf("%lu\n", ret);

    // // 根据排名获取分数
    // printf("The Rank equal 4 is :");
    // node = zslGetElementByRank(zsl, 4);
    // printf("%s->%f\n", node->ele, node->score);

    // // 分值范围删除节点
    // zrangespec range2 = {       
    //     .min = 20.0,
    //     .max = 40.0,
    //     .minex = 0,
    //     .maxex = 0
    // };
    // ret = zslDeleteRangeByScore(zsl,&range2);
    // printf("zslDeleteRangeByScore 20.0 <= x <= 40.0, removed : %d\n",ret);

    // // 索引范围删除节点
    // ret = zslDeleteRangeByRank(zsl,1,2);
    // printf("zslDeleteRangeByRank 1 & 2 index, removed : %d\n",ret);

    // ret = zslDelete(zsl, 70.0, sdsnew("alice"), &node);  // 删除元素
    // if (ret == 1) {
    //     printf("Delete node:%s->%f success!\n", node->ele, node->score);
    // }
    
    return 0;
}