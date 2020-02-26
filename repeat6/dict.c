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

static int dict_can_resize = 1;

static unsigned int dict_force_resize_ratio = 5;

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

// 扩容大小策略,返回第一个大于size的2的n次幂
static unsigned long _dictNextPower(unsigned long size){
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if (size >= LONG_MAX) return LONG_MAX;
    while (1) {
        if (i > size){
            return i;
        }
        i *= 2;
    }
}

// 扩容
int dictExpand(dict *d,unsigned long size){

    dictht n;
    unsigned long realsize;
    // 计算哈希表要扩容的大小
    realsize = _dictNextPower(size);
    // 不进行扩容的情况
    // 1. 正在rehash
    // 2. 0号哈希表已用节点数大于扩容节点数
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 哈希表参数赋值
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.size = realsize;
    n.sizemask = realsize-1;
    n.used = 0;

    // 如果 0 号哈希表为空,那么这是一次初始化
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    // 如果 0 号哈希表非空,那么这是一次 rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;

}

// 扩容控制策略
static int _dictExpandIfNeeded(dict *d){

    // 处于 rehash 状态,不扩容
    if (dictIsRehashing(d)) return DICT_ERR;
    // 0 号哈希表为空,进行哈希表数组初始化内存的申请
    if (d->ht[0].table == NULL) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 节点数量超过最大值 同时
    // 启动强制扩容 或者 节点使用率超过 dict_force_resize_ratio
    if (d->ht[0].used > d->ht[0].size &&
         (dict_can_resize || 
          d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

// 获取键的哈希值, -1 表示键已存在
static int dictKeyIndex(dict *d,const void *key){

    int i;
    unsigned int h,table,idx;
    dictEntry *he;

    // 扩容控制策略,不满足返回-1
    if(_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 计算哈希值
    h = dictHashKey(d,key);

    // 遍历哈希表,查找哈希值是否存在
    // 存在返回 -1
    for(table=0; table<=1; table++){

        // 计算索引值,防止溢出
        idx = h & d->ht[table].sizemask;
        // 查找节点
        he = d->ht[table].table[idx];
        while(he){
            if (dictCompareKey(d,key,he->key)) {
                return -1;
            }
            he = he->next;
        }

        // 非rehash,不需要搜索ht[1]
        if (!dictIsRehashing(d)) break;
    }

    // 返回哈希值
    return idx;
}

// 增加节点的键
dictEntry *dictAddRaw(dict *d,void *key){

    int index;
    dictEntry *entry;
    dictht *ht;
    // 获取键的哈希值,-1 表示键已存在
    if ((index = dictKeyIndex(d,key)) == -1)        
        return NULL;
    
    // 确认赋值的哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    // 申请节点内存
    entry = zmalloc(sizeof(*entry));
    // 节点指针赋值为NULL
    entry->next = ht->table[index];
    // 新节点添加到链表表头
    ht->table[index] = entry;
    // 更新已用节点数
    ht->used++;
    // 设置新节点的键
    dictSetKey(d,entry,key);

    // 返回节点
    return entry;
}

// 增加节点
int dictAdd(dict *d,void *key,void *val){

    // 尝试添加键,如果键已存在,返回NULL
    dictEntry *entry = dictAddRaw(d,key);

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