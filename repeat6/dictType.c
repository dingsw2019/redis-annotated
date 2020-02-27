#include <stdlib.h>
#include "zmalloc.h"
#include "dict.h"
#include "dictType.h"

// 创建一个节点的键
keyObject *keyCreate(int index){

    keyObject *k = zmalloc(sizeof(*k));
    k->val = index;
    return k;
}

// 创建一个节点的值
valObject *valCreate(int index){

    valObject *v = zmalloc(sizeof(*v));
    v->val = index;
    return v;
}

// 计算哈希值
unsigned int keyHashIndex(const void *key){
    keyObject *k = (keyObject*)key;
    int val = k->val;
    return (val < 0) ? 0-val : val;
}

// 对比两个键
int keyCompare(void *privdata,const void *key1,const void *key2){
    DICT_NOTUSED(privdata);
    keyObject *k1 = (keyObject*)key1;
    keyObject *k2 = (keyObject*)key2;
    return (k1->val == k2->val);
}

// 销毁节点的键
void keyDestructor(void *privdata,void *key){
    DICT_NOTUSED(privdata);
    keyObject *k = (keyObject*)key;
    zfree(k);
}

// 销毁节点的值
void valDestructor(void *privdata,void *val){
    DICT_NOTUSED(privdata);
    valObject *v = (valObject*)val;
    zfree(v);
}

dictType initDictType = {
    keyHashIndex,
    NULL,
    NULL,
    keyCompare,
    keyDestructor,
    valDestructor
};