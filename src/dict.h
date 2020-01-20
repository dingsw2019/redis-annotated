#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

/**
 * 字典的操作状态
 */
// 操作成功
#define DICT_OK 0
// 操作失败或出错
#define DICT_ERR 1

// 如果字典的私有数据不使用
// 用这个宏来避免编译器错误
#define DICT_NOTUSED(V) ((void) V)

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

    // 指向下一个哈希表节点，形成链表
    struct dictEntry *next;

} dictEntry;

// 字典函数
typedef struct dictType {

    // 计算哈希值
    unsigned int (*hashFunction)(const void *key);
    // 复制键
    void (*keyDup)(void *privdata,const void *key);
    // 复制值
    void (*valDup)(void *privdata, const void *obj);
    // 对比键
    int (*keyCompare)(void *privdata,const void *key1, const void *key2);
    // 销毁键
    void (*keyDestructor)(void *privdata, void *key);
    // 销毁值
    void (*valDestructor)(void *privdata, void *obj);

} dictType;


/**
 * 哈希表
 * 
 * 每个字典都使用两个哈希表，从而实现渐进式 rehash
 */
typedef struct dictht {

    // 哈希表数组
    dictEntry **table;

    // 哈希表大小
    unsigned long size;

    // 哈希表大小掩码，用于计算索引值
    // 总是等于 size - 1
    unsigned long sizemask;

    // 该哈希表已有节点的数量
    unsigned long used;

} dictht;

// 字典
typedef struct dict {

    // 字典函数
    dictType *type;

    // 私有数据
    void *privdata;

    // 哈希表
    dictht ht[2];

    // rehash 索引，当 rehash 不在进行时，值为 -1
    int rehashidx;

    // 目前正在运行的安全迭代器的数量
    int iterators;

} dict;


/**
 * 字典迭代器
 * 
 * 如果 safe 属性的值为 1，那么在迭代进行的过程中，
 * 程序仍可执行 dictAdd ， dictFind 和其他函数，对字典进行修改。
 * 
 * 如果 safe 不为 1，那么程序只会调用 dictNext 对字典进行迭代，
 * 而不对字典进行修改。
 */
typedef struct dictIterator {

    // 被迭代的字典
    dict *d;

    // 指向的哈希表，节点索引，安全的迭代器
    // table : 正在被迭代的哈希表号码，值可以是 0 或 1 (一个字典两个哈希表)
    // index : 迭代器当前所指向的哈希表索引位置
    // safe  : 标识这个迭代器是否安全
    int table,index,safe;

    // entry : 当前迭代到的节点的指针
    // nextEntry : 当前迭代节点的下一个节点
    //             因为在安全迭代器运行时， entry 所指向的节点可能会被修改
    //             所以需要一个额外的指针来保存下一个节点的位置
    //             从而防止指针丢失
    dictEntry *entry,*nextEntry;

    long long fingerprint;
} dictIterator;

// 释放给定字典节点的值
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

// 设置给定字典节点的值
#define dictSetVal(d, entry, _val_) do { \
    if((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata, _val); \
    else \
        entry->v.val = (_val_); \
}while(0)

// 释放给定字典节点的键
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

// 设置给定字典节点的键
#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        entry->key = (_key_); \
}while(0)

// 查看字典是否正在 rehash
#define dictIsRehashing(ht) ((ht)->rehashidx != -1)

dict *dictCreate(dictType *type,void *privDataPtr);

#endif