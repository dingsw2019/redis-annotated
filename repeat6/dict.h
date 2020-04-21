#ifndef DICT_H
#define DICT_H

#include <stdint.h>

// 哈希表节点
typedef struct dictEntry {
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

// 哈希表自定义函数
typedef struct dictType {
    // 计算哈希值
    unsigned int (*hashFunction)(const void *key);
    // 键复制
    void *(*keyDup)(void *privdata, const void *key);
    // 值复制
    void *(*valDup)(void *privdata, const void *obj);
    // 键对比
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    // 键释放
    void (*keyDestructor)(void *privdata, void *key);
    // 值释放
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

// 哈希表
typedef struct dictht {
    // 节点数组
    dictEntry **table;
    // 总节点大小
    unsigned long size;
    // 总节点掩码, 用来计算索引
    unsigned long sizemask;
    // 已用节点数
    unsigned long used;
} dictht;

// 字典
typedef struct dict {
    // 主副哈希表
    dictht ht[2];
    // 自定义函数
    dictType *type;
    // 私有数据
    void *privdata;
    // rehash 索引
    int rehashidx;
    // 迭代器数量
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator {

    // 字典
    dict *d;
    // table : 哈希表
    // index : 哈希表的索引值
    // safe  ：是否安全迭代器
    int table, index, safe;

    // 当前节点, 下一个节点
    dictEntry *entry, *nextEntry;

    long long fingerPrint;
} dictIterator;

#define DICT_OK 0
#define DICT_ERR 1
#define DICT_NOTUSED(v) ((void) v)

// 哈希表的初始大小
#define DICT_HT_INITIAL_SIZE 4

// 释放值
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val);

// 设置值
#define dictSetVal(d, entry, _val_) do{ \
    if ((d)->type->valDup) {    \
        (entry)->v.val = (d)->type->valDup((d)->privdata, (_val_)); \
    } else {    \
        (entry)->v.val = (_val_);   \
    }   \
}while(0)

// 释放键
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key);

// 设置键
#define dictSetKey(d, entry, _key_) do{                             \
    if ((d)->type->keyDup) {                                        \
        (entry)->key = (d)->type->keyDup((d)->privdata, (_key_));   \
    } else {                                                        \
        (entry)->key = (_key_);                                     \
    }                                                               \
}while(0)

// 对比键
#define dictCompareKey(d,key1,key2)                             \
    (((d)->type->keyCompare) ?                                  \
        (d)->type->keyCompare((d)->privdata, (key1), (key2)) :  \
        (key1 == key2))

// 计算哈希值
#define dictHashKey(d,key) (d)->type->hashFunction(key)

// 返回节点的键
#define dictGetKey(he) ((he)->key)

// 返回节点的值
#define dictGetVal(he) ((he)->v.val)

// 已用节点数量
#define dictSize(d) ((d)->ht[0].used + (d)->ht[1].used)

// 是否rehash
#define dictIsRehashing(d) ((d)->rehashidx != -1)

dict *dictCreate(dictType *type, void *privDataPtr);
void  dictRelease(dict *d);
int   dictRehash(dict *d, int n);

// 增
int   dictExpand(dict *d, unsigned long size);
int   dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key);

// 删
int   dictGenericDelete(dict *d, const void *key, int nofree);
int   dictDelete(dict *d, void *key);
int   dictDeleteNoFree(dict *d, void *key);

// 改
int   dictReplace(dict *d, void *key, void *val);
dictEntry *dictReplaceRaw(dict *d, void *key);

// 查
dictEntry *dictFind(dict *d, void *key);
dictEntry *dictGetRandomKey(dict *d);
int dictGetRandomKeys(dict *d, dictEntry **des, int count);

// 迭代器
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void   dictReleaseIterator(dictIterator *iter);

#endif
