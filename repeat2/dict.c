#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

#include <assert.h>

#include "intdicttype.h"
#include "dict.h"

extern dictType intDictType;
// 指定字典是否启用 rehash 的标识
static int dict_can_resize = 1;
// 强制 rehash 的节点使用率
static int dict_force_resize_ratio = 5;

// 扩容策略函数
static int _dictExpandIfNeeded(dict *ht);
// 扩容大小策略函数
// static unsigned int _dictNextPower(unsigned long size);
static unsigned long _dictNextPower(unsigned long size);
// 获取 key 的哈希表索引值函数
static int _dictKeyIndex(dict *ht,const void *key);
// 字典初始化函数
static int _dictInit(dict *ht,dictType *type,void *privDataPtr);

static int _dictKeyIndex(dict *d,const void *key){

    // int h,table,idx;
    unsigned int h,table,idx;
    dictEntry *he;
    // 是否需要扩容,不需要扩容直接返回
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 获取 key 的哈希值
    // h = dictHashFunction(key);
    h = dictHashKey(d,key);

    // 遍历查找 key 是否存在
    for(table=0; table<=1; table++){

        // key的索引值，防止溢出
        idx = h & d->ht[table].sizemask;
        // 节点
        he = d->ht[table].table[idx];
        // 节点链表查找 key
        while(he){
            // if (dictCompareKey(d,key,he->key))
            if (dictCompareKeys(d,key,he->key))
                return -1;
            he = he->next;
        }

        // 如果是 rehash 状态，遍历 1 号哈希表
        if (!dictIsRehash(d)) break;
    }

    // 返回哈希表索引值
    return idx;
}

static int _dictExpandIfNeeded(dict *d){

    // 处于 rehash 状态，不进行扩容
    if (dictIsRehash(d)) return DICT_OK;

    // 字典 0 号哈希表为空，初始化 0 号哈希表
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INIT_SIZE);

    // 内存不足扩容，需同时满足以下两种情况
    // 1) 已用节点数量大于节点总量
    // 2) 开启强制rehash模式 或者 节点使用率大于强制rehash比例
    if (d->ht[0].used >= d->ht[0].size && 
        (dict_can_resize || d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

int dictExpand(dict *d,unsigned long size){

    dictht n;
    // 计算扩容后节点的数量
    unsigned int realsize = _dictNextPower(size);

    // 正在进行rehash 或者 使用节点数大于请求节点数
    // 不进行扩容
    if (dictIsRehash(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 申请节点内存并初始化哈希表属性
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.used = 0;

    // 如果 0 号哈希表为空，说明是初始化，把节点赋值给 0 号哈希表
    // 如果 0 号哈希表非空，说明要进行rehash，把节点赋值给 1 号哈希表，并开启 rehash
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// static unsigned long _dictNextPower(unsigned long size){
//     unsigned long i;
//     if (i > LONG_MAX) return LONG_MAX;
//     while(1){
//         if (i > size)
//             return i;
//         i *= 2;
//     }
// }
static unsigned long _dictNextPower(unsigned long size){
    unsigned long i = DICT_HT_INIT_SIZE;
    if (size > LONG_MAX) return LONG_MAX;
    while(1){
        if (i >= size)
            return i;
        i *= 2;
    }
}


// 清除 或重置哈希表的属性值
static void _dictReset(dictht *ht){
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典的属性值
int _dictInit(dict *d,dictType *type,void *privDataPtr){

    // 初始化哈希表的属性值
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;

    return DICT_OK;
}

// 创建一个字典
dict *dictCreate(dictType *type,void *privDataPtr){

    // 申请内存
    dict *d = zmalloc(sizeof(*d));
    // 初始化字典
    _dictInit(d,type,privDataPtr);
    return d;
}

int _dictClear(dict *d,dictht *ht,void (callback)(void *)){
    unsigned long i;

    // 遍历哈希表
    for(i=0;i < ht->size && ht->used > 0; i++){
        dictEntry *he,*nextHe;
        // 如果设置了回调函数，只调用 1 次
        if (callback && (i & 65535)==0) callback(d->privdata);

        // 跳过空节点
        if ((he = ht->table[i]) == NULL) continue;

        // 遍历节点链表
        while(he){
            nextHe = he->next;
            // 释放键
            dictFreeKey(d,he);
            // 释放值
            dictFreeVal(d,he);
            // 减少节点使用数
            ht->used--;
            // 释放节点
            zfree(he);
            // 指向下一个节点
            he = nextHe;
        }
    }

    // 重置哈希表
    _dictReset(ht);

    // 释放哈希表内存
    zfree(ht->table);

    return DICT_OK;
}

// 清除并释放指定字典
void dictRelease(dict *d){

    // 释放哈希表
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);

    // 释放字典
    zfree(d);
}

dictEntry *dictAddRaw(dict *d,void *key){

    int index;
    dictEntry *entry;
    dictht *ht;

    // 如果处在 rehash 状态，进行单步 rehash 操作
    // if (dictIsRehash(d)) _dictRehashStep(d);
    // 如果 key 已存在，不进行操作
    if ((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 确定哈希表，如果处在 rehash 状态
    // 新节点添加到 1 的哈希表，否则添加到 0 的哈希表
    ht = dictIsRehash(d) ? &d->ht[1] : &d->ht[0];

    // 创建一个新节点
    entry = zmalloc(sizeof(*entry));

    // 新节点的下一个节点指向 NULL
    entry->next = ht->table[index];
    // 将新节点指向哈希表的表头
    ht->table[index] = entry;
    // 哈希表节点数增加
    ht->used++;

    // 设置节点的键
    dictSetKey(d,entry,key);

    // 返回节点
    return entry;
}

// 向字典中 新增一个节点
int dictAdd(dict *d,void *key,void *val){

    // 新增一个键, 如果键存在返回 NULL
    dictEntry *entry = dictAddRaw(d,key);
    if (!entry) return DICT_ERR;

    // 将值写入节点
    dictSetVal(d,entry,val);

    return DICT_OK;
}

void test_empty_dict(void)
{
    dict* d = dictCreate(&intDictType, NULL);

    dictRelease(d);
}

void test_add_and_delete_key_value_pair(void)
{
    // 创建新字典
    dict *d = dictCreate(&intDictType, NULL);

    // 创建键和值
    KeyObject *k = create_key();
    k->value = 1;
    ValueObject *v = create_value();
    v->value = 10086;

    // 添加键值对
    dictAdd(d, k, v);

    printf("dictAdd : dict size %d",dictSize(d));

    // assert(
    //     dictSize(d) == 1
    // );

    // assert(
    //     dictFind(d, k) != NULL
    // );

    // // 删除键值对
    // dictDelete(d, k);

    // assert(
    //     dictSize(d) == 0
    // );

    // assert(
    //     dictFind(d, k) == NULL
    // );

    // // 释放字典
    // dictRelease(d);
}

// gcc -g zmalloc.c intdicttype.c dict.c
void main(void)
{

    // test_empty_dict();

    test_add_and_delete_key_value_pair();

}