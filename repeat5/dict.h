#include <stdint.h>

#ifndef _DICT_H
#define _DICT_H

#define DICT_OK 0
#define DICT_ERR 1
// 忽略编译器错误
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
    // 表中节点大小
    unsigned long size;
    // 表大小的掩码
    unsigned long sizemask;
    // 表中已使用节点数
    unsigned long used;
} dictht;

// 字典函数
typedef struct dictType
{
    // 计算哈希值
    unsigned int (*keyHashFunction)(const void *key);
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
    // 自定义函数
    dictType *type;
    // rehash 的节点数组的索引
    int rehashidx;
    // 安全迭代器计数器
    int iterators;
} dict;

// 字典迭代器
typedef struct dictIterator
{
    // 迭代的字典
    dict *d;
    // table : 哈希表索引
    // index : 节点数组索引
    // safe : 是否安全迭代器
    int table,index,safe;

    // 迭代的节点和其下一个节点
    dictEntry *entry,*nextEntry;

    // 指纹
    long long fingerprint;

} dictIterator;



// 节点数组初始化大小
#define DICT_HT_INITIAL_SIZE 4
// 销毁键
#define dictFreeKey(d,entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata,(entry)->key)

// 设置键
#define dictSetKey(d,entry,_key_) do{ \
    if ((d)->type->keyDup) \
        (entry)->key = (d)->type->keyDup((d)->privdata,(_key_)) \
    else \
        (entry)->key = (_key_); \
}while(0)

// 销毁值
#define dictFreeVal(d,entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata,(entry)->v.val)

// 设置值
#define dictSetVal(d,entry,_val_) do{ \
    if ((d)->type->valDup) \
        (entry)->v.val = (d)->type->valDup((d)->privdata,(_val_)) \
    else \
        (entry)->v.val = (_val_); \
}while(0)
// 计算哈希值
// 哈希表已用节点数
// 是否 rehash 状态

#endif