
#define REDIS_LRU_BITS 24

// redis对象
typedef struct redisObject
{
    // 类型
    unsigned type:4;
    
    // 编码
    unsigned encoding:4;

    // 对象最后一次被访问的时间
    unsigned lru:REDIS_LRU_BITS;

    // 引用计数
    int refcount;

    // 指向实际值的指针
    void *ptr;
} robj;

// 跳跃表节点
typedef struct zskiplistNode 
{
    // 层
    struct zskiplistLevel 
    {
        // 前进指针
        struct zskiplistNode *forward;
        // 跨度
        unsigned int span;
    } level[];

    // 后退指针
    struct zskiplistNode *backward;

    // 分数
    double score;

    // 对象成员
    robj *obj;
} zskiplistNode;

// 跳跃表
typedef struct zskiplist
{
    // 指向首尾节点指针
    struct zskiplistNode *header, *tail;
    // 最大层数节点的层数
    int level;
    // 节点数量
    unsigned long length;
};
