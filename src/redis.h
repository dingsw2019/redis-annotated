#ifndef __REDIS_H
#define __REDIS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
// #include <syslog.h>
// #include <netinet/in.h>
// #include <lua.h>
#include <signal.h>

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

/* 客户端标识标志 redisClient->flags */
#define REDIS_MULTI (1<<3)

/* 客户端阻塞状态 */
#define REDIS_BLOCKED_NONE 0
#define REDIS_BLOCKED_LIST 1
#define REDIS_BLOCKED_WAIT 2

/* 双端链表的方向 */
#define REDIS_HEAD 0
#define REDIS_TAIL 1

// todo 待完善
#define redisAssertWithInfo(_c,_o,_e) _exit(1)
#define redisAssert(_e) _exit(1)
#define redisPanic(_e) _exit(1)

typedef long long mstime_t; /* 毫秒 */

// LRU 是Least Recently Used的缩写
// 即最近最少使用，是一种常用的页面置换算法，选择最近最久未使用的页面予以淘汰。 
#define REDIS_LRU_BITS 24
#define REDIS_LRU_CLOCK_MAX ((1<<REDIS_LRU_BITS)-1) /* robj->lru 的最大值 */
#define REDIS_LRU_CLOCK_RESOLUTION 1000
#define REDIS_SHARED_BULKHDR_LEN 32

// 过期时间的单位, 秒或毫秒
#define UNIT_SECONDS 0
#define UNIT_MILLISECONDS 1

/* Keyspace changes notification classes. Every class is associated with a
 * character for configuration purposes. 
 * 密钥变更通知类常量, 每个常量对应一个字符
 */
#define REDIS_NOTIFY_KEYSPACE (1<<0)    /* K */
#define REDIS_NOTIFY_KEYEVENT (1<<1)    /* E */
#define REDIS_NOTIFY_GENERIC (1<<2)     /* g */
#define REDIS_NOTIFY_STRING (1<<3)      /* $ */
#define REDIS_NOTIFY_LIST (1<<4)        /* l */
#define REDIS_NOTIFY_SET (1<<5)         /* s */
#define REDIS_NOTIFY_HASH (1<<6)        /* h */
#define REDIS_NOTIFY_ZSET (1<<7)        /* z */
#define REDIS_NOTIFY_EXPIRED (1<<8)     /* x */
#define REDIS_NOTIFY_EVICTED (1<<9)     /* e */
#define REDIS_NOTIFY_ALL (REDIS_NOTIFY_GENERIC | REDIS_NOTIFY_STRING | REDIS_NOTIFY_LIST | REDIS_NOTIFY_SET | REDIS_NOTIFY_HASH | REDIS_NOTIFY_ZSET | REDIS_NOTIFY_EXPIRED | REDIS_NOTIFY_EVICTED)      /* A */


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

/**
 * 列表迭代器对象
 */
typedef struct {

    // 列表对象
    robj *subject;

    // 对象使用的编码
    unsigned char encoding;

    // 迭代的方向
    unsigned char direction;

    // ziplist 索引, 迭代 ziplist 编码的列表时使用
    unsigned char *zi;

    // 链表节点的指针, 迭代双端链表编码的列表时使用
    listNode *ln;

} listTypeIterator;

/**
 * 迭代列表时用来存储节点
 */
typedef struct {

    // 列表迭代器
    listTypeIterator *li;

    // 压缩列表节点
    unsigned char *zi;

    // 双端链表节点
    listNode *ln;
    
} listTypeEntry;

/*
 * 哈希对象的迭代器
 */
typedef struct {

    // 被迭代的哈希对象
    robj *subject;

    // 哈希对象的编码
    int encoding;

    // 域指针和值指针
    // 在迭代 ZIPLIST 编码的哈希对象时使用
    unsigned char *fptr, *vptr;

    // 字典迭代器和指向当前迭代字典节点的指针
    // 在迭代 HT 编码的哈希对象时使用
    dictIterator *di;
    dictEntry *de;
} hashTypeIterator;

#define REDIS_HASH_KEY 1
#define REDIS_HASH_VALUE 2

// todo 暂时简化
// #define LRU_CLOCK() ((1000/server.hz <= REDIS_LRU_CLOCK_RESOLUTION) ? server.lruclock : getLRUClock())
#define LRU_CLOCK() (getLRUClock())

typedef struct redisDb {

    // 数据库键空间, 保存所有键值对
    dict *dict;

    // 键的过期时间, 字典键为键, 字典的值为过期时间 UNIX 时间戳
    dict *expires;

    // 正处于阻塞状态的键
    dict *blocking_keys;

    // 可以解除阻塞的键
    dict *ready_keys;

    // 正在被 WATCH 命令监视的键
    dict *watched_keys;

    // 数据库号码
    int id;

    // 数据库的键的平均 TTL, 统计信息
    long long avg_ttl;

} redisDb;

// 阻塞状态
typedef struct blockingState {

    /* Generic fields. */
    // 阻塞时限
    mstime_t timeout;       /* Blocking operation timeout. If UNIX current time
                             * is > timeout then the operation timed out. */

    /* REDIS_BLOCK_LIST */
    // 造成阻塞的键
    dict *keys;             /* The keys we are waiting to terminate a blocking
                             * operation such as BLPOP. Otherwise NULL. */
    // 在被阻塞的键有新元素进入时，需要将这些新元素添加到哪里的目标键
    // 用于 BRPOPLPUSH 命令
    robj *target;           /* The key that should receive the element,
                             * for BRPOPLPUSH. */

    /* REDIS_BLOCK_WAIT */
    // 等待 ACK 的复制节点数量
    int numreplicas;        /* Number of replicas we are waiting for ACK. */
    // 复制偏移量
    long long reploffset;   /* Replication offset to reach. */

} blockingState;

// 记录解除了客户端的阻塞状态的键，以及键所在的数据库。
typedef struct readyList {
    redisDb *db;
    robj *key;
} readyList;

typedef struct redisClient {

    // 当前正在使用的数据库
    redisDb *db;
    
    // 当前正在使用的数据库的 id
    int dictid;

    // 参数数量
    int argc;

    // 参数对象数组
    robj **argv;

    // 客户端状态标志
    int flags;  /* REDIS_SLAVE | REDIS_MONITOR | REDIS_MULTI ... */

    // 阻塞类型
    int btype;

    // 阻塞状态
    blockingState bpop;

} redisClient;

struct redisServer {

    // 每秒调用的次数
    int hz;

    // 最近一次 SAVE 后, 数据库被修改的次数
    long long dirty;

    size_t hash_max_ziplist_entries;
    size_t hash_max_ziplist_value;
    size_t list_max_ziplist_entries;
    size_t list_max_ziplist_value;
    size_t set_max_intset_entries;
    size_t zset_max_ziplist_entries;
    size_t zset_max_ziplist_value;
    size_t hll_sparse_max_bytes;

    // 用于 BLPOP, BRPOP, BRPOPLPUSH 
    list *ready_keys;
};

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

/* Log levels */
#define REDIS_DEBUG 0
#define REDIS_VERBOSE 1
#define REDIS_NOTICE 2
#define REDIS_WARNING 3
#define REDIS_LOG_RAW (1<<10) /* Modifier to log without timestamp */
#define REDIS_DEFAULT_VERBOSITY REDIS_NOTICE

/*--------------------- 压缩列表 -----------------------*/

#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

// // 跳跃表节点 (level必须放在最后一个)
// typedef struct zskiplistNode 
// {
//     // 对象成员
//     sds ele;

//     // 分值
//     double score;

//     // 后退指针
//     struct zskiplistNode *backward;

//     // 层
//     struct zskiplistLevel {
//         // 前进指针
//         struct zskiplistNode *forward;
//         // 跨度
//         unsigned int span;
//     } level[];

// } zskiplistNode;

// // 跳跃表
// typedef struct zskiplist
// {
//     // 指向首尾节点的指针
//     struct zskiplistNode *header, *tail;
//     // 节点数量
//     unsigned long length;
//     // 最大层数的节点的层数
//     int level;
// } zskiplist;

/*
 * 跳跃表节点
 */
typedef struct zskiplistNode {

    // 成员对象
    robj *obj;

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

/*
 * 跳跃表
 */
typedef struct zskiplist {

    // 表头节点和表尾节点
    struct zskiplistNode *header, *tail;

    // 表中节点的数量
    unsigned long length;

    // 表中层数最大的节点的层数
    int level;

} zskiplist;
/**
 * 有序集合
 * 保存两种结构, 是为在不同场景下的操作, 取最优解
 */
typedef struct zset {

    // 字典, 键为成员, 值为分值
    // 用于支持 O(1) 复杂度的按成员取分值操作
    dict *dict;

    // 跳跃表, 按分值排序成员
    // 用于支持平均复杂度为 O(log N) 的按分值定位成员操作
    // 以及范围操作
    zskiplist *zsl;

} zset;

// 表示开区间/闭区间范围的结构
typedef struct 
{
    // 最小值和最大值
    double min, max;

    // 指示最小值和最大值是否不包含在范围内
    // 值为 1 表示不包含, 值为 0 表示包含
    int minex, maxex;
} zrangespec;

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/
extern struct redisServer server;
extern struct sharedObjectsStruct shared;
extern dictType setDictType;
extern dictType zsetDictType;
extern dictType hashDictType;

/* Redis 对象实现 */
void decrRefCount(robj *o);
void decrRefCountVoid(void *o);
void incrRefCount(robj *o);
robj *resetRefCount(robj *obj);
void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);
robj *createObject(int type, void *ptr);
robj *createStringObject(char *ptr, size_t len);
robj *createRawStringObject(char *ptr, size_t len);;
robj *createEmbeddedStringObject(char *ptr, size_t len);
robj *dupStringObject(robj *o);
int isObjectRepresentableAsLongLong(robj *o, long long *llongval);
robj *tryObjectEncoding(robj *o);
robj *getDecodedObject(robj *o);
size_t stringObjectLen(robj *o);
robj *createStringObjectFromLongLong(long long value);
robj *createStringObjectFromLongDouble(long double value);
robj *createListObject(void);
robj *createZiplistObject(void);
robj *createSetObject(void);
robj *createIntsetObject(void);
robj *createHashObject(void);
robj *createZsetObject(void);
robj *createZsetZiplistObject(void);
int getLongFromObjectOrReply(redisClient *c, robj *o, long *target, const char *msg);
int checkType(redisClient *c, robj *o, int type);
int getLongLongFromObjectOrReply(redisClient *c, robj *o, long long *target, const char *msg);
int getDoubleFromObjectOrReply(redisClient *c, robj *o, double *target, const char *msg);
int getLongLongFromObject(robj *o, long long *target);
int getLongDoubleFromObject(robj *o, long double *target);
int getLongDoubleFromObjectOrReply(redisClient *c, robj *o, long double *target, const char *msg);
char *strEncoding(int encoding);
int compareStringObjects(robj *a, robj *b);
int collateStringObjects(robj *a, robj *b);
int equalStringObjects(robj *a, robj *b);

#define sdsEncodedObject(objptr) (objptr->encoding == REDIS_ENCODING_RAW || objptr->encoding == REDIS_ENCODING_EMBSTR)


/* 跳跃表 API */
zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj);
unsigned char *zzlInsert(unsigned char *zl, robj *ele, double score);
int zslDelete(zskiplist *zsl, double score, robj *obj);
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range);
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range);
double zzlGetScore(unsigned char *sptr);
void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
unsigned int zsetLength(robj *zobj);
void zsetConvert(robj *zobj, int encoding);
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o);


/* Core function 核心函数 */
unsigned int getLRUClock(void);


/* networking.c -- Networking and Client related operations 
 * 网络模块相关的函数
 */
void addReplyBulk(redisClient *c, robj *obj);
void addReply(redisClient *c, robj *obj);
void addReplyError(redisClient *c, char *err);
void addReplyMultiBulkLen(redisClient *c, long length);
void addReplyBulkCString(redisClient *c, char *s);
void addReplyBulkCBuffer(redisClient *c, void *p, size_t len);

void addReplyLongLong(redisClient *c, long long ll);

void rewriteClientCommandArgument(redisClient *c, int i, robj *newval);
void rewriteClientCommandVector(redisClient *c, int argc, ...);

/* db.c -- Keyspace access API 
 * 数据库操作函数
 */
void setExpire(redisDb *db, robj *key, long long when);
robj *lookupKeyRead(redisDb *db, robj *key);
robj *lookupKeyWrite(redisDb *db, robj *key);
robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply);
robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply);
void dbAdd(redisDb *db, robj *key, robj *val);
int dbDelete(redisDb *db, robj *key);
void dbOverwrite(redisDb *db, robj *key, robj *val);
void setKey(redisDb *db, robj *key, robj *val);

robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o);

void signalModifiedKey(redisDb *db, robj *key);
void signalFlushedDb(int dbid);


/* Keyspace events notification */
void notifyKeyspaceEvent(int type, char *event, robj *key, int dbid);
int keyspaceEventsStringToFlags(char *classes);
sds keyspaceEventsFlagsToString(int flags);

/* 阻塞客户端的方法 */
void blockClient(redisClient *c, int btype);

#endif