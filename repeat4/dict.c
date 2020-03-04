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

/* ---------------- private --------------- */
// 初始化或重置哈希表属性
static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典属性
static int _dictInit(dict *d,dictType *type,void *privDataPtr)
{
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;

    return DICT_OK;
}
/* ----------------- API -------------- */

// 创建一个空字典
dict *dictCreate(dictType *type,void *privDataPtr)
{
    // 申请内存
    dict *d = zmalloc(sizeof(*d));

    // 初始化属性
    _dictInit(d,type,privDataPtr);

    return d;
}

// 释放节点数组并重置哈希表
// 成功返回 DICT_OK , 否则返回 DICT_ERR
int _dictClear(dict *d,dictht *ht,void (callback)(void *))
{
    unsigned long i;

    // 遍历哈希表节点数组
    for(i=0;i<ht->size && ht->used>0; i++){

        dictEntry *he,*nextHe;
        // 回调函数只调用一次
        if (callback && (i & 65535)==0) callback(d->privdata);
        
        // 跳过空节点
        if ((he = ht->table[i]) == NULL) continue;

        // 遍历节点
        while(he){
            
            nextHe = he->next;
            // 释放值
            dictFreeVal(d,he);
            // 释放键
            dictFreeKey(d,he);
            // 释放节点
            zfree(he);
            // 更新已用节点数
            ht->used--;
            // 处理下一个节点
            he = nextHe;
        }
    }


    // 释放哈希表
    zfree(ht->table);

    // 重置哈希表
    _dictReset(ht);

    // 返回
    return DICT_OK;
}

// 释放字典
dict *dictRelease(dict *d)
{
    // 释放哈希表
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);

    // 释放字典
    zfree(d);
}

// 扩容大小策略, 返回扩容大小的值
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if (size >= LONG_MAX) return LONG_MAX;
    while(1){
        if (i > size)
            return i;
        i *= 2;
    }
}

// 哈希表扩容
// 成功返回 DICT_OK, 否则 DICT_ERR
int dictExpand(dict *d,unsigned long size)
{
    dictht n;
    // 计算扩容大小
    unsigned long realsize = _dictNextPower(size);

    // rehash状态 或 使用节点数大于 size
    // 两种情况不进行扩容
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 设置哈希表属性
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = zcalloc(realsize*(sizeof(dictEntry*)));
    n.used = 0;

    // 0 号哈希表为空, 就进行初始化赋值, 将哈希表赋给 0 号哈希表
    if (d->ht[0].size == 0) {
        d->ht[0] = n;
        return DICT_OK;
    }

    // 否则给 1 号哈希表, 并开启 rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 是否开启强制扩容
static int dict_can_resize = 1;
// 强制扩容的节点使用率
static unsigned long dict_force_resize_ratio = 5;
// 扩容控制策略
// 成功返回 DICT_OK, 否则 DICT_ERR
static int _dictExpandIfNeeded(dict *d)
{
    // rehash 不扩容
    if (dictIsRehashing(d)) return DICT_OK;

    // 0 号哈希表为空, 初始化扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 节点空间严重不足, 2倍扩容
    if (d->ht[0].used>=d->ht[0].size &&
        (dict_can_resize || (d->ht[0].used/d->ht[0].size > dict_force_resize_ratio)))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

// 计算哈希值,并校验哈希值是否已存在
// 如果存在,返回 -1
// 不存在,返回哈希值
static int _dictKeyIndex(dict *d,void *key)
{
    unsigned int h,idx,table;
    dictEntry *he;

    // 扩容检查
    if(_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 计算哈希值
    h = dictHashKey(d,key);

    // 遍历哈希表
    for(table=0; table<=1; table++){

        // 计算索引值
        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];
        // 遍历节点链表
        while(he){
            // 相同的键 返回 -1
            if (dictCompareKey(d,he->key,key))
                return -1;
            // 处理下一个节点
            he = he->next;
        }

        if (!dictIsRehashing(d)) break;
    }

    // 返回索引值
    return idx;
}

// 确定键是否存在
// 如果存在,无法添加返回 NULL
// 不存在,添加节点并加入键,返回节点
dictEntry *dictAddRaw(dict *d,void *key)
{
    unsigned int index;
    dictht *ht;
    dictEntry *entry;

    // 获取哈希值,失败返回NULL
    if ((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 确定哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    // 申请节点内存
    entry = zmalloc(sizeof(*entry));

    // 节点下一个指针指向,节点数组的首地址
    entry->next = ht->table[index];
    // 新节点添加到数组
    ht->table[index] = entry;

    // 更新已用节点数
    ht->used++;

    // 设置节点键
    dictSetKey(d,entry,key);

    // 返回节点
    return entry;
}

// 添加节点
// 成功返回 DICT_OK , 否则返回 DICT_ERR
int dictAdd(dict *d,void *key,void *val)
{
    // 尝试添加节点
    dictEntry *entry = dictAddRaw(d,key);

    // 添加失败
    if (!entry) return DICT_ERR;

    // 设置节点值
    dictSetVal(d,entry,val);

    return DICT_OK;
}

// 查找节点,找到返回节点,否则返回 NULL
dictEntry *dictFind(dict *d,void *key)
{   
    dictEntry *he;
    unsigned int h, idx, table;
    // 空表不查找
    if (d->ht[0].size == 0) return NULL;

    // 尝试单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算哈希值
    h = dictHashKey(d,key);

    // 遍历哈希表
    for(table=0; table<=1; table++){
        // 计算索引值
        idx = h & d->ht[table].sizemask;

        // 确定节点链表
        he = d->ht[table].table[idx];

        // 遍历链表
        while(he){
            // 相同key,返回节点
            if (dictCompareKey(d,he->key,key))
                return he;
            // 处理下一个节点
            he = he->next;
        }

        // 非 rehash 状态, 不处理 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 未找到
    return NULL;
}

/* ----------------- debug -------------- */
// 打印节点的键值对
void dictPrintEntry(dictEntry *he)
{
    keyObject *k = (keyObject*)he->key;
    valObject *v = (valObject*)he->v.val;
    printf("dictPrintEntry, k=%d,v=%d\n",k->val,v->val);
}


// gcc -g zmalloc.c dictType.c dict.c
void main(void)
{
    int ret;
    dictEntry *he;

    // 创建一个空字典
    dict *d = dictCreate(&initDictType, NULL);

    // 节点的键值
    keyObject *k = keyCreate(1);
    valObject *v = valCreate(10086);

    // 添加节点
    dictAdd(d, k, v);

    // dictPrintStats(d);
    printf("\ndictAdd, (k=1,v=10086) join dict,dict used size %d\n",dictSize(d));
    printf("---------------------\n");

    // 查找节点
    he = dictFind(d, k);
    if (he) {
        printf("dictFind,k is 1 to find entry\n");
        dictPrintEntry(he);
    } else {
        printf("dictFind, not find\n");
    }
    printf("---------------------\n");

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
    // dictPrintStats(d);
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
    
    // 释放字典
    dictRelease(d);

}