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
#include "dictType.h"

extern dictType initDictType;
/**
 * 私有函数
 */
// 字典是否开启rehash
static int dict_can_resize = 1;
// 强制 rehash 的节点使用率
static int dict_force_resize_ratio = 5;
// 初始化字典的属性
static int _dictInit(dict *d,dictType *type,void *privDataPtr);
// 字典的哈希表扩容策略
static int _dictExpandIfNeeded(dict *d);
// 字典的哈希表扩容大小策略
static int _dictNextPower(unsigned long size);


// 初始化或重置哈希表的属性
void _dictReset(dictht *ht){
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典的属性
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

    // 给字典申请内存
    dict *d = zmalloc(sizeof(*d));

    // 初始化字典属性
    _dictInit(d,type,privDataPtr);

    return d;
}
// 释放字典的哈希表
int _dictClear(dict *d,dictht *ht,void (callback)(void *)){

    unsigned long i;
    // 遍历哈希表
    for(i=0; i < ht->size && ht->used > 0; i++){
        dictEntry *he,*nextHe;
        // 回调函数,调用一次
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
            // 释放节点
            zfree(he);
            // 减少已使用节点数
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
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);

    zfree(d);
}

// 字典的哈希表扩容大小策略
static int _dictNextPower(unsigned long size){
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if (i > LONG_MAX) return LONG_MAX;
    while(1){
        if (i > size)
            return i;
        i *= 2;
    }
}

// 字典哈希表扩容
int dictExpand(dict *d,unsigned long size){

    dictht n;
    // 确认扩容大小
    unsigned long realsize = _dictNextPower(size);

    //  rehash 和 已用节点数大于请求节点数
    if (dictIsRehashing(d) || d->ht[0].used > size) 
        return DICT_ERR;

    // 申请空间并更新哈希表属性
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.used = 0;

    // 如果 0 号哈希表为空,扩容哈希表赋值给 0 号哈希表
    // 如果 0 号哈希表非空,说明要进行 rehash , 赋值给 1 号哈希表
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 字典的哈希表扩容策略
static int _dictExpandIfNeeded(dict *d){

    // rehash 不进行扩容
    if (dictIsRehashing(d)) return DICT_ERR;

    // 初始化扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 可用哈希表空间不足扩容
    if (d->ht[0].used >= d->ht[0].size && 
        (dict_can_resize || (d->ht[0].used/d->ht[0].size) > dict_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

// 避免重复 key 加入字典
static int _dictKeyIndex(dict *d,void *key){

    int h,table,idx;
    dictEntry *he,*entry;

    // 是否需要扩容
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

        // 获取 key 的哈希值
    h = dictHashKey(d,key);

    // 遍历哈希表，确认 key 是否存在
    for(table=0; table<=1; table++){
        
        // 防止索引值溢出
        idx = h & d->ht[table].sizemask;
        
        he = d->ht[table].table[idx];
        //链表查找
        while(he){
            if (dictCompareKeys(d,key,he->key))
                return -1;
            he = he->next;
        }

        if (!dictIsRehashing(d)) break;
    }

    return idx;
}

// 避免重复 key 加入字典
dictEntry *dictAddRaw(dict *d,void *key){

    int index;
    dictht *ht;
    dictEntry *entry;

    // 获取 key 的索引值，返回 -1 说明 key 已经存在
    if ((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 确定哈希表,rehash状态写入 1 号哈希表
    // 否则写入 0 号哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    // 创建新节点
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    ht->table[index] = entry;

    // 增加哈希表使用节点数
    ht->used++;

    dictSetKey(d,entry,key);
    
    // 返回节点
    return entry;
}

// 以键值方式新增一个节点到指定字典
int dictAdd(dict *d,void *key,void *val){

    dictEntry *entry;
    // key 加入字典
    entry = dictAddRaw(d,key);
    if (!entry) return DICT_ERR;

    dictSetVal(d,entry,val);

    return DICT_OK;
}


void test_empty_dict(void){
    dict* d = dictCreate(&initDictType, NULL);

    dictRelease(d);
}

void test_add_and_delete_key_value_pair(void){
    // 创建新字典
    dict *d = dictCreate(&initDictType, NULL);

    // 创建键和值
    keyObject *k = keyCreate(1);
    valObject *v = valCreate(10086);

    // 添加键值对
    dictAdd(d, k, v);

    printf("dictAdd : dict size %d",dictSize(d));

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

// gcc -g zmalloc.c dictType.c dict.c
void main(void){

    // test_empty_dict();

    test_add_and_delete_key_value_pair();
}