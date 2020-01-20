#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

// 避免编译器错误
#define DICT_NOTUSED(V) ((void) V)

// 节点
typedef struct dictEntry {
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

// 字典的特定函数
typedef struct dictType{
    
    // 计算哈希值的函数
    unsigned int (*hashFunction)(const void *key);
    // 复制键的函数
    void (*keyDup)(void *privdata, const void *key);
    // 复制值的函数
    void (*valDup)(void *privdata, const void *obj);
    // 对比键的函数
    int (*keyCompare)(void *privdata,const void *key1,const void *key2);
    // 销毁键的函数
    void (*keyDestructor)(void *privdata,void *key);
    // 销毁值的函数
    void (*valDestructor)(void *privdata,void *obj);
} dictType;

// 哈希表
typedef struct dictht {

    // 节点数组
    dictEntry **table;
    // 哈希表大小
    unsigned long size;
    // 哈希表索引最大值
    unsigned long sizemask;
    // 已使用节点的数量
    unsigned long used;
} dictht;

// 字典
typedef struct dict {
    // 字典类型的特征函数
    dictType *type;
    // 私有数据
    void *privdata;
    // 哈希表
    dictht ht[2];
    // rehash 索引
    int rehashidx;
    // 安全迭代器的数量
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator {
    // 被迭代的字典
    dict *d;
    // table : 迭代的哈希表
    // index : 哈希表的节点的索引
    // safe : 是否安全的迭代器
    int table,index,safe;
    // 当前节点和下一个节点的指针
    dictEntry *entry,*nextEntry;
    long long fingerprint;
} dictIterator;



// 销毁节点的值
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata,entry->v.val)

// 设置节点的值
#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata,_val_); \
    else \
        entry->v.val = (_val_); \
}while(0)

// 销毁节点的键
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata,(entry)->key)

// 设置节点的键
#define dictSetKey(d, entry, _key_) do{ \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup(d->privdata,_key_); \
    else \
        entry->key = (_key_); \
}while(0)

// 是否正rehash
#define dictIsRehash(d) ((d)->rehashidx != -1)

#endif