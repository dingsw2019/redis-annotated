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
    if (dictIsRehash(d)) _dictRehashStep(d);
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

// gcc -g zmalloc.c intdicttype.c dict.c
void main(void)
{

    test_empty_dict();

    // test_add_and_delete_key_value_pair();

}