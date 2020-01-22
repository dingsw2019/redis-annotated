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

// int main(int argc, char **argv) {

    // int ret;
    // sds key = sdsnew("key");
    // sds val = sdsnew("val");
    // dict *dd = dictCreate(&keyptrDictType, NULL);

    // printf("Add elements to dict\n");
    // for (int i = 0; i < 6 ; ++i) {
    //     ret = dictAdd(dd, sdscatprintf(key, "%d", i), sdscatprintf(val, "%d", i));
    //     printf("Add ret%d is :%d ,", i, ret);
    //     printf("ht[0].used :%lu, ht[0].size :%lu, "
    //                    "ht[1].used :%lu, ht[1].size :%lu\n", dd->ht[0].used, dd->ht[0].size, dd->ht[1].used, dd->ht[1].size);
    // }

    // printf("\nDel elements to dict\n");
    // for (int i = 0; i < 6 ; ++i) {
    //     ret = dictDelete(dd, sdscatprintf(key, "%d", i));
    //     printf("Del ret%d is :%d ,", i, ret);
    //     printf("ht[0].used :%lu, ht[0].size :%lu, "
    //                    "ht[1].used :%lu, ht[1].size :%lu\n", dd->ht[0].used, dd->ht[0].size, dd->ht[1].used, dd->ht[1].size);
    // }

    // sdsfree(key);
    // sdsfree(val);
    // dictRelease(dd);

    // return 0;
// }