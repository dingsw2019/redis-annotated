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
#include "time.h"


extern dictType initDictType;

static int dict_can_resize = 1;

static int dict_force_resize_ratio = 5;

static int _dictRehashStep(dict *d);

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
    if (dictIsRehashing(d)) _dictRehashStep(d);

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
    if (dictIsRehashing(d)) _dictRehashStep(d);

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
                    d->ht[table].table[idx] = he->next;
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
int dictDelete(dict *d,void *key){
    return dictGenericDelete(d,key,0);
}

// 获取一个不安全的迭代器
dictIterator *dictGetIterator(dict *d)
{
    // 申请内存
    dictIterator *iter = zmalloc(sizeof(*iter));

    // 初始化属性
    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

// 获取一个安全的迭代器
dictIterator *dictGetSafeIterator(dict *d)
{
    dictIterator *iter = dictGetIterator(d);
    iter->safe = 1;
    return iter;
}

// 获取当前节点,并迭代到下一个节点
// 成功返回节点,迭代完成返回 NULL
dictEntry *dictNext(dictIterator *iter)
{
    while(1){

        // 两种情况进入这里
        // 1. 初始化迭代器
        // 2. 某一节点链表迭代完,需要调到下一个节点链表
        if (iter->entry == NULL) {

            dictht *ht = &iter->d->ht[iter->table];
            // 初次迭代执行
            if (iter->table == 0 && iter->index == -1) {
                // 安全迭代器,更新安全迭代器计数器
                if (iter->safe) {
                    iter->d->iterators++;
                } 
                // 非安全迭代器,计算指纹
                else {
                    // iter->figerprint = dictFigerprint(d);
                }
            }

            iter->index++;

            // 索引值越界检查
            if (iter->index >= (signed)ht->size) {
                // rehash 状态,开始遍历 1 号哈希表
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }

            // 节点链表赋值
            iter->entry = ht->table[iter->index];
        }
        // 迭代下一个节点
        else {
            iter->entry = iter->nextEntry;
        }

        // 如果当前节点非空,给下一个节点指针复制
        // 返回节点
        if (iter->entry) {
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }

    // 迭代完了
    return NULL;
}

// 释放迭代器
void dictReleaseIterator(dictIterator *iter)
{
    // 非初始化迭代器
    if (!(iter->table == 0 && iter->index == -1)) {

        // 安全迭代器,更新安全迭代器的计数器
        if (iter->safe) {
            iter->d->iterators--;
        } 
        // 不安全迭代器,验证指纹
        else {
            // assert(iter->figerprint == dictFingerPrint(iter->d));
        }
    }

    zfree(iter);
}


#define DICT_STATS_VECTLEN 50
static void _dictPrintStatsHt(dictht *ht) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];

    if (ht->used == 0) {
        printf("No stats available for empty dictionaries\n");
        return;
    }

    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }
    printf("Hash table stats:\n");
    printf(" table size: %ld\n", ht->size);
    printf(" number of elements: %ld\n", ht->used);
    printf(" different slots: %ld\n", slots);
    printf(" max chain length: %ld\n", maxchainlen);
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);
    printf(" avg chain length (computed): %.02f\n", (float)ht->used/slots);
    printf(" Chain length distribution:\n");
    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        printf("   %s%ld: %ld (%.02f%%)\n",(i == DICT_STATS_VECTLEN-1)?">= ":"", i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }
}

void dictPrintStats(dict *d) {
    _dictPrintStatsHt(&d->ht[0]);
    if (dictIsRehashing(d)) {
        printf("-- Rehashing into ht[1]:\n");
        _dictPrintStatsHt(&d->ht[1]);
    }
}

// 打印节点的键值对
void dictPrintEntry(dictEntry *he){

    keyObject *key = (keyObject*)he->key;
    keyObject *val = (keyObject*)he->v.val;
    printf("dictPrintEntry,k=%d,v=%d\n",key->val,val->val);
}

// 打印所有节点键值对
void dictPrintAllEntry(dict *d)
{
    dictEntry *he;
    dictIterator *iter = dictGetIterator(d);

    while ((he = dictNext(iter)) != NULL) {
        dictPrintEntry(he);
    }

    dictReleaseIterator(iter);
}

// 获取随机值
int customRandom()
{
    srand(time(NULL));
    return rand();
}

// 随机获取一个节点
// 成功返回节点, 未找到返回 NULL
dictEntry *dictGetRandomKey(dict *d)
{   
    dictEntry *he,*origHe;
    unsigned long h;
    int listlen,listele;

    // 空字典,无法返回节点
    if (d->ht[0].size == 0) return NULL;

    // rehash 状态, 尝试 单步 rehash
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 获取随机的非空节点链表
    // 非rehash 状态, 从 0 号哈希表获取随机节点链表
    if (!dictIsRehashing(d)) {
        do {
            h = customRandom() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        }while(he == NULL);
    }
    // rehash状态, 从 0 号和 1 号哈希表随机获取节点链表
    else {
        do {
            h = customRandom() % (d->ht[0].size + d->ht[1].size);
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] : d->ht[0].table[h];
        }while(he == NULL);
    }

    origHe = he;
    // 计算链表长度
    listlen = 0;
    while(he){
        he = he->next;
        listlen++;
    }
    // 计算随机值
    listele = customRandom() % listlen;
    // 从节点链表中获取随机节点
    he = origHe;
    while(listele--) he = he->next;

    // 返回节点
    return he;
}


// N 步渐进式 rehash
// 返回 1 表示仍有节点需从 0 号哈希表移动到 1 号哈希表
// 返回 0 表示所有节点移动完毕 或 不需要移动
int dictRehash(dict *d,int n){

    // 非 rehash 状态, 不处理
    if (!dictIsRehashing(d)) return 0;

    while(n--){

        dictEntry *he,*nextHe;
        
        // 0 号哈希表为空, 表示 rehash 完成
        if (d->ht[0].used == 0) {
            // 释放 0 号哈希表内存
            zfree(d->ht[0].table);
            // 1 号哈希表拷贝给 0 号哈希表
            d->ht[0] = d->ht[1];
            // 重置 1 号哈希表
            _dictReset(&d->ht[1]);
            // 关闭 rehash 标识
            d->rehashidx = -1;
            // 返回 0
            return 0;
        }

        // 确保 rehashidx 没有越界
        assert(d->ht[0].size > (unsigned)d->rehashidx);

        // 跳过空节点链表
        while(d->ht[0].table[d->rehashidx] == NULL) d->rehashidx++;

        he = d->ht[0].table[d->rehashidx];
        // 遍历节点链表
        while(he){
            unsigned long h;
            nextHe = he->next;
            // 计算节点键在 1 号哈希表的哈希值和索引值
            h = dictHashKey(d,he->key) & d->ht[1].sizemask;
            // 添加节点到 1 号哈希表
            he->next = d->ht[1].table[h];
            d->ht[1].table[h] = he;
            // 更新哈希表已用节点数
            d->ht[0].used--;
            d->ht[1].used++;
            // 处理下一个节点
            he = nextHe;
        }

        // 0 号哈希表的当前移动的节点链表 置空
        d->ht[0].table[d->rehashidx] = NULL;
        // 更新 rehashidx
        d->rehashidx++;
    }

    return 1;
}

static int _dictRehashStep(dict *d)
{
    if(d->iterators == 0) dictRehash(d,1);
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

    dictPrintStats(d);
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

    // 节点值替换
    valObject *vRep = valCreate(10010);
    dictReplace(d,k,vRep);
    he = dictFind(d, k);
    if (he) {
        printf("dictReplace, find entry(k=1,value=10086) and replace value(10010)\n");
        dictPrintEntry(he);
    } else {
        printf("dictReplace, not find\n");
    }
    printf("---------------------\n");

    // 新增节点(dictReplace)
    keyObject *k2 = keyCreate(2);
    valObject *v2 = valCreate(10000);
    dictReplace(d,k2,v2);
    he = dictFind(d, k);
    if (he) {
        printf("dictAdd through dictReplace, add entry(k=2,value=10000) join dict\n");
        dictPrintEntry(he);
    } else {
        printf("dictAdd through dictReplace, not find entry\n");
    }
    dictPrintStats(d);
    printf("---------------------\n");

    // 随机获取一个节点
    he = dictGetRandomKey(d);
    if (he) {
        printf("dictGetRandomKey , ");
        dictPrintEntry(he);
    } else {
        printf("dictGetRandomKey , not find entry\n");
    }
    printf("---------------------\n");

    // 通过迭代器获取打印所有节点
    dictPrintAllEntry(d);
    printf("---------------------\n");

    // 删除节点
    ret = dictDelete(d, k);
    printf("dictDelete : %s, dict size %d\n\n",((ret==DICT_OK) ? "yes" : "no"),dictSize(d));
    
    // 释放字典
    dictRelease(d);

}