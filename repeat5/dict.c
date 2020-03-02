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




// gcc -g zmalloc.c dictType.c dict.c
void main(void)
{
    // int ret;
    // dictEntry *he;

    // // 创建一个空字典
    // dict *d = dictCreate(&initDictType, NULL);

    // // 节点的键值
    keyObject *k = keyCreate(1);
    valObject *v = valCreate(10086);

    printf("k=%d,v=%d\n",k->val,v->val);

    // // 添加节点
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
    // he = dictFind(d, k);
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
    
    // // 释放字典
    // dictRelease(d);

}