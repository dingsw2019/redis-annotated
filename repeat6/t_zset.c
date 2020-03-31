#include "redis.h"

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

// 获取随机层数
int zslRandomLevel(void) {
    int level = 1;
    // 随机数大于 1/4的 int 值
    while((rand() & 0xFFFF) < (ZSKIPLIST_P * 0xFFFF)) {
        level++;
    }

    return (level < ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
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
                 && sdscmp(ele, x->level[i].forward->ele)<0) )) 
        {
            x = x->level[i].forward;
            rank[i] += x->level[i].span;
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
        update[i]->level[i].span += 1;
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