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

extern dictType initDictType;

/* ------------------- private -------------------- */
// 是否开启强制扩容
static int dict_can_resize = 1;
// 强制扩容的使用率
static unsigned int dict_force_resize_ratio = 5;

// 初始化或重置哈希表属性
static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}


/* ------------------- API -------------------- */

// 初始化字典属性
int _dictInit(dict *d,dictType *type,void *privDataPtr)
{
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
dict *dictCreate(dictType *type,void *privDataPtr)
{
    // 申请内存空间
    dict *d = zmalloc(sizeof(*d));
    // 初始化属性
    _dictInit(d,type,privDataPtr);
    return d;
}

// 释放哈希表所有节点,并重置哈希表
// 成功返回 DICT_OK
int _dictClear(dict *d, dictht *ht,void (callback)(void *))
{
    unsigned long i;

    for(i=0; i<ht->size && ht->used>0; i++){
        dictEntry *he,*nextHe;

        // 调用一次回调函数
        if (callback && (i & 65535)==0) callback(d->privdata);

        // 跳过空节点数组
        if((he = ht->table[i]) == NULL) continue;

        // 遍历节点链表
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

    // 释放哈希表数组
    zfree(ht->table);

    // 重置哈希表属性
    _dictReset(ht);

    return DICT_OK;
}

// 释放字典
void dictRelease(dict *d)
{
    // 重置 0 号哈希表
    _dictClear(d,&d->ht[0],NULL);
    // 重置 1 号哈希表
    _dictClear(d,&d->ht[1],NULL);

    // 释放字典
    zfree(d);
}

// 字典扩容大小策略
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

// 字典扩容执行
// 成功返回 DICT_OK
int dictExpand(dict *d,unsigned long size)
{
    dictht n;

    // 计算扩容大小
    unsigned long realsize = _dictNextPower(size);

    // rehash 状态不扩容
    if (dictIsRehashing(d)) return DICT_ERR;

    // 设置哈希表属性
    n.table = zcalloc(realsize*sizeof(dictEntry*));;
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.used = 0;

    // 0 号哈希表为空,将新哈希表赋给 0 号哈希表
    if (d->ht[0].size == 0) {
        d->ht[0] = n;
        return DICT_OK;
    }

    // 否则赋值给 1 号哈希表, 并开启 rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 检查字典是否需要扩容,字典扩容控制策略
// 执行扩容返回 DICT_OK
static int _dictExpandIfNeeded(dict *d)
{
    // rehash 状态,不扩容
    if (dictIsRehashing(d)) return DICT_OK;
    
    // 初始化扩容
    if (d->ht[0].size == 0) return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 已用空间严重不足,进行2倍扩容
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize || 
         (d->ht[0].used/d->ht[0].size)>dict_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;
}

// 获取键的哈希值
// 如果键已经存在字典中,返回 -1
// 否则返回 哈希值
static int _dictKeyIndex(dict *d,const void *key)
{
    unsigned long h,idx,table;
    dictEntry *he;

    // 扩容策略
    if(_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 计算哈希值
    h = dictHashKey(d,key);
        
    // 从主副哈希表搜索
    for(table=0; table<=1; table++){

        // 计算索引
        idx = h & d->ht[table].sizemask;

        // 获取链表首节点
        he = d->ht[table].table[idx];

        // 遍历链表,查找 key
        while(he){
            // 找到相同 key , 返回 -1
            if (dictCompareKey(d,he->key,key))
                return -1;
            he = he->next;
        }

        // 非 rehash 状态, 不用搜索 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 返回哈希值
    return idx;
}

// 添加节点,并填充键
// 如果键已存在字典中,添加失败返回 NULL,否则返回节点
dictEntry *dictAddRaw(dict *d,void *key)
{
    unsigned long index;
    dictEntry *entry;
    dictht *ht;

    // 计算哈希值
    // 如果哈希值为 -1 ,代表key已存在字典中,返回 NULL
    if((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 确认哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    // 申请节点内存
    entry = zmalloc(sizeof(*entry));

    // 头插法, 添加节点到链表
    entry->next = ht->table[index];
    ht->table[index] = entry;

    // 设置节点键
    dictSetKey(d,entry,key);

    // 更新哈希表已用节点数
    ht->used++;

    // 返回节点
    return entry;
}

// 添加节点
// 添加成功返回  DICT_OK,失败返回 DICT_ERR
int dictAdd(dict *d,void *key,void *val)
{
    // 尝试添加节点
    dictEntry *entry = dictAddRaw(d,key);

    // 如果key已存在,返回NULL
    if (!entry) return DICT_ERR;
    
    // 添加节点值
    dictSetVal(d,entry,val);

    return DICT_OK;
}

// 查找节点
// 找到返回 节点，否则返回 NULL
dictEntry *dictFind(dict *d,const void *key)
{
    unsigned long h,idx,table;
    dictEntry *he;

    // 空字典,不查找
    if (d->ht[0].size == 0) return NULL;
    
    // 尝试单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算哈希值
    h = dictHashKey(d,key);

    // 遍历主副哈希表
    for(table=0; table<=1; table++){
        // 计算哈希值在哈希表的索引值
        idx = h & d->ht[table].sizemask;
        // 确定节点
        he = d->ht[table].table[idx];
        // 遍历节点链表
        while(he){
            // 找到相同 key 返回
            if (dictCompareKey(d,he->key,key))
                return he;
            // 处理下一个节点
            he = he->next;
        }
        // 非 rehash 状态,不需要遍历 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 未找到节点
    return NULL;
}

int customRandom(){
    srand(time(NULL));
    return rand();
}

// 随机返回一个节点
// 成功返回节点, 失败返回 NULL
dictEntry *dictGetRandomKey(dict *d)
{
    unsigned long h;
    dictEntry *he,*origHe;
    int listlen,listele;

    // 空字典,无法随机
    if (d->ht[0].size == 0) return NULL;

    // 单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 非 rehash 状态 , 从 0 号哈希表获取
    if (!dictIsRehashing(d)) {
        do {
            // 随机索引值
            h = customRandom() & d->ht[0].sizemask;
            // 确定节点链表的首地址
            he = d->ht[0].table[h];
        }while(he == NULL);
    } 
    // rehash 状态, 从 0 号 和 1 号哈希表获取
    else {
        do {
            // 随机索引值
            h = customRandom() % (d->ht[0].size+d->ht[1].size);
            // 确定节点链表的首地址
            he = (h >= d->ht[0].size) ? d->ht[1].table[h-d->ht[0].size] :
                                        d->ht[0].table[h];
        }while(he == NULL);
    }

    // 计算链表长度
    listlen = 0;
    origHe = he;
    while(he){
        listlen++;
        he = he->next;
    }

    // 计算链表的随机索引
    listele = customRandom() % listlen;

    // 根据随机索引获取节点
    he = origHe;
    while(listele--) he = he->next;

    // 返回节点
    return he;
}

// 替换或添加节点
// 如果键存在,替换值,返回 0
// 如果键不存在,新增节点, 返回 1
int dictReplace(dict *d,void *key,void *val)
{
    dictEntry *entry,auxentry;
    // 尝试新增节点,如果成功返回 1
    if(dictAdd(d,key,val) == DICT_OK)
        return 1;

    // 新增失败,说明键存在,查找节点
    entry = dictFind(d,key);

    // 保存旧值的指针
    auxentry = *entry;

    // 更新节点值
    dictSetVal(d,entry,val);

    // 释放节点的旧值
    dictFreeVal(d,&auxentry);

    return 0;
}

// 查找或添加节点(只设置键)
// 如果键存在字典中,返回节点
// 如果键不存在字典中,添加后返回节点
dictEntry *dictReplaceRaw(dict *d,void *key)
{   
    dictEntry * entry = dictFind(d,key);
    
    return (entry) ? entry : dictAddRaw(d,key);
}

// 删除节点
// 不释放节点(nofree=1) , 释放节点
// 删除成功返回 DICT_OK, 未找到返回 DICT_ERR
int dictGenericDelete(dict *d,const void *key,int nofree)
{
    unsigned long h,idx,table;
    dictEntry *he,*prevHe;

    // 空数组不处理
    if (d->ht[0].size == 0) return DICT_ERR;

    // 计算哈希值
    h = dictHashKey(d,key);

    // 遍历主副哈希表
    for(table=0; table<=1; table++){
        // 获取索引值
        idx = h & d->ht[table].sizemask;

        // 确定节点链表的首地址
        he = d->ht[table].table[idx];
        prevHe = NULL;
        // 遍历链表
        while(he){

            // 找到相同 key
            if (dictCompareKey(d,he->key,key)) {


                // 前一个节点的指针指向下一个节点
                if (prevHe == NULL) {
                    d->ht[table].table[idx] = he->next;
                } else {
                    prevHe->next = he->next;
                }

                if (!nofree) {
                    // 释放值
                    dictFreeVal(d,he);
                    // 释放键
                    dictFreeKey(d,he);
                }
                // 释放节点
                zfree(he);
                // 更新已用节点数
                d->ht[table].used--;

                return DICT_OK;
            }

            // 记录上一个节点
            prevHe = he;

            // 处理下一个节点
            he = he->next;
        }
        
        // 非 rehash 状态, 不需要遍历 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    return DICT_ERR;
}

// 删除节点,并释放节点
// 删除成功返回 1, 未找到返回 0
int dictDelete(dict *d,const void *key)
{
    return dictGenericDelete(d,key,0);
}

// 删除节点, 不释放节点
int dictDeleteNoFree(dict *d,const void *key)
{
    return dictGenericDelete(d,key,1);
}

// 创建一个不安全迭代器
dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));
    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

// 创建一个安全迭代器
dictIterator *dictGetSafeIterator(dict *d)
{
    dictIterator *iter = dictGetIterator(d);
    iter->safe = 1;
    return iter;
}

// 将节点指针移动到下一个节点
// 成功返回节点, 未找到或迭代完成返回 NULL
dictEntry *dictNext(dictIterator *iter)
{
    // 两种情况进入这里
    // 1. 迭代器初始化
    // 2. 遍历完一个节点链表
    while(1){    
        if (iter->entry == NULL) {
            dictht *ht = &iter->d->ht[iter->table];
            // 初次迭代运行
            if (iter->table == 0 && iter->index == -1) {
                // 安全迭代器
                if (iter->safe) {
                // 更新字典的安全迭代器计数器
                iter->d->iterators++;
                } 
                // 不安全迭代器
                else {
                    // 记录指纹
                    // iter->fingerprint = fingerprint(d);
                }
            }
            // 更新迭代器索引值
            iter->index++;

            // 如果索引值越界,说明 0 号哈希表遍历完,开始遍历 1 号哈希表
            if (iter->index >= ht->size) {
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    // table , index 更新
                    iter->table++;
                    iter->index = 0;
                    // 哈希表 更新
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }

            // 确认节点
            iter->entry = ht->table[iter->index];
        }
        // 走到这里说明, 要开始迭代节点链表了
        else {
            // 确认节点
            iter->entry = iter->nextEntry;
        }

        // 如果节点链表存在,赋值下一个节点,并返回当前节点
        if (iter->entry) {
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }

    // 迭代完成
    return NULL;
}

// 释放迭代器
void dictReleaseIterator(dictIterator *iter)
{
    // 如果是运行过的迭代器
    if (!(iter->table == 0 && iter->index == -1)) {
        // 安全迭代器
        if (iter->safe) {
            // 更新字典的安全迭代器计数器
            iter->d->rehashidx--;
        } 
        // 不安全迭代器
        else {
            // 验证指纹
            // assert(iter->fingerprint == fingerPrint(d));
        }
    }

    // 释放迭代器
    zfree(iter);
}

// N步渐进式 rehash
// 返回 1 , 表示还存在需要移动的节点
// 返回 0 , 表示 rehash 完毕
int dictRehash(dict *d,int n)
{   
    if (!dictIsRehashing(d)) return 0;
    
    // 执行 n 次
    while(n--){

        dictEntry *he;

        // 0 号哈希表空, 说明 rehash 完成
        if (d->ht[0].used == 0) {
            // 释放 0 号哈希表内存
            zfree(d->ht[0].table);
            // 1 号哈希表赋值给 0 号哈希表
            d->ht[0] = d->ht[1];
            // 重置 1 号哈希表
            _dictReset(&d->ht[1]);
            // 关闭 rehash 标识
            d->rehashidx = -1;
            // 返回
            return 0;
        }

        // 确保 rehash 没有越界
        assert(d->ht[0].size > (unsigned)d->rehashidx);

        // 获取非空节点链表首地址
        while(d->ht[0].table[d->rehashidx] == NULL)
            d->rehashidx++;

        he = d->ht[0].table[d->rehashidx];
        // 遍历节点链表
        while(he){
            unsigned long idx;  
            // 计算键在 1 号哈希表的索引值
            idx = dictHashKey(d,he->key) & d->ht[1].sizemask;
            // 键值对赋给 1 号哈希表
            he->next = d->ht[1].table[idx];
            d->ht[1].table[idx] = he;
            // 更新哈希表已用节点数
            d->ht[0].used--;
            d->ht[1].used++;

            // 处理下一个节点
            he = he->next;
        }
        // 节点链表置空
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }

    return 1;
}

#ifdef DICT_TEST_MAIN
/* ------------------- debug -------------------- */
void dictPrintEntry(dictEntry *he){
    keyObject *k = (keyObject*)he->key;
    valObject *v = (valObject*)he->v.val;

    printf("dictPrintEntry, k=%d,v=%d\n",k->val,v->val);
}

void dictPrintAllEntry(dict *d) {
    
    dictEntry *he;

    // 获取迭代器
    dictIterator *iter = dictGetIterator(d);
    
    // 打印所有节点
    while((he = dictNext(iter)) != NULL)
        dictPrintEntry(he);

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
    he = dictFind(d, k2);
    if (he) {
        printf("dictAdd through dictReplace, add entry(k=2,value=10000) join dict\n");
        dictPrintEntry(he);
    } else {
        printf("dictAdd through dictReplace, not find entry\n");
    }
    // dictPrintStats(d);
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
    
    // // 释放字典
    dictRelease(d);

}
#endif