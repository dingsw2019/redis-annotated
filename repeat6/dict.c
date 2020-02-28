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

static int dict_force_resize_ratio = 5;

// 重置或初始化哈希表参数
static void _dictReset(dictht *ht){
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典参数
static int _dictInit(dict *d,dictType *type,void *privDataPtr){
    // 初始化哈希表
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    // 初始化其他参数
    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;
}

// 创建一个空字典
dict *dictCreate(dictType *type,void *privDataPtr){

    // 申请字典内存
    dict *d = zmalloc(sizeof(*d));
    // 初始化参数
    _dictInit(d,type,privDataPtr);

    return d;
}

// 释放哈希表所有节点,并重置哈希表属性
int _dictClear(dict *d,dictht *ht,void (callback)(void *)){

    unsigned long i;
    // 遍历哈希表的节点数组
    for(i=0; i<ht->size && ht->used>0; i++){
        dictEntry *he,*nextHe;
        // 调用一次,自定义回调函数
        if (callback && (i & 65535)==0) callback(d->privdata);
        // 跳过空节点
        if ((he = ht->table[i]) == NULL) continue;
        // 遍历链表
        while(he){
            nextHe = he->next;
            // 释放键
            dictFreeKey(d,he);
            // 释放值
            dictFreeVal(d,he);
            // 释放节点
            zfree(he);
            // 更新已用节点数
            ht->used--;
            he = nextHe;
        }
    }

    // 释放节点数组
    zfree(ht->table);

    // 重置哈希表属性
    _dictReset(ht);

    return DICT_OK;
}

// 释放字典
void dictRelease(dict *d){

    // 释放哈希表数组,并重置属性
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

// 哈希表数组扩容执行
int dictExpand(dict *d,unsigned long size){

    unsigned long realsize;
    dictht n;
    // 获取实际扩容大小
    realsize = _dictNextPower(size);

    // 不可扩容情况
    // 1.rehash 状态
    // 2.已用节点数大于请求扩容大小
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 申请数组内存
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    // 设置哈希表属性
    n.size = realsize;
    n.sizemask = realsize-1;
    n.used = 0;

    // 0 号哈希表为空,说明是哈希表数组的初始化行为
    if (d->ht[0].size == 0) {
        d->ht[0] = n;
        return DICT_OK;
    }
    // 0 号哈希表非空,说明是 rehash 行为
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 扩容策略控制
// 不可扩容返回 DICT_ERR
static int _dictExpandIfNeeded(dict *d){

    // 不扩容情况
    // rehash 状态
    if (dictIsRehashing(d)) return DICT_OK;

    // 0 号哈希表为空,进行数组初始化扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 已用节点数超节点数,同时启动强制扩容参数
    // 或 节点使用率大于强制扩容节点使用率
    // 进行 2 倍扩容
    if (d->ht[0].used >= d->ht[0].size && 
        (dict_can_resize || 
        (d->ht[0].used/d->ht[0].size) > dict_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

// 获取键的哈希值
// 如果哈希值已存在数组中,返回 -1
// 哈希值不存在数组,返回哈希值
static int _dictKeyIndex(dict *d,const void *key){

    unsigned long h,table,idx;
    dictEntry *he;
    // 扩容检查
    if(_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;
    
    // 计算 key 的哈希值
    h = dictHashKey(d,key);

    // 查找哈希值是否已存在于数组中
    for(table=0; table<=1; table++){

        // 防止哈希值溢出
        idx = h & d->ht[table].sizemask;
        // 哈希值定位节点
        he = d->ht[table].table[idx];
        // 节点链表查找
        while(he){
            if (dictCompareKey(d,he->key,key)){
                return -1;
            }
            he = he->next;
        }

        // 非 rehash 状态,不需要遍历 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    return idx;
}

// 添加节点的键,并确定键是否存在
// 如果键存在不新增
dictEntry *dictAddRaw(dict *d,void *key){

    int index;
    dictEntry *entry;
    dictht *ht;
    // 计算 key 的哈希值
    // 如果哈希值已存在于数组中 返回值为 -1
    if ((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 更新的哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    
    // 申请节点内存
    entry = zmalloc(sizeof(*entry));
    // 节点指针指向 NULL
    entry->next = ht->table[index];
    // 哈希表数组添加新节点
    ht->table[index] = entry;
    // 节点键赋值
    dictSetKey(d,entry,key);
    // 更新已用节点数
    ht->used++;

    return entry;
}

// 添加节点
int dictAdd(dict *d,void *key,void *val){

    // 新增节点
    dictEntry *entry = dictAddRaw(d,key);
    // 新增失败
    if (!entry) return DICT_ERR;
    // 设置节点的值
    dictSetVal(d,entry,val);

    return DICT_OK; 
}

/**
 * 返回字典的键为 key 的节点
 * 找到返回节点,未找到返回 NULL
 * 
 * T = O(N)
 */
dictEntry *dictFind(dict *d,void *key){

    dictEntry *he;
    unsigned long h,idx,table;

    // 空字典,不进行查找
    if (d->ht[0].size == 0) return NULL;

    // 尝试进行单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算键的哈希值
    h = dictHashKey(d,key);

    // 遍历哈希表
    for(table=0; table<=1; table++){

        // 计算索引值
        idx = h & d->ht[table].sizemask;

        // 遍历节点链表
        he = d->ht[table].table[idx];
        while(he){
            // 查找相同 key
            if (dictCompareKey(d,key,he->key))
                return he;
            
            he = he->next;
        }

        // 非 rehash 状态, 不查 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 未找到
    return NULL;
}

/**
 * 将键值对添加到字典的哈希表数组
 * 
 * 如果键值对添加成功,返回 1
 * 如果键已存在,替换该节点的val值,返回 0
 * 
 * T = O(N)
 */
int dictReplace(dict *d,void *key,void *val){

    dictEntry *entry,auxentry;
    // 尝试添加新节点,如果键不存在,添加成功
    if(dictAdd(d,key,val) == DICT_OK)
        return 1;

    // 运行到这里,说明 key 已存在于数组中
    // 找到 key 的节点
    entry = dictFind(d,key);

    // 保存旧值(val)的指针
    auxentry = *entry;

    // 设置新值(val)
    dictSetVal(d,entry,val);

    // 释放旧值
    // 复制节点为保留旧值的首地址,设置新值(val)后,
    // val 的地址就变了,也就无法释放了
    dictFreeVal(d,&auxentry);

    return 0;
}

/**
 * 查找并释放给定键的节点
 * 
 * 参数 nofree 决定是否调用键和值的释放函数
 * 0 调用 , 1 不调用
 * 
 * 找到并释放返回 DICT_OK
 * 未找到返回 DICT_ERR
 */
int dictGenericDelete(dict *d,const void *key,int nofree){

    unsigned long h,idx,table;
    dictEntry *he,*prevHe;

    // 空字典,返回
    if (d->ht[0].size == 0) return DICT_ERR;
    // 进行单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算哈希值
    h = dictHashKey(d,key);

    // 遍历哈希表
    for(table=0; table<=1; table++){

        // 计算索引值
        idx = h & d->ht[table].sizemask;

        // 遍历节点链表
        he = d->ht[table].table[idx];
        prevHe = NULL;
        while(he){
            
            // 找到相同节点键
            if (dictCompareKey(d,key,he->key)) {

                if (prevHe){
                    // 删除链表非首节点
                    // 更新删除节点的前一个节点指针
                    // 指向删除节点的下一个节点
                    prevHe->next = he->next;
                } else {
                    // 删除链表的第一个节点
                    // 变更链表首地址为第一个节点
                    d->ht[table].table = he->next;
                }

                if (!nofree) {
                    // 释放键
                    dictFreeKey(d,he);
                    // 释放值
                    dictFreeVal(d,he);
                }
                // 释放节点
                zfree(he);
                // 更新哈希表已用节点数
                d->ht[table].used--;

                return DICT_OK;
            }
            // 记录上一个节点
            prevHe = he;
            // 处理下一个节点
            he = he->next;
        }

        // 非 rehash 状态,不查找 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 未找到
    return DICT_ERR;
}

/**
 * 从字典释放包含给定键的节点
 * 释放返回 DICT_OK,未找到返回 DICT_ERR
 * T = O(1)
 */
int dictDelect(dict *d,void *key){
    return dictGenericDelete(d,key,0);
}

void test_empty_dict(void)
{
    dict* d = dictCreate(&initDictType, NULL);

    dictRelease(d);
}

void test_add_and_delete_key_value_pair(void)
{
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
void main(void)
{

    // test_empty_dict();

    test_add_and_delete_key_value_pair();

}