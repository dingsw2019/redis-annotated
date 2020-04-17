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

// 报错
#define redisAssertWithInfo(_c, _o, _e) _exit(1)
#define redisAssert(_e) _exit(1)
#define redisPanic(_e) _exit(1)

// LRU
#define REDIS_LRU_BITS 24
#define REDIS_LRU_CLOCK_MAX ((1<<REDIS_LRU_BITS)-1)
#define REDIS_LRU_CLOCK_RESOLUTION 1000
#define REDIS_SHARED_BULKHDR_LEN 32

// redis对象结构
typedef struct redisObject {

    // 类型
    unsigned type:4;

    // 编码
    unsigned encoding:4;

    // LRU
    unsigned lru:REDIS_LRU_BITS;

    int refcount;

    // 值指针
    void *ptr;

} robj;

#define LRU_CLOCK() 1


#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

// 跳跃表节点
typedef struct zskiplistNode{
    sds ele;
    double score;
    struct zskiplistNode *backward;
    struct zskiplistLevel {
        struct zskiplistNode *forward;
        unsigned int span;
    } level[];
} zskiplistNode;


// 跳跃表
typedef struct zskiplist {
    struct zskiplistNode *header, *tail;
    unsigned long length;
    int level;

} zskiplist;

typedef struct zset {

    // 字典, 键为成员, 值为分值
    // 用于支持 O(1) 复杂度的按成员取分值操作
    dict *dict;

    // 跳跃表, 按分值排序成员
    // 用于支持平均复杂度为 O(log N) 的按分值定位成员操作
    // 以及范围操作
    zskiplist *zsl;

} zset;

// 范围搜索器
typedef struct zrangespec {

    double min, max;

    // 1 表示不包含 , 0 包含
    int minex, maxex;
} zrangespec;

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
extern struct redisServer server;
extern struct sharedObjectsStruct shared;
extern dictType setDictType;
extern dictType zsetDictType;

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
// robj *createStringObjectFromLongDouble(long double value);
robj *createListObject(void);
robj *createZiplistObject(void);
robj *createSetObject(void);
robj *createIntsetObject(void);
robj *createHashObject(void);
robj *createZsetObject(void);
robj *createZsetZiplistObject(void);
// int getLongFromObjectOrReply(redisClient *c, robj *o, long *target, const char *msg);
// int checkType(redisClient *c, robj *o, int type);
// int getLongLongFromObjectOrReply(redisClient *c, robj *o, long long *target, const char *msg);
// int getDoubleFromObjectOrReply(redisClient *c, robj *o, double *target, const char *msg);
// int getLongLongFromObject(robj *o, long long *target);
// int getLongDoubleFromObject(robj *o, long double *target);
// int getLongDoubleFromObjectOrReply(redisClient *c, robj *o, long double *target, const char *msg);
// char *strEncoding(int encoding);
// int compareStringObjects(robj *a, robj *b);
// int collateStringObjects(robj *a, robj *b);
// int equalStringObjects(robj *a, robj *b);


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

#endif