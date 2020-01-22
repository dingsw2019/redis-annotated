#include <stdlib.h>
#include "zmalloc.h"
#include "dict.h"
#include "dictType.h"

// 创建一个键
keyObject *keyCreate(int index){
    keyObject *k = zmalloc(sizeof(*k));
    k->val = index;
    return k;
}
// 销毁一个键
void keyRelease(keyObject *key){
    zfree(key);
}

// 创建一个值
valObject *valCreate(int val){
    valObject *v = zmalloc(sizeof(*v));
    v->val = val;
    return v;
}
// 销毁一个值
void valRelease(valObject *val){
    zfree(val);
}

// dict type 的函数

// 获取 key 的哈希值
unsigned int hashKeyIndex(const void *key){
    keyObject *k = (keyObject*)key;
    int val = k->val;
    return (val < 0) ? (0-val) : val;
}
// 对比两个键
int keyCompare(void *privdata,const void *key1,const void *key2){
    DICT_NOTUSED(privdata);

    keyObject *k1 = (keyObject*)key1;
    keyObject *k2 = (keyObject*)key2;

    return (k1->val == k2->val);
}
// 销毁指定键
void keyDestructor(void *privdata,void *key){
    keyObject *k = (keyObject*)key;
    keyRelease(k);
}
// 销毁指定值
void valDestructor(void *privdata,void *val){
    valObject *v = (valObject*)val;
    valRelease(v);
}

dictType initDictType = {
    hashKeyIndex,
    NULL,
    NULL,
    keyCompare,
    keyDestructor,
    valDestructor
};