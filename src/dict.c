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

#include "intdicttype.h"
#include "dict.h"

extern dictType intDictType;

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
    if (dictIsRehashing(d)) _dictRehashStep(d);

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
    dict* d = dictCreate(&intDictType, NULL);

    dictRelease(d);
}

void test_add_and_delete_key_value_pair(void)
{
    // // 创建新字典
    // dict *d = dictCreate(&intDictType, NULL);

    // // 创建键和值
    // KeyObject *k = create_key();
    // k->value = 1;
    // ValueObject *v = create_value();
    // v->value = 10086;

    // // 添加键值对
    // dictAdd(d, k, v);

    // assert(
    //     dictSize(d) == 1
    // );

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

// gcc -g zmalloc.c intdicttype.c dict.c
void main(void)
{

    test_empty_dict();

    // test_add_and_delete_key_value_pair();

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