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

// 清空哈希表
int _dictClear(dict *d, dictht *ht, void (callback)(void *)) {

    unsigned long i;
    dictEntry *he, *nextHe;
    
    // 遍历删除节点
    for (i=0; ht->used >0 && i<ht->size; i++) {

        // 回调函数, 只执行一次
        if (callback && (i & 65525) == 0) callback(d->privdata);
        
        // 跳过空节点
        if ((he = ht->table[i]) == NULL) continue;

        // 节点链表
        while (he) {

            nextHe = he->next;

            dictFreeVal(d,he);
            dictFreeKey(d,he);
            zfree(he);

            ht->used--;

            he = nextHe;
        }
    }

    zfree(ht->table);

    _dictReset(ht);

    return DICT_OK;
}

// 释放字典
void dictRelease(dict *d) {

    // 清空哈希表
    _dictClear(d, &d->ht[0], NULL);
    _dictClear(d, &d->ht[1], NULL);

    // 释放字典
    zfree(d);
}

// n 步 rehash
// 还有可移动的节点, 返回 1
// 否则返回 0
int dictRehash(dict *d, int n) {

    // 非 rehash 状态, 不处理
    if (!dictIsRehashing(d)) return 0;

    
    while (n--) {
        dictEntry *he, *nextHe;
        // rehash 完成
        if (d->ht[0].used == 0) {
            zfree(d->ht[0].table);
            d->ht[0] = d->ht[1];
            _dictReset(&d->ht[1]);
            d->rehashidx = -1;
            return 0;
        }

        // 索引是否越界
        assert(d->ht[0].size > (unsigned)d->rehashidx);

        // 跳过空节点
        while(d->ht[0].table[d->rehashidx] == NULL)
            d->rehashidx++;

        he = d->ht[0].table[d->rehashidx];
        // 节点链表, 移动到 1 号哈希表
        while (he) {
            unsigned long idx;

            nextHe = he->next;

            // 计算索引值
            idx = dictHashKey(d, he->key) & d->ht[1].sizemask;

            // 添加到新哈希表
            he->next = d->ht[1].table[idx];
            d->ht[1].table[idx] = he;

            // 更新节点数
            d->ht[0].used--;
            d->ht[1].used++;

            // 处理下一个节点
            he = nextHe;
        }

        // 删除 0 号哈希表节点指针
        d->ht[0].table[d->rehashidx] = NULL;
        // 更新 rehashidx
        d->rehashidx++;
    }

    return 1;
}

// 单步 rehash
static void _dictRehashStep(dict *d) {
    if (d->iterators == 0) dictRehash(d, 1);
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

    // 单步 rehash
    if (dictIsRehashing(d)) _dictRehashStep(d);

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

// 查找节点
dictEntry *dictFind(dict *d, void *key) {

    unsigned int h, idx, table;
    dictEntry *he;

    // 空字典
    if (d->ht[0].size == 0) return NULL;

    // 单步 rehash
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算 key 的哈希值
    h = dictHashKey(d, key);

    for (table=0; table<=1; table++) {

        // 计算索引值
        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];
        // 节点链表
        while (he) {

            // 找到目标节点, 返回
            if (dictCompareKey(d, he->key, key)) {
                return he;
            }
            // 处理下一个节点
            he = he->next;
        }

        // 非 rehash 状态, 不遍历 1号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 未找到
    return NULL;
}

// 随机返回节点
dictEntry *dictGetRandomKey(dict *d) {

    dictEntry *origHe,*he;
    unsigned int idx;
    int listlen, listele;

    // 空字典不进行处理
    if (d->ht[0].size == 0) return NULL;

    // 单步 rehash
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 随机选择节点链表
    if (dictIsRehashing(d)) {
        do {
            idx = (rand() % (d->ht[0].size+d->ht[1].size));
            he = (idx >= d->ht[0].size) ?  d->ht[1].table[idx-d->ht[0].size] : d->ht[0].table[idx];
        } while(he == NULL);
    } else {
        do {
            idx = rand() & d->ht[0].sizemask;
            he = d->ht[0].table[idx];
        } while(he == NULL);
    }

    // 计算节点数量
    origHe = he;
    listlen = 0;
    while (he) {
        listlen++;
        he = he->next;
    }

    // 随机选择节点
    listele = rand() % listlen;
    he = origHe;
    while (listele--) he = he->next;
    
    return he;
}

// 随机获取多个节点
// int dictGetRandomKeys(dict *d, dictEntry **des, int count) {
// }

// key不存在, 新增节点, 返回 1
// key  存在, 修改节点值, 返回 0
int dictReplace(dict *d, void *key, void *val) {

    dictEntry *entry, auxentry;

    // 尝试新增节点
    if (dictAdd(d, key, val) == DICT_OK)
        return 1;

    // 新增失败, 保存原节点值
    entry = dictFind(d, key);
    auxentry = *entry;
    // 设置新节点值
    dictSetVal(d, entry, val);
    // 释放原节点值
    dictFreeVal(d, &auxentry);

    return 0;
}

// 新增或查找节点
// 如果是新增, 返回写入键的节点
dictEntry *dictReplaceRaw(dict *d, void *key) {

    // 查找节点
    dictEntry *entry = dictFind(d, key);
    // 找到节点返回, 否则添加
    return (entry) ? entry : dictAddRaw(d, key);
}

// 获取一个非安全迭代器
dictIterator *dictGetIterator(dict *d) {

    dictIterator *iter = zmalloc(sizeof(*iter));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

// 获取一个安全迭代器
dictIterator *dictGetSafeIterator(dict *d) {

    dictIterator *iter = dictGetIterator(d);
    iter->safe = 1;
    return iter;
}

// 移动一个节点
dictEntry *dictNext(dictIterator *iter) {

    while (1) {

        // 移动节点链表
        if (iter->entry == NULL) {
            dictht *ht = &iter->d->ht[iter->table];

            // 首次执行, 初始化迭代器属性
            if (iter->table == 0 && iter->index == -1) {
                if (iter->safe) {
                    iter->d->iterators++;
                } else {
                    // iter->fingerPrint = 
                }
            }

            // 更新索引
            iter->index++;

            // 是否迭代 1 号哈希表
            if (iter->index >= (signed)ht->size) {
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }

            // 指向下一个节点链表
            iter->entry = ht->table[iter->index];

        // 移动到写一个节点
        } else {
            iter->entry = iter->nextEntry;
        }

        // 记录下一个节点
        if (iter->entry) {
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }

    return NULL;
}

// 销毁迭代器
void   dictReleaseIterator(dictIterator *iter) {

    if (!(iter->index==-1 && iter->table ==0)) {
        if (iter->safe) {
            iter->d->iterators--;
        } else {
            // iter->fingerPrint = 
        }
    }

    zfree(iter);
}

// 标准删除方法
// nofree =1 表示不释放节点的键值, 否则释放
// 删除成功返回 DICT_OK , 否则返回 DICT_ERR
int dictGenericDelete(dict *d, const void *key, int nofree) {

    unsigned int h, idx, table;
    dictEntry *prevHe, *he;

    // 空字典, 不处理
    if (d->ht[0].size == 0) return DICT_ERR;
    
    // 单步 rehash
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算 key 的哈希值
    h = dictHashKey(d, key);

    // 遍历哈希表
    for (table=0; table <= 1; table++) {
        // 计算索引值
        idx = h & d->ht[table].sizemask;

        // 前置节点初始化
        prevHe = NULL;
        he = d->ht[table].table[idx];
        // 遍历节点链表
        while (he) {

            // 找到目标节点
            if (dictCompareKey(d,he->key,key)) {

                // 处理目标节点的相邻节点的指针
                if (prevHe) {
                    prevHe->next = he->next;
                } else {
                    d->ht[table].table[idx] = he->next;
                }
                // 释放节点键值
                if (!nofree) {
                    dictFreeVal(d, he);
                    dictFreeKey(d, he);
                }
                // 释放节点
                zfree(he);
                // 更新节点计数器
                d->ht[table].used--;

                return DICT_OK;
            }
            prevHe = he;
            // 下一个节点
            he = he->next;
        }

        if (!dictIsRehashing(d)) break;
    }

    // 未找到节点
    return DICT_ERR;
}

int dictDelete(dict *d, void *key) {
    return dictGenericDelete(d, key, 0);
}

int dictDeleteNoFree(dict *d, void *key) {
    return dictGenericDelete(d, key, 1);
}

/*--------------------------- debug -------------------------*/
void dictPrintEntry(dictEntry *he) {

    keyObject *k = (keyObject*)he->key;
    valObject *v = (valObject*)he->v.val;
    printf("dictPrintEntry,k=%d,v=%d\n",k->val,v->val);
}

void dictPrintAllEntry(dict *d) {

    dictIterator *iter = dictGetSafeIterator(d);
    dictEntry *he;

    printf("dictPrintAllEntry , list : \n");
    while ((he = dictNext(iter)) != NULL) {
        dictPrintEntry(he);
    }
    dictReleaseIterator(iter);
}

// gcc -g zmalloc.c dictType.c dict.c
void main(void)
{
    int ret;
    dictEntry *he;

    srand(time(NULL));

    // 创建一个空字典
    dict *d = dictCreate(&initDictType, NULL);

    // 节点的键值
    keyObject *k = keyCreate(1);
    valObject *v = valCreate(10086);

    // 添加节点
    dictAdd(d, k, v);

    // // dictPrintStats(d);
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
    
    // 释放字典
    dictRelease(d);
}