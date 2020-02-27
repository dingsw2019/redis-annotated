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

// 启动强制扩容哈希表
static int dict_can_resize = 1;

// 强制扩容哈希表,规定的哈希表使用率
static int dict_force_resize_ratio = 5;

//----------------------------------------

// 重置或初始化哈希表
static void _dictReset(dictht *ht){
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典
int _dictInit(dict *d,dictType *type,void *privDataPtr){

    // 初始化哈希表
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;

    return DICT_OK;
}

// 创建一个空字典
dict *dictCreate(dictType *type,void *privDataPtr){

    // 申请字典内存
    dict *d = zmalloc(sizeof(*d));
    // 初始化字典属性
    _dictInit(d,type,privDataPtr);

    return d;
}

// 释放哈希表
int _dictClear(dict *d, dictht *ht, void (callback)(void *)){

    unsigned long i;
    // 遍历哈希表
    for(i=0; i<ht->size && ht->used>0; i++){
        dictEntry *he,*nextHe;
        // 回调函数执行一次
        if (callback && (i & 65535)==0) callback(d->privdata);

        // 跳过空节点
        if ((he = ht->table[i]) == NULL)
            continue;

        // 遍历相同哈希值的节点
        while(he){
            nextHe = he->next;
            // 释放键
            dictFreeKey(d,he);
            // 释放值
            dictFreeVal(d,he);
            // 释放节点
            zfree(he);
            // 更新哈希表已用节点数
            ht->used--;
            // 处理下一个节点
            he = nextHe;
        }
    }

    // 释放哈希表数组
    zfree(ht->table);

    // 重置哈希表
    _dictReset(ht);

    return DICT_OK;
}

// 释放字典
void dictRelease(dict *d){

    // 释放哈希表
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);

    // 释放字典
    zfree(d);
}

// 扩容大小策略
static unsigned long _dictNextPower(unsigned long size){
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if (size >= LONG_MAX) return LONG_MAX;
    while(1){
        if (i > size){
            return i;
        }
        i *= 2;
    }
}

// 扩容执行
int dictExpand(dict *d,unsigned long size){
    unsigned long realsize;
    dictht n;
    // 计算扩容大小
    realsize = _dictNextPower(size);
    // 不扩容情况
    // 1.处于 rehash 状态 
    // 2.哈希表已用节点数大于申请大小
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 申请内存
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    // 哈希表属性赋值
    n.size = realsize;
    n.sizemask = realsize-1;
    n.used = 0;

    // 0 号哈希表为空,赋值给 0 号哈希表
    if (d->ht[0].size == 0) {
        d->ht[0] = n;
        return DICT_OK;
    }

    // 0 号哈希表非空,赋值给 1 号哈希表,并开启 rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 哈希表扩容控制策略
static int _dictExpandIfNeeded(dict *d){

    // rehash 状态,不进行扩容
    if (dictIsRehashing(d)) return DICT_ERR;
    // 0 号哈希表为空,进行初始化扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);
    // 已用大小超过设置大小 同时
    // 启动强制扩容 或 哈希表使用率大于强制扩容设置的使用率
    // 进行 2 倍扩容
    if (d->ht[0].used >= d->ht[0].size && 
        (dict_can_resize || 
        (d->ht[0].used/d->ht[0].size)>dict_force_resize_ratio))
    {
        return dictExpand(d,2*d->ht[0].used);
    }
    return DICT_OK;
}

// 计算哈希值并检查该值是否存在
// 存在返回 -1, 否则返回哈希值
static int _dictKeyIndex(dict *d,const void *key){

    unsigned int h,table,idx;
    dictEntry *he;

    // 是否需要扩容
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 计算哈希值
    h = dictHashKey(d,key);
    // 遍历哈希表,查找哈希值是否存在,找到返回 -1
    for(table=0; table<=1; table++){
        
        // 检查哈希值,防止溢出
        idx = h & d->ht[table].sizemask;
        // 根据哈希值获取节点
        he = d->ht[table].table[idx];
        
        // 检查链表
        while(he){
            if (dictCompareKey(d,key,he->key)){
                return -1;
            }
            he = he->next;
        }

        // 非 rehash 状态,不需要检查 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 返回哈希值
    return idx;
}

// 添加节点的键,如果键已存在不添加
dictEntry *dictAddRaw(dict *d,void *key){

    int index;
    dictEntry *entry;
    dictht *ht;

    // 获取键的哈希值,如果返回 -1 代表键已存在
    if((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 选择添加新节点的哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    // 新节点指针指向 NULL
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    // 新节点添加到哈希表中
    ht->table[index] = entry;
    // 更新哈希表已用节点数
    ht->used++;
    // 设置新节点的键
    dictSetKey(d,entry,key);
    // 返回新节点
    return entry;
}

// 添加节点
int dictAdd(dict *d,void *key,void *val){

    dictEntry *entry;
    // 尝试添加节点的键
    entry = dictAddRaw(d,key);
    // 如果键已存在,不执行添加
    if (!entry) return DICT_ERR;
    // 设置节点的值
    dictSetVal(d,entry,val);
    
    return DICT_OK;
}

//----------------------------------------

void test_empty_dict(void)
{
    dict* d = dictCreate(&initDictType, NULL);

    dictRelease(d);
}

void test_add_and_delete_key_value_pair(void)
{
    //创建新字典
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
void main(void)
{

    // test_empty_dict();
    test_add_and_delete_key_value_pair();

}