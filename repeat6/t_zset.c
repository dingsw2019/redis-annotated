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




//gcc -g zmalloc.c sds.c t_zset.c
int main(void) {

    srand((unsigned)time(NULL));

    unsigned long ret;
    zskiplistNode *node;
    zskiplist *zsl = zslCreate();

    // // 添加节点
    // zslInsert(zsl, 65.5, sdsnew("tom"));     // 3
    // zslInsert(zsl, 69.5, sdsnew("wangwu"));  // 4
    // zslInsert(zsl, 87.5, sdsnew("jack"));    // 6
    // zslInsert(zsl, 20.5, sdsnew("zhangsan"));// 1
    // zslInsert(zsl, 70.0, sdsnew("alice"));   // 5
    // zslInsert(zsl, 39.5, sdsnew("lisi"));    // 2
    // zslInsert(zsl, 95.0, sdsnew("tony"));    // 7

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