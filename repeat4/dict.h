#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

// 状态
#define DICT_OK 0
#define DICT_ERR 1

// 如果未使用 privdata 
// 用此宏避免编译器错误
#define DICT_NOTUSED(V) ((void) V)
// 哈希表大小初始化节点数量
#define DICT_HT_INITIAL_SIZE 4

// 字典中哈希表的节点
typedef struct dictEntry {
    // 键
    void *key;
    // 值
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
    } v;
    // 指向下一个节点的指针
    struct dictEntry *next;

} dictEntry;

// 字典的特征函数
typedef struct dictType {
    // 获取 key 的哈希值
    unsigned int (*hashFunction)(const void *key);
    // 复制键
    void *(*keyDup)(void *privdata,const void *key);
    // 复制值
    void *(*valDup)(void *privdata,const void *obj);
    // 对比键
    int (*keyCompare)(void *privdata,const void *key1,const void *key2);
    // 释放键
    void (*keyDestructor)(void *privdata,void *key);
    // 释放值
    void (*valDestructor)(void *privdata,void *obj);
} dictType;

// 字典的哈希表结构
typedef struct dictht {
    // 哈希表节点数组
    dictEntry **table;
    // 数组大小
    unsigned long size;
    // 数组索引最大值
    unsigned long sizemask;
    // 已占用节点数
    unsigned long used;
} dictht;

// 字典的结构
typedef struct dict {
    // 特征函数
    dictType *type;
    // 私有数据
    void *privdata;
    // 主副哈希表
    dictht ht[2];
    // rehash 的索引值
    int rehashidx;
    // 迭代器数量
    int iterators;
} dict;


// 字典迭代器
typedef struct dictIterator {
    // 被迭代的字典
    dict *d;
    // table : 迭代的哈希表(主副)
    // index : 哈希表的索引值
    // safe : 是否安全迭代器
    int table,index,safe;
    // 当前和下一个哈希表节点的指针
    dictEntry *entry,*nextEntry;
    long long fingerprint;
} dictIterator;

// 释放哈希表节点的值
#define dictFreeVal(d,entry) \
    if (d->type->valDestructor) \
        d->type->valDestructor((d)->privdata,(entry)->v.val)

// 设置哈希表节点的值
#define dictSetVal(d,entry,_val_) do{ \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata,(_val_)); \
    else \
        entry->v.val = (_val_); \
}while(0)

// 释放哈希表节点的键
#define dictFreeKey(d,entry) \
    if (d->type->keyDestructor) \
        d->type->keyDestructor((d)->privdata,(entry)->key)
        
// 设置哈希表节点的键
#define dictSetKey(d,entry,_key_) do{ \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata,(_key_)); \
    else \
        entry->key = (_key_); \
}while(0)


// 对比哈希表节点的键
#define dictCompareKeys(d,key1,key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata,key1,key2) : \
        (key1) == (key2))

// 获取 key 的哈希值
#define dictHashKey(d,key) (d)->type->hashFunction(key)

// 字典主副哈希表已使用的节点数量
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)

// 字典是否处于 rehash 状态
#define dictIsRehashing(d) ((d)->rehashidx != -1)

// 初始化或重置哈希表的属性
// void _dictReset(dictht *ht);
// 创建一个空的字典
dict *dictCreate(dictType *type,void *privDataPtr);
// 释放字典的哈希表
int _dictClear(dict *d,dictht *ht,void (callback)(void *));
// 释放指定字典
void dictRelease(dict *d);
// 字典哈希表扩容
int dictExpand(dict *d,unsigned long size);
// 避免重复 key 加入字典
dictEntry *dictAddRaw(dict *d,void *key);
// 以键值方式新增一个节点到指定字典
int dictAdd(dict *d,void *key,void *val);

#endif