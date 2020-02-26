#ifndef __DICT_H
#define __DICT_H

#include <stdint.h>

// 操作成功
#define DICT_OK 0
// 操作失败
#define DICT_ERR 1
// 避免编译器错误
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
    // 哈希表大小掩码,索引
    unsigned long sizemask;
    // 已用大小
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
    // 对比键
    int (*keyCompare)(void *privdata,const void *key1,const void *key2);
    // 销毁键
    void (*keyDestructor)(void *privdata,void *key);
    // 销毁值
    void (*valDestructor)(void *privdata,void *obj);
} dictType;

// 字典
typedef struct dict
{
    // 主副哈希表
    dictht ht[2];
    // 私有数据
    void *privdata;
    // 字典函数
    dictType *type;
    // rehash 索引
    int rehashidx;
    // 安全迭代器数量
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator 
{
    // 字典
    dict *d;

    // table,哈希表索引值
    // index,哈希表的节点数组索引
    // safe,是否安全索引
    int table,index,safe;
    // 要检查的节点和下一个节点
    dictEntry *entry,*nextEntry;

    long long fingerprint;
} dictIterator;

// 哈希表的初始大小
#define DICT_HT_INITIAL_SIZE 4

// 销毁节点的值
#define dictFreeVal(d,entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata,(entry)->v.val)

// 设置节点的值
#define dictSetVal(d,entry,_val_) do{ \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata,_val_); \
    else \
        entry->v.val = (_val_); \
}while(0)

// 销毁节点的键
#define dictFreeKey(d,entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata,(entry)->key)

// 设置节点的键
#define dictSetKey(d,entry,_key_) do{ \
    if ((d)->type->keyDup) \
        (entry)->key = (d)->type->keyDup((d)->privdata,(_key_)); \
    else \
        (entry)->key = (_key_); \
}while(0)

// 比对键
#define dictCompareKey(d,key1,key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata,key1,key2) : \
        (key1) == (key2))

// 计算哈希值
#define dictHashKey(d,key) (d)->type->hashFunction(key)
// 已用节点数量
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
// 是否处于rehash状态
#define dictIsRehashing(d) ((d)->rehashidx != -1)


#endif