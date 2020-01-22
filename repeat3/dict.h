#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

// 操作成功
#define DICT_OK 0
// 操作失败或出错
#define DICT_ERR 1

// 如果字典的私有数据不使用
// 用这个宏来避免编译器错误
#define DICT_NOTUSED(V) ((void) V)


// 字典哈希表的节点
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


// 字典特征的函数
typedef struct dictType
{
    // 计算哈希值的函数
    unsigned int (*hashFunction)(const void *key);
    // 复制键
    void *(*keyDup)(void *privdata,const void *key);
    // 复制值
    void *(*valDup)(void *privdata,const void *obj);
    // 对比键
    int (*keyCompare)(void *privdata,const void *key1,const void *key2);
    // 销毁键
    void (*keyDestructor)(void *privdata,void *key);
    // 销毁值
    void (*valDestructor)(void *privdata,void *obj);
} dictType;

// 哈希表
typedef struct dictht
{
    // 节点数组
    dictEntry **table;
    // 数组大小
    unsigned long size;
    // 数组索引最大值
    unsigned long sizemask;
    // 已填充节点的数量
    unsigned long used;
} dictht;

// 字典
typedef struct dict
{
    // 特征函数
    dictType *type;

    // 私有数据
    void *privdata;

    // 哈希表(主备两个表)
    dictht ht[2];
    // rehash的索引值
    int rehashidx;
    // 安全迭代器数量
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator
{
    // 被迭代的字典
    dict *d;
    // table : 字典的哈希表(主副表)
    // index : 哈希表的索引 , 疑问: 哈希表的 sizemask 是long ,这里用 int ?
    // safe : 是否安全迭代器
    int table,index,safe;
    // 当前和下一个要迭代的哈希表节点
    dictEntry *entry,*nextEntry;
    long long figerprint;
} dictIterator;

// 初始化哈希表节点的数量
#define DICT_HT_INITIAL_SIZE 4

// 设置节点的值
#define dictSetVal(d,entry,_val_) do{ \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata,_val_); \
    else \
        entry->v.val = (_val_); \
}while(0)

// 释放节点的值
#define dictFreeVal(d,entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata,(entry)->v.val)
        
// 设置节点的键
#define dictSetKey(d,entry,_key_) do{ \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata,_key_); \
    else \
        entry->key = (_key_); \
}while(0)

// 释放节点的键
#define dictFreeKey(d,entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata,(entry)->key)

// 对比两个节点的键
#define dictCompareKeys(d,key1,key2) \
    ((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata,key1,key2) : \
        (key1) == (key2)

// 计算给定键的哈希值
#define dictHashKey(d,key) (d)->type->hashFunction(key)
// 字典已使用的节点数量
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
// 字典是否处于 rehash 状态
#define dictIsRehashing(d) ((d)->rehashidx != -1)


void _dictReset(dictht *ht);
dict *dictCreate(dictType *type,void *privDataPtr);
int _dictClear(dict *d,dictht *ht,void (callback)(void *));
void dictRelease(dict *d);
dictEntry *dictAddRow(dict *d,void *key);
int dictAdd(dict *d,void *key,void *val);
#endif