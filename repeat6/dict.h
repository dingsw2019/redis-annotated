#include "fmacros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include "zmalloc.h"
#include "redisassert.h"
#include <assert.h>
#include "dictType.h"

// 节点
typedef struct dictEntry 
{
    // 键
    void *key;
    // 值
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
    } v;
    // 指向下一个节点
    struct dictEntry *next;
} dictEntry;

// 哈希表
typedef struct dictht 
{
    // 节点数组
    dictEntry **table;
    // 哈希表大小
    unsigned long size;
    // 哈希表索引
    unsigned long sizemask;
    // 已用节点数
    unsigned long used;
} dictht;

// 字典函数
typedef struct dictType 
{
    // 计算哈希值
    unsigned int (*hashFunction)(const void *key);
    // 键复制
    void *(*keyDup)(void *privdata, const void *key);
    // 值复制
    void *(*valDup)(void *privdata, const void *obj);
    // 对比键
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    // 释放键
    void (*keyDestructor)(void *privdata, void *key);
    // 释放值
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

// 字典
typedef struct dict 
{
    // 哈希表
    dictht ht[2];
    // 私有数据
    void *privdata;
    // 字典自定义函数
    dictType *type;
    // rehash 索引 myerr
    // unsigned int rehashidx;
    int rehashidx;
    // 迭代器计数器
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator
{
    // 字典
    dict *d;
    // table : 哈希表索引
    // index : 节点索引
    // safe : 是否安全迭代器
    int table, index, safe;
    // 节点
    dictEntry *entry, *nextEntry;
    // 指纹
    long long fingerprint;
} dictIterator;

// 哈希表节点数组初始化大小
#define DICT_HT_INITIAL_SIZE 4
#define DICT_OK 0
#define DICT_ERR 1

// myerr 忘记写
#define DICT_NOTUSED(V) ((void) V)

// 销毁键 myerr
// #define dictFreeKey(d,_key_) \
//     if ((d)->type->keyDestructor) \
//         (d)->type->keyDestructor((d)->privdata, (_key_))
#define dictFreeKey(d,entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

// 复制键
#define dictSetKey(d,entry,_key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata,(_key_)); \
    else \
        entry->key = (_key_); \
}while(0)

// 销毁值 myerr
// #define dictFreeVal(d,_val_) \
//     if ((d)->type->valDestructor) \
//         (d)->type->valDestructor((d)->privdata,(_val_))
#define dictFreeVal(d,entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata,(entry)->v.val)

// 复制值
#define dictSetVal(d,entry,_val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata,(_val_)); \
    else \
        entry->v.val = (_val_); \
}while(0)

// 对比键
#define dictCompareKey(d,key1,key2) \
    ((d)->type->keyCompare ? \
        (d)->type->keyCompare((d)->privdata,key1,key2) : \
        (key1 == key2))

// 计算哈希值
#define dictHashKey(d,key) (d)->type->hashFunction(key)

// 计算节点数量
#define dictSize(d) ((d)->ht[0].used + (d)->ht[1].used)

// 是否处于 rehash
#define dictIsRehashing(d) ((d)->rehashidx != -1)