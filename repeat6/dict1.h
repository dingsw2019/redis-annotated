#ifndef _DICT_H
#define _DICT_H

#include <stdint.h>

#define DICT_OK 0
#define DICT_ERR 1
// 避免编译器报错
#define DICT_NOTUSED(V) ((void) V)

// 字典函数
typedef struct dictType
{
    // 计算哈希值
    unsigned int (*hashFunction)(const void *key);
    // 复制节点键
    void *(*keyDup)(void *privdata,const void *key);
    // 复制节点值
    void *(*valDup)(void *privdata,const void *obj);
    // 对比节点键
    int (*keyCompare)(void *privdata,const void *key1,const void *key2);
    // 销毁节点键
    void (*keyDestructor)(void *privdata,void *key);
    // 销毁节点值
    void (*valDestructor)(void *privdata,void *val);
} dictType;

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
    // 下一个节点
    struct dictEntry *next;
} dictEntry;

// 哈希表
typedef struct dictht
{
    // 节点数组
    dictEntry **table;
    // 哈希表节点数量
    unsigned long size;
    // 哈希表节点数掩码,用于索引比对
    unsigned long sizemask;
    // 哈希表已用节点数
    unsigned long used;

} dictht;

// 字典
typedef struct dict 
{
    // 主副哈希表
    dictht ht[2];
    // 私有数据
    void *privdata;
    // 字典函数
    dictType *type;
    // rehash 索引,-1 代表未启动 rehash
    int rehashidx;
    // 迭代器数量
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator
{
    // 迭代的字典
    dict *d;
    // table : 迭代的哈希表
    // index : 哈希表的索引
    // safe  : 是否安全迭代器
    int table,index,safe;
    // 迭代的节点和下一个节点
    dictEntry *entry,*nextEntry;

    long long figerprint;
} dictIterator;

// 哈希表的节点数组初始化大小
#define DICT_HT_INITIAL_SIZE 4
// 释放节点值
#define dictFreeVal(d,entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata,(entry)->v.val)
// 设置节点值
#define dictSetVal(d,entry,_val_) do{ \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata,(_val_)); \
    else \
        entry->v.val = (_val_); \
}while(0)
// 释放节点键
#define dictFreeKey(d,entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata,(entry)->key)
// 设置节点键
#define dictSetKey(d,entry,_key_) do{ \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata,_key_); \
    else \
        entry->key = (_key_); \
}while(0)
// 对比键
#define dictCompareKey(d,key1,key2) \
    ((d)->type->keyCompare ? \
        (d)->type->keyCompare((d)->privdata,key1,key2) : \
        (key1) == (key2))
// 计算哈希值
#define dictHashKey(d,key) (d)->type->hashFunction(key)
// 已用节点数
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
// 是否处于 rehash 状态
#define dictIsRehashing(d) ((d)->rehashidx != -1)

dict *dictCreate(dictType *type,void *privDataPtr);
int dictAdd(dict *d,void *key,void *val);
dictEntry *dictAddRaw(dict *d,void *key);
int dictExpand(dict *d,unsigned long size);
#endif