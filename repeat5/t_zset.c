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
                (x->level[i].forward->score < score) ||
                 (x->level[i].forward->score == score && 
                  sdscmp(x->level[i].forward->ele,ele)< 0 ))
        {
            x = x->level[i].forward;
        }
        update[i] = x;
    }
    // 比对代码前,没有的代码 myerr
    x = x->level[0].forward;


    // 找到目标节点
    if (x && x->score == score && sdscmp(x->ele,ele)) {
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