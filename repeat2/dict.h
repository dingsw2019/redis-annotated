#ifndef __DICT_H
#define __DICT_H

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
    
    // 指向下一个节点，单链表
    struct dictEntry *next;

} dictEntry;

// 字典类型
typedef struct dictType
{
    // 计算哈希值
    unsigned int (*hashFunction)(const void *key);

    // 复制键
    void (*keyDup)(void *privdata, const void *key);

    // 复制值函数
    void (*valDup)(void *privdata, const void *obj);

    // 比较键函数
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);

    // 销毁键函数
    void (*keyDestructor)(void *privdata, void *key);

    // 销毁值函数
    void (*valDestructor)(void *privdata, void *obj);
} dictType;


// 哈希表
typedef struct dictht {
    // 节点数组
    dictEntry **table;

    // 哈希表大小
    unsigned long size;

    // 哈希表索引的最大值
    unsigned long sizemask;

    // 使用空间的节点数
    unsigned long used;

} dictht;

// 字典
typedef struct dict {

    // 字典类型函数
    dictType *type;

    // 私有数据
    void *privdata;

    // 哈希表
    dictht ht[2];

    // rehash
    int rehashidx;

    // 安全迭代器数量
    int iterators;

} dict;

// 字典迭代器
typedef struct dictIterator {
    // 迭代的字典
    dict *d;
    // table : 迭代的哈希表，有两个哈希表
    // index : 迭代的哈希表的节点的索引
    // safe : 是否为安全的迭代器
    int table,index,safe;

    // 当前节点和下一个节点的指针
    dictEntry *entry,*nextEntry;

    long long fingerprint;

} dictIterator;

#endif