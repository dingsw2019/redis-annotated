#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>
#include <time.h>   

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

#include <assert.h>
#include "dictType.h"

#include "dict.h"

extern dictType initDictType;
// 强制扩容
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;
// 强制扩容比例

// 初始化或重置哈希表
static void _dictReset(dictht *ht) {

    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典
int _dictInit(dict *d, dictType *type, void *privDataPtr) {

    // 哈希表
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    d->privdata = privDataPtr;
    d->type = type;
    d->rehashidx = -1;
    d->iterators = 0;

    return DICT_OK;
}

// 创建并返回一个空字典
dict *dictCreate(dictType *type, void *privDataPtr) {

    dict *d;
    // 申请内存
    if ((d = zmalloc(sizeof(*d))) == NULL)
        return NULL;

    // 初始化
    _dictInit(d, type, privDataPtr);

    return d;
}

// 扩容大小策略
static unsigned long _dictNextPower(unsigned long size) {
    unsigned long i = DICT_HT_INITIAL_SIZE;
    while(1) {
        if (i > size)
            return i;
        i *= 2;
    }
}

// 扩容, 扩容成功返回 DICT_OK, 否则返回 DICT_ERR
int dictExpand(dict *d, unsigned long size) {

    dictht n;
    // 获取扩容后的节点大小
    unsigned long realsize = _dictNextPower(size);

    // 不扩容
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 新哈希表
    n.table = zcalloc(realsize * sizeof(dictEntry*));
    n.size = realsize;
    n.sizemask = realsize-1;
    n.used = 0;

    // 新哈希表填入 0 号哈希表
    if (d->ht[0].size == 0) {
        d->ht[0] = n;
        return DICT_OK;
    }

    // 填入 1 号哈希表, 并开启 rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 检测字典是否需要扩容
// 需要扩容返回 执行扩容的结果, 否则返回 0
static int _dictExpandIfNeeded(dict *d) {

    // rehash 不扩容
    if (dictIsRehashing(d)) return DICT_OK;

    // 初始化扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 空间不足, 强制扩容
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize || 
            (d->ht[0].used/d->ht[0].size) > dict_force_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used*2);
    }

    return DICT_OK;
}

// 计算 key 的哈希值, 并确保哈希值不存在于字典中
// 如果哈希值存在, 返回 -1
// 否则返回哈希值
static int _dictKeyIndex(dict *d, void *key) {

    unsigned int h, idx, table;
    dictEntry *he;

    // 是否需要扩容
    if (_dictExpandIfNeeded(d) == DICT_ERR) return -1;

    // 计算哈希值
    h = dictHashKey(d, key);

    // 确定哈希表
    for (table=0; table <=1; table++) {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        
        // 遍历查找, 哈希值是否存在
        while(he) {
            if (dictCompareKey(d,he->key,key))
                return -1;
            he = he->next;
        }

        if (!dictIsRehashing(d)) break;
    }

    return idx;
}

// 添加节点的键
// 如果键存在返回 NULL, 否则返回新机电
dictEntry *dictAddRaw(dict *d, void *key) {

    int index;
    dictEntry *entry;
    dictht *ht;

    // 获取哈希值, 如果是 -1 , 说明键已存在, 返回
    if ((index = _dictKeyIndex(d, key)) == -1) {
        return NULL;
    }

    // 创建新节点
    entry = zmalloc(sizeof(*entry));

    // 将节点添加到链表头部
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry->next = ht->table[index];
    ht->table[index] = entry;

    // 写入键
    dictSetKey(d, entry, key);

    // 更新节点计数器
    ht->used++;

    return entry;
}

// 添加节点
// 添加成功返回 0, 否则返回 1
int dictAdd(dict *d, void *key, void *val) {

    // 创建新节点, 并将键写入
    dictEntry *entry = dictAddRaw(d, key);
    if (!entry) return DICT_ERR;

    // 写入值
    dictSetVal(d, entry, val);

    return DICT_OK;
}



/*--------------------------- debug -------------------------*/


// gcc -g zmalloc.c dictType.c dict.c
void main(void)
{
    int ret;
    dictEntry *he;

    srand(time(NULL));

    // 创建一个空字典
    dict *d = dictCreate(&initDictType, NULL);

    // // 节点的键值
    // keyObject *k = keyCreate(1);
    // valObject *v = valCreate(10086);

    // // 添加节点
    // dictAdd(d, k, v);

    // // dictPrintStats(d);
    // printf("\ndictAdd, (k=1,v=10086) join dict,dict used size %d\n",dictSize(d));
    // printf("---------------------\n");

    // // 查找节点
    // he = dictFind(d, k);
    // if (he) {
    //     printf("dictFind,k is 1 to find entry\n");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictFind, not find\n");
    // }
    // printf("---------------------\n");

    // // 节点值替换
    // valObject *vRep = valCreate(10010);
    // dictReplace(d,k,vRep);
    // he = dictFind(d, k);
    // if (he) {
    //     printf("dictReplace, find entry(k=1,value=10086) and replace value(10010)\n");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictReplace, not find\n");
    // }
    // printf("---------------------\n");

    // // 新增节点(dictReplace)
    // keyObject *k2 = keyCreate(2);
    // valObject *v2 = valCreate(10000);
    // dictReplace(d,k2,v2);
    // he = dictFind(d, k2);
    // if (he) {
    //     printf("dictAdd through dictReplace, add entry(k=2,value=10000) join dict\n");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictAdd through dictReplace, not find entry\n");
    // }
    // // dictPrintStats(d);
    // printf("---------------------\n");

    // // 随机获取一个节点
    // he = dictGetRandomKey(d);
    // if (he) {
    //     printf("dictGetRandomKey , ");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictGetRandomKey , not find entry\n");
    // }
    // printf("---------------------\n");

    // // 通过迭代器获取打印所有节点
    // dictPrintAllEntry(d);
    // printf("---------------------\n");

    // // 删除节点
    // ret = dictDelete(d, k);
    // printf("dictDelete : %s, dict size %d\n\n",((ret==DICT_OK) ? "yes" : "no"),dictSize(d));
    
    // // 释放字典
    // dictRelease(d);
}