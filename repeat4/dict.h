#ifndef _DICT_H
#define _DICT_H

#include <stdint.h>

#define DICT_OK 0 
#define DICT_ERR 1
#define DICT_NOTUSED(V) ((void) V)

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
    // 数组大小
    unsigned long size;
    // 数组大小掩码,用来计算索引
    unsigned long sizemask;
    // 数据已用大小
    unsigned long used;
} dictht;

// 字典函数
typedef struct dictType
{
    // 计算哈希值
    unsigned int (*hashFunction)(const void *key);
    // 复制键
    void *(*keyDup)(void *privdata,const void *key);
    // 复制值
    void *(*valDup)(void *privdata,const void *obj);
    // 对比值
    void (*keyCompare)(void *privdata,const void *key1,const void *key2);
    // 销毁键
    void (*keyDestructor)(void *privdata,void *key);
    // 销毁值
    void (*valDestructor)(void *privdata,void *obj);
} dictType;

// 字典
typedef struct dict
{
    // 哈希表
    dictht ht[2];
    // 私有数据
    void *privdata;
    // 自定义函数
    dictType *type;
    // rehash索引
    int rehashidx;
    // 安全迭代器计数器
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator 
{
    // 字典
    dict *d;
    // table ： 哈希表索引
    // index ： 节点索引
    // safe  ： 是否安全迭代器
    int table,index,safe;

    // 当前迭代节点与下一个节点
    dictEntry *entry,*nextEntry;

    // 指纹
    long long fingerprint;
} dictIterator;

// 哈希表数组初始化大小
#define DICT_HT_INITIVAL_SIZE 4

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
    if ((d)->type->valDup) \
        entry->key = (d)->type->keyDup((d)->privdata,(_key_)); \
    else \
        entry->key = (_key_); \
}while(0)

// 对比节点键
#define dictCompareKey(d,key1,key2) \
    ((d)->type->keyCompare ? \
        (d)->type->keyCompare((d)->privdata,key1,key2) : \
        (key1) == (key2))

// 计算哈希值
#define dictHashKey(d,key) (d)->type->hashFunction(key)

// 已用节点数
#define dictSize(d) (d)->ht[0].used + (d)->ht[1].used

// 是否 rehash
#define dictIsRehashing(d) ((d)->rehashidx != -1)

#endif