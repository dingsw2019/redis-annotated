#ifndef __REDIS_H
#define __REDIS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sds.h"
#include "dict.h"
#include "adlist.h"
#include "zmalloc.h"
#include "ziplist.h"
#include "intset.h"
#include "util.h"

#define REDIS_OK 0
#define REDIS_ERR -1

/* 默认的服务器配置值*/

#define REDIS_SHARED_INTEGERS 10000  /* redis字符串对象的整数编码的共享整数范围(1~10000) */
#define REDIS_SHARED_SELECT_CMDS 10

// 对象类型
#define REDIS_STRING 0
#define REDIS_LIST 1
#define REDIS_SET 2
#define REDIS_ZSET 3
#define REDIS_HASH 4

// 对象编码
#define REDIS_ENCODING_RAW 0
#define REDIS_ENCODING_INT 1
#define REDIS_ENCODING_HT 2
#define REDIS_ENCODING_ZIPMAP 3
#define REDIS_ENCODING_LINKEDLIST 4
#define REDIS_ENCODING_ZIPLIST 5
#define REDIS_ENCODING_INTSET 6
#define REDIS_ENCODING_SKIPLIST 7
#define REDIS_ENCODING_EMBSTR 8


/*--------------------- 压缩列表 -----------------------*/

#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

// 跳跃表节点 (level必须放在最后一个)
typedef struct zskiplistNode 
{
    // 对象成员
    sds ele;

    // 分值
    double score;

    // 后退指针
    struct zskiplistNode *backward;

    // 层
    struct zskiplistLevel {
        // 前进指针
        struct zskiplistNode *forward;
        // 跨度
        unsigned int span;
    } level[];

} zskiplistNode;

// 跳跃表
typedef struct zskiplist
{
    // 指向首尾节点的指针
    struct zskiplistNode *header, *tail;
    // 节点数量
    unsigned long length;
    // 最大层数的节点的层数
    int level;
} zskiplist;

// 表示开区间/闭区间范围的结构
typedef struct 
{
    // 最小值和最大值
    double min, max;

    // 指示最小值和最大值是否不包含在范围内
    // 值为 1 表示不包含, 值为 0 表示包含
    int minex, maxex;
} zrangespec;

// LRU 是Least Recently Used的缩写
// 即最近最少使用，是一种常用的页面置换算法，选择最近最久未使用的页面予以淘汰。 
#define REDIS_LRU_BITS 24
#define REDIS_LRU_CLOCK_MAX ((1<<REDIS_LRU_BITS)-1) /* robj->lru 的最大值 */
#define REDIS_LRU_CLOCK_RESOLUTION 1000
#define REDIS_SHARED_BULKHDR_LEN 32

// redis对象
typedef struct redisObject {

    // 对象类型(从使用者角度定义的类型)
    unsigned type:4;

    // 值对象的编码方式
    unsigned encoding:4;

    // 对象最后一次被访问的时间
    // 用于计算对象空转时长
    // 设置 maxmemory, 会优先释放空转时间长的对象
    unsigned lru:REDIS_LRU_BITS;

    // 引用计数
    int refcount;

    // 指向实际指针
    void *ptr;
} robj;

// todo 暂时简化
#define LRU_CLOCK() 1000


// 通过复用来减少内存碎片, 以及减少操作耗时的共享对象
struct sharedObjectsStruct {
    robj *crlf, *ok, *err, *emptybulk, *czero, *cone, *cnegone, *pong, *space,
    *colon, *nullbulk, *nullmultibulk, *queued,
    *emptymultibulk, *wrongtypeerr, *nokeyerr, *syntaxerr, *sameobjecterr,
    *outofrangeerr, *noscripterr, *loadingerr, *slowscripterr, *bgsaveerr,
    *masterdownerr, *roslaveerr, *execaborterr, *noautherr, *noreplicaserr,
    *busykeyerr, *oomerr, *plus, *messagebulk, *pmessagebulk, *subscribebulk,
    *unsubscribebulk, *psubscribebulk, *punsubscribebulk, *del, *rpop, *lpop,
    *lpush, *emptyscan, *minstring, *maxstring,
    *select[REDIS_SHARED_SELECT_CMDS],
    *integers[REDIS_SHARED_INTEGERS],
    *mbulkhdr[REDIS_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
    *bulkhdr[REDIS_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
};

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/
extern struct sharedObjectsStruct shared;

/* Redis 对象实现 */
void decrRefCount(robj *o);
void decrRefCountVoid(void *o);
void incrRefCount(robj *o);
robj *resetRefCount(robj *obj);
void freeStringObject(robj *o);
void freeListObject(robj *o);
// void freeSetObject(robj *o);
// void freeZsetObject(robj *o);
// void freeHashObject(robj *o);
robj *createObject(int type, void *ptr);
robj *createStringObject(char *ptr, size_t len);
robj *createRawStringObject(char *ptr, size_t len);;
robj *createEmbeddeStringObject(char *ptr, size_t len);
robj *dupStringObject(robj *o);

robj *createStringObjectFromLongLong(long long value);
// robj *createStringObjectFromLongDouble(long double value);




#endif