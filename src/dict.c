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
// #include "intdicttype.h"
#include "dictType.h"

extern dictType initDictType;

// 指示字典是否启用 rehash 的标识
static int dict_can_resize = 1;
// 强制 rehash 的比率
static unsigned int dict_force_resize_ratio = 5;

// 扩容策略
static int _dictExpandIfNeeded(dict *ht);
// 扩容大小策略
static unsigned long _dictNextPower(unsigned long size);
// 确定哈希表索引值
static int _dictKeyIndex(dict *ht,const void *key);
// 字典初始化
static int _dictInit(dict *ht,dictType *type,void *privDataPtr);

/**
 * 重置或初始化指定哈希表的各项属性值
 * 
 * T = O(1)
 */
static void _dictReset(dictht *ht){

    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/**
 * 初始化字典各项属性值
 * 
 * T = O(1)
 */
int _dictInit(dict *d,dictType *type, void *privDataPtr){

    // 初始化两个哈希表的各项属性值
    // 但暂时不分配内存给哈希表数组
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    // 设置类型特定函数
    d->type = type;

    // 设置私有数据
    d->privdata = privDataPtr;

    // 设置哈希表 rehash 状态
    d->rehashidx = -1;

    // 设置字典的安全迭代器数量
    d->iterators = 0;

    return DICT_OK;
}

/**
 * 创建一个新字典
 * 
 * T = O(1)
 */
dict *dictCreate(dictType *type,void *privDataPtr){

    // 申请内存
    dict *d = zmalloc(sizeof(*d));

    // 初始化字典各项属性值
    _dictInit(d,type,privDataPtr);

    return d;
}


/**
 * 删除哈希表上的所有节点，并重置哈希表的各项属性
 * 
 * T = O(N)
 */
int _dictClear(dict *d,dictht *ht,void (callback)(void *)){

    unsigned long i;

    // 遍历哈希表,删除节点
    for(i=0;i < ht->size && ht->used > 0; i++){
        dictEntry *he, *nextHe;
        // 回调函数只调用一次,当 i=0 时调用
        // 疑问：为什么不用 i=0 来判断? (i & 65535) 优点是什么?
        if (callback && (i & 65535)==0) callback(d->privdata);

        // 跳过无节点内容的空索引
        if ((he = ht->table[i]) == NULL) continue;

        // 遍历节点链表
        while(he){
            nextHe = he->next;
            // 删除键
            dictFreeKey(d,he);
            // 删除值
            dictFreeVal(d,he);
            // 释放节点
            zfree(he);
            // 更新已使用节点数量
            ht->used--;
            // 处理下一个节点
            he = nextHe;
        }
    }

    // 释放哈希表结构
    zfree(ht->table);

    // 重置哈希表属性
    _dictReset(ht);

    return DICT_OK;
}

/**
 * 删除并释放指定字典
 * 
 * T = O(N)
 */
void dictRelease(dict *d){

    // 删除并清空两个哈希表
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);

    // 释放字典
    zfree(d);
}

/**
 * 尝试将给定键值对添加到字典中
 * 
 * 只有给定键 key 不存在于字典时，添加操作才会成功
 * 
 * 成功返回 DICT_OK ， 失败返回 DICT_ERR
 * 
 * 最坏 T = O(N) , 平摊 O(1)
 */
int dictAdd(dict *d,void *key,void *val){

    // 尝试添加键到字典，并返回包含了这个键的新哈希节点
    dictEntry *entry = dictAddRaw(d,key);

    // 键已存在，添加失败
    if (!entry) return DICT_ERR;

    // 键不存在，设置节点的值
    dictSetVal(d,entry,val);

    return DICT_OK;
}


/**
 * 返回可以将 key 插入到哈希表的索引位置
 * 如果 key 已经存在于哈希表，那么返回 -1
 * 
 * 如果字典正在进行 rehash，那么总返回 1 号哈希表的索引
 * 因为字典处在 rehash 时，新节点总是插入到 1 号哈希表
 */
static int _dictKeyIndex(dict *d,const void *key){

    unsigned int h,idx,table;
    dictEntry *he;

    // 是否需要扩容
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 计算 key 的哈希值
    h = dictHashKey(d,key);

    // 查找 key 是否存在
    for(table=0; table<=1; table++){

        // 计算索引值，防止溢出
        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];
        // 链表查找 key 是否存在
        while (he) {
            if (dictCompareKeys(d,key,he->key))
                return -1;
            he = he->next;
        }

        // 如果运行到这里，说明 0 号哈希表不包含 key
        // 如果这是处在 rehash 状态, 那么继续查找 1 号哈希表是否存在 key
        if (!dictIsRehashing(d)) break;
    }

    // 返回索引
    return idx;
}

/**
 * 根据需要，初始化字典的哈希表，或者对字典现有的哈希表进行扩容
 */
static int _dictExpandIfNeeded(dict *d){

    // 如果字典处于 rehash 状态，不进行扩容
    if (dictIsRehashing(d)) return DICT_OK;

    // 如果字典的 0 号哈希表为空，说明是初始化哈希表
    // 根据初始化哈希表大小进行扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 节点数量超过最大值 同时 
    // 满足(启动强制扩容 或者 节点使用率超过 dict_force_resize_ratio)
    // 根据当前节点数量两倍的大小进行扩容
    if (d->ht[0].used >= d->ht[0].size && 
        (dict_can_resize || 
         d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

/**
 * 创建一个新的哈希表，并根据字典的情况，选择以下其中一个动作来进行
 * 
 * 1) 如果字典的 0 号哈希表为空，那么将新哈希表设置为 0 号哈希表
 * 2) 如果字典的 0 号哈希表非空，那么将新哈希表设置为 1 号哈希表
 *    并打开字典的 rehash 标识，使得程序可以开始对字典进行 rehash
 * 
 * size 参数不够大，或者 rehash 已经在进行时，返回 DICT_ERR 。
 * 
 * 成功创建 0 号哈希表，或者 1 号哈希表时，返回 DICT_OK
 */
int dictExpand(dict *d,unsigned long size){
    // 新哈希表
    dictht n;

    // 根据 size 计算哈希表的大小
    unsigned long realsize = _dictNextPower(size);

    // 不进行扩容的情况
    // 1) 字典正在进行 rehash
    // 2) size 的值小于 0 号哈希表已用节点数
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 为哈希表分配空间，并初始化哈希表属性值
    n.size = realsize;
    n.sizemask = realsize-1;
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.used = 0;

    // 如果 0 号哈希表为空，那么这是一次初始化
    // 程序将新哈希表赋给 0 号哈希表的指针，然后字典就可以开始处理键值对
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    // 如果 0 号哈希表非空，那么这是一次 rehash
    // 程序将新哈希表设置为 1 号哈希表
    // 并将字典的 rehash 标识打开，让程序可以开始对字典进行 rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}


/**
 * 计算第一个大于等于 size 的 2 的 N 次方，用作哈希表的值
 */
static unsigned long _dictNextPower(unsigned long size){
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if (size >= LONG_MAX) return LONG_MAX;
    while(1){
        if (i > size)
            return i;
        i *= 2;
    }
}

/**
 * 尝试将键插入到字典中
 * 
 * 如果键已经在字典存在，那么返回 NULL
 * 
 * 如果键不存在，那么创建新的哈希节点
 * 将节点和键关联，并插入到字典，然后返回节点
 * 
 * T = O(N)
 */
dictEntry *dictAddRaw(dict *d,void *key){

    int index;
    dictEntry *entry;
    dictht *ht;

    // 如果字典处在 rehash 状态，进行单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算键在哈希表中的索引值
    // 如果值为 -1 ， 那么表示键已经存在
    // T = O(N)
    if ((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 如果字典处在 rehash 状态，那么将新键添加到 1 号哈希表
    // 否则，添加到 0 号哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    // 为新节点分配空间
    entry = zmalloc(sizeof(*entry));
    // 新节点的下一个节点为 NULL
    entry->next = ht->table[index];
    // 将新节点插入到链表表头
    ht->table[index] = entry;

    // 更新哈希表已使用节点数量
    ht->used++;

    // 设置新节点的键
    dictSetKey(d,entry,key);

    // 返回节点
    return entry;
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
            if (dictCompareKeys(d,key,he->key))
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
 * 自定义随机函数 (非 redis 函数)
 * 因为 C99编译器没有 random 函数
 */ 
int customRandom()
{
    srand(time(NULL));
    return rand();
}

/**
 * 从字典中随机返回一个节点
 * 
 * 空字典返回 NULL , 否则返回 节点
 *
 * T = O(N)
 */
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he,*origHe;
    unsigned int h;
    int listlen, listele;

    // 空字典不进行处理
    if (d->ht[0].size == 0) return NULL;

    // 处在 rehash 状态,进行单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // rehash状态,处理 主副哈希表
    if (dictIsRehashing(d)){
        // 随机获取非空节点链表
        do {
            // 随机索引值,根据主副哈希表的长度计算随机值
            h = customRandom() % (d->ht[0].size+d->ht[1].size);
            // 取出节点链表首地址
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] :
                                        d->ht[0].table[h];
        }while(he == NULL);

    } 
    // 非 rehash 状态,处理主哈希表
    else {
        // 随机获取非空节点链表
        do {
            // 随机索引值 (gcc 没有random)
            h = customRandom() & d->ht[0].sizemask;
            // 取出节点链表首地址
            he = d->ht[0].table[h];
        }while(he == NULL);
    }

    // 计算链表长度
    listlen = 0;
    origHe = he;
    while(he){
        he = he->next;
        listlen++;
    }
    // 根据链表长度获取随机值
    listele = customRandom() % listlen;

    // 获取随机值指向的节点
    he = origHe;
    while(listele--) he = he->next;

    // 返回节点
    return he;
}

/**
 * 随机起始节点,然后连续取最多 count 个节点
 * 并存入 des 中
 * 
 * 返回添加到 des 的节点数, 未添加返回 0
 */
int dictGetRandomKeys(dict *d,dictEntry **des,int count){

    int j;
    int stored = 0;

    // 节点数小于 count,设置count为已有节点数
    if (dictSize(d) < count) count = dictSize(d);

    while (stored < count){
        // 遍历主副哈希表
        for(j=0; j<2; j++){

            // 获取随机数并确定哈希表索引值
            unsigned int i = customRandom() & d->ht[j].sizemask;
            int size = d->ht[j].size;

            // 遍历随机节点链表后的所有节点链表
            // 包含随机节点链表
            while(size--){
                dictEntry *he = d->ht[j].table[i];
                // 遍历节点链表
                while(he){

                    // 填充 count 个节点到 des
                    *des = he;
                    des++;
                    // 下一个节点
                    he = he->next;
                    stored++;
                    // 填充完成,返回填充数
                    if (stored == count) return stored;
                    
                }
                // 下一个节点链表
                i = (i+1) & d->ht[j].sizemask;
            }

            // 非 rehash 状态,不遍历 1 号哈希表
            assert(dictIsRehashing(d) != 0);
        }
    }

    // 返回填充数
    return stored;
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
 * 新增或查找包含指定键的节点
 * 
 * key 已存在, 返回节点
 * key 不存在, 新增节点并返回
 * 出发任一情况,始终返回节点,新增的节点没有 value
 *
 * T = O(N) 
 */
dictEntry *dictReplaceRaw(dict *d,void *key){

    // 查找节点是否存在
    dictEntry *entry = dictFind(d,key);
    // 节点存在直接返回,否则新增节点
    // todo可优化,dictAddRaw中包含find逻辑,已经find过,只需要一个单纯add的函数就可以了
    return entry ? entry : dictAddRaw(d,key);
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
            if (dictCompareKeys(d,key,he->key)) {

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

/**
 * 从字典删除给定键的节点
 * 但不释放节点
 * 
 * 删除成功返回 DICT_OK, 未找到返回 DICT_ERR
 *
 * T = O(1)
 */
int dictDeleteNoFree(dict *ht,void *key)
{
    return dictGenericDelete(ht,key,1);
}

/**
 * 获取一个非安全迭代器
 */
dictIterator *dictGetIterator(dict *d)
{
    // 申请迭代器内存空间
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

/**
 * 获取一个安全迭代器
 */
dictIterator *dictGetSafeIterator(dict *d)
{
    // 创建并初始化一个非安全迭代器
    dictIterator *iter = dictGetIterator(d);
    // 设置为安全迭代器
    iter->safe = 1;

    return iter;
}

/**
 * 获取当前节点
 * 并将节点指针迭代到下一个节点
 * 
 * 正常返回节点, 迭代完成返回 NULL
 */
dictEntry *dictNext(dictIterator *iter){


    while(1){

        // 两种可能进入循环
        // 1. 迭代器第一次运行
        // 2. 当前节点链表的所有节点已迭代完
        if (iter->entry == NULL) {

            // 处理的哈希表
            dictht * ht = &iter->d->ht[iter->table];

            // 初次迭代执行
            if (iter->index == -1 && iter->table == 0) {
                
                // 安全迭代器，更新安全迭代器计数器
                if (iter->safe) {
                    iter->d->iterators++;
                } 
                // 不安全迭代器，计算指纹
                else {
                    // iter->fingerprint = dictFingerprint(iter->d);
                }
            }

            // 更新索引
            iter->index++;

            // 哈希表迭代完成检查
            // 迭代节点数等于哈希表节点总数,说明当前哈希表迭代完成
            if (iter->index >= (signed)ht->size) {

                // rehash 状态,继续迭代 1 号哈希表
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }

            iter->entry = ht->table[iter->index];
        } 
        // 执行到这里，说明程序正在迭代某个节点链表
        else {
            // 指向下一个节点
            iter->entry = iter->nextEntry;
        }

        // 当前节点存在,记录下一个节点
        if (iter->entry) {
            iter->nextEntry = iter->entry->next;
            // 返回当前节点
            return iter->entry;
        }
    }

    // 迭代完毕
    return NULL;
}

/**
 * 释放迭代器
 */
void dictReleaseIterator(dictIterator *iter)
{
    // 非初始化迭代器
    if (!(iter->index == -1 && iter->table == 0)){

        // 安全迭代器,更新安全迭代器计数器
        if (iter->safe)
            iter->d->iterators--;
        // 不安全迭代器,验证指纹是否相同
        else {
            // assert(iter->fingerprint == dictFingerprint(iter->d));
        }
    }

    // 释放迭代器
    zfree(iter);
}

/* ------------------------------- Debugging --------------------------------- */
#define DICT_STATS_VECTLEN 50

// 打印哈希表使用情况的统计数据
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

// 打印字典使用情况的统计数据
void dictPrintStats(dict *d) {
    _dictPrintStatsHt(&d->ht[0]);
    if (dictIsRehashing(d)) {
        printf("-- Rehashing into ht[1]:\n");
        _dictPrintStatsHt(&d->ht[1]);
    }
}

// 打印节点的键值对
void dictPrintEntry(dictEntry *he){

    keyObject *k = (keyObject*)he->key;
    keyObject *v = (keyObject*)he->v.val;
    printf("dictPrintEntry,k=%d,v=%d\n",k->val,v->val);
}

// 打印所有节点的键值对
void dictPrintAllEntry(dict *d)
{
    dictEntry *he;
    // 获取迭代器
    dictIterator *iter = dictGetIterator(d);

    // 迭代所有节点
    printf("dictPrintAllEntry , list : \n");
    while((he = dictNext(iter)) != NULL){
        dictPrintEntry(he);
    }

    // 释放迭代器
    dictReleaseIterator(iter);
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