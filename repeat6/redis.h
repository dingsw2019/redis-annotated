#define MAX_LEVEL 32

#define REDIS_LRU_BITS 24

// redis 对象结构
typedef struct redisObject 
{
    // 类型
    unsigned type:4;
    // 编码
    unsigned encoding:4;
    // 最后一次被访问的时间
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

    // 分值
    double score;

    // 对象成员
    robj *obj;
} zskiplistNode;

// 跳跃表
typedef struct zskiplist 
{
    // 指向首尾节点的指针
    struct zskiplistNode *header, *tail;
    // 节点数量
    unsigned long length;
    // 最大的层数
    int level;
} zskiplist;