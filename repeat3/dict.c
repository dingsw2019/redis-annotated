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
#include "fmacros.h"
#include "intdicttype.h"


extern dictType intDictType;

static int dict_can_resize = 1;
static int dict_force_resize_ratio = 5;

/**私有函数**/
// 哈希表扩容策略函数
static int _dictExpandIfNeeded(dict *d);
// 哈希表扩容大小策略函数
static unsigned long _dictNextPower(unsigned long size);
// 获取 key 的哈希表索引值
static int _dictKeyIndex(dict *d,const void *key);
// 字典属性初始化
static int _dictInit(dict *d,dictType *type,void *privDataPtr);


// 设置或重置 哈希表属性
void _dictReset(dictht *ht){
    
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典属性
int _dictInit(dict *d,dictType *type,void *privDataPtr){

    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;
    
    return DICT_OK;
}

// 创建一个空的字典
dict *dictCreate(dictType *type,void *privDataPtr){
    
    // 申请内存
    dict *d = zmalloc(sizeof(*d));
    // 初始化字典属性
    _dictInit(d,type,privDataPtr);

    return d;
}

// 释放字典的哈希
// void _dictClear(dict *d,dictht *ht){

//     unsigned long i;
//     dictEntry *he,*nextHe;

//     // 释放节点
//     for(i=0; i < ht->size && ht->used > 0; i++){

//         // 跳过空节点
//         if (!ht->table[i]) continue;

//         he = ht->table[i];
//         // 清除节点链表上所有节点
//         while(he){
//             nextHe = he->next;
//             // 删除节点
//             zfree(he);
//             // 减少节点数量
//             ht->used--;
//             he = nextHe;
//         }
//     }

//     // 释放哈希表
//     zfree(ht);
// }
int _dictClear(dict *d,dictht *ht,void (callback)(void *)){

    unsigned long i;

    // 释放节点
    for(i=0; i < ht->size && ht->used > 0; i++){
        dictEntry *he,*nextHe;
        // 回调函数,只调用一次
        if (callback && (i & 65535)==0) callback(d->privdata);
        // 跳过空节点
        if (ht->table[i] == NULL) continue;

        he = ht->table[i];
        // 清除节点链表上所有节点
        while(he){
            nextHe = he->next;
            // 释放节点的键
            dictFreeKey(d,he);
            // 释放节点的值
            dictFreeVal(d,he);
            // 释放节点
            zfree(he);
            // 减少节点数量
            ht->used--;
            he = nextHe;
        }
    }

    // 释放哈希表
    zfree(ht->table);

    // 重置哈希表属性
    _dictReset(ht);

    return DICT_OK;
}

// 释放指定字典
void dictRelease(dict *d){

    // 释放哈希表
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);
    // 释放字典
    zfree(d);
}



// 哈希表扩容大小策略函数
static unsigned long _dictNextPower(unsigned long size){
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if (size > LONG_MAX) return LONG_MAX;
    while(1){
        if (i > size)
            return i;
        i *= 2;
    }
}

// 扩容
static int dictExpand(dict *d,unsigned long size){

    dictht n;
    // 扩容大小策略
    unsigned long realsize = _dictNextPower(size);
    // rehash状态 或 已用节点数大于请求节点数
    // 不进行扩容
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 申请哈希表空间并初始化哈希表属性
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.used = 0;

    // 如果 0 号哈希表为空,赋值给 0 号哈希表
    // 如果 0 号哈希表非空，赋值给 1 号哈希表，并开启 rehash
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 扩容策略
static int _dictExpandIfNeeded(dict *d){

    // rehash 状态不扩容
    if (dictIsRehashing(d)) return DICT_OK;
    // 初始化扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 哈希表可用空间不足扩容
    if (d->ht[0].used >= d->ht[0].size && 
        (dict_can_resize || 
         d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

// 获取 key 的哈希表索引值
static int _dictKeyIndex(dict *d,const void *key){

    int h,idx,table;
    dictEntry *he;
    // 是否需要扩容哈希表
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 获取 key 的哈希值
    h = dictHashKey(d,key);

    // 查找 key 的哈希值是否存在
    for(table=0; table<=1; table++){
        
        // 确认索引值,防止溢出
        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];
        // 查找索引是否存在
        while(he){
            if (dictCompareKeys(d,key,he->key))
                return -1;
            he = he->next;
        }
        
        // 如果是 rehash 状态，也要搜索 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 返回索引
    return idx;
}

// 添加节点的键到指定的字典
// dictEntry *dictAddRow(dict *d,void *key){

//     int index;
//     dictht *ht;
//     dictEntry *entry;
//     // 获取 key 的哈希值,如果返回 -1 ,
//     // 表示该 key 已存在于哈希表中
//     if((index = _dictKeyIndex(d,key)) == -1)
//         return NULL;

//     // 确认哈希表,rehash 状态写入 1 号哈希表
//     // 否则写入 0 号哈希表
//     ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

//     // 申请新节点内存
//     ht->table[index] = zmalloc(sizeof(*entry));

//     // 将 key 写入新节点的键中
//     dictSetKey(d,ht->table[index],key);

//     return ht->table[index];
// }
dictEntry *dictAddRow(dict *d,void *key){

    int index;
    dictht *ht;
    dictEntry *entry;
    // 获取 key 的哈希值,如果返回 -1 ,
    // 表示该 key 已存在于哈希表中
    if((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 确认哈希表,rehash 状态写入 1 号哈希表
    // 否则写入 0 号哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    // 申请新节点内存
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    ht->table[index] = entry;

    // 更新节点使用数量
    ht->used++;

    // 将 key 写入新节点的键中
    dictSetKey(d,entry,key);

    return entry;
}

// 添加节点到指定的字典
int dictAdd(dict *d,void *key,void *val){

    dictEntry *entry;
    // 添加新的节点,并填充节点的键
    entry = dictAddRow(d,key);
    if (!entry) return DICT_ERR;

    // 值写入新节点
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