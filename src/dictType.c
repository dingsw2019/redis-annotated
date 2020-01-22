#include <stdlib.h>
#include "zmalloc.h"
#include "dict.h"
#include "dictType.h"

// 创建一个键,并指定该键的哈希表索引值
keyObject *keyCreate(int index){
    keyObject *k = zmalloc(sizeof(*k));
    k->val = index;
    return k;
}

// 销毁指定键
void keyRelease(keyObject *k){
    zfree(k);
}

// 创建一个值,并制定该值的值
valObject *valCreate(int val){
    valObject *v = zmalloc(sizeof(*v));
    v->val = val;
    return v;
}

// 销毁指定值
void valRelease(valObject *v){
    zfree(v);
}

// 根据 key 获取哈希值的自定义函数
unsigned int keyHashIndex(const void *key){
    keyObject *k = (keyObject*)key;
    int val = k->val;
    
    return (val < 0) ? 0-val : val;
}

// 比较两个节点键的自定义函数
int keyCompare(void *privdata,const void *key1,const void *key2){
    DICT_NOTUSED(privdata);
    keyObject *k1 = (keyObject*)key1;
    keyObject *k2 = (keyObject*)key2;

    return (k1->val == k2->val);
}

// 销毁键的自定义函数
void keyDestructor(void *privdata,void *key){
    DICT_NOTUSED(privdata);
    keyObject *k = (keyObject*)key;
    keyRelease(k);
}

// 销毁值的自定义函数
void valDestructor(void *privdata,void *val){
    DICT_NOTUSED(privdata);
    valObject *v = (valObject*)val;
    valRelease(v);
}

// 初始化字典自定义函数
dictType initDictType = {
    keyHashIndex,
    NULL,
    NULL,
    keyCompare,
    keyDestructor,
    valDestructor
};