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
/* ------------------------------- */

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
    unsigned int i;

    // 遍历哈希表节点数组
    for(i=0;i<ht->size && ht->used>0; i++){

        dictEntry *he,*nextHe;
        // 回调函数只调用一次
        if (callback && (i & 65535)==0) callback(d->privdata);
        
        // 跳过空节点
        while((he = ht->table[i]) == NULL) continue;

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
    // dictAdd(d, k, v);

    // dictPrintStats(d);
    // printf("\ndictAdd, (k=1,v=10086) join dict,dict used size %d\n",dictSize(d));
    // printf("---------------------\n");

    // // 查找节点
    // he = dictFind(d, k);
    // if (he) {
    //     printf("dictFind,k is 1 to find entry\n");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictFind, not find\n");
    // }
    // printf("---------------------\n");

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