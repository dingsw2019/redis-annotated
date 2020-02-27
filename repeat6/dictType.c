#include <stdlib.h>
#include "zmalloc.h"
#include "dict.h"
#include "dictType.h"

// 创建键
keyObject *keyCreate(int val){
    keyObject *k = zmalloc(sizeof(*k));
    k->val = val;
    return k;
}

// 创建值
valObject *valCreate(int val){
    valObject *v = zmalloc(sizeof(*v));
    v->val = val;
    return v;
}

// 销毁键
void keyDestructor(void *privdata,void *key){
    DICT_NOTUSED(privdata);
    keyObject *k = (keyObject*)key;
    zfree(k);
}

// 销毁值
void valDestructor(void *privdata,void *val){
    DICT_NOTUSED(privdata);
    valObject *v = (valObject*)val;
    zfree(v);
}

// 对比键
int keyCompare(void *privdata,const void *key1,const void *key2){
    DICT_NOTUSED(privdata);
    keyObject *k1 = (keyObject*)key1;
    keyObject *k2 = (keyObject*)key2;
    return (k1->val == k2->val);
}

// 计算哈希值
unsigned int keyHashIndex(const void *key){
    keyObject *k = (keyObject*)key;
    int val = k->val;
    return (val<0) ? -val : val;
}

// 字典函数
dictType initDictType = {
    keyHashIndex,
    NULL,
    NULL,
    keyCompare,
    keyDestructor,
    valDestructor
};