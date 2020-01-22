#include <stdlib.h>

#include "zmalloc.h"
#include "dict.h"
#include "dictType.h"

// 创建一个键
objectKey *keyCreate(int index){
    
    objectKey *k = zmalloc(sizeof(*k));
    k->val = index;
    return k;
}
// 创建一个值
objectVal *valCreate(int val){
    objectVal *v = zmalloc(sizeof(*v));
    v->val = val;
    return v;
}

// 销毁一个键
void keyRelease(objectKey *obj){
    zfree(obj);
}

// 销毁一个值
void valRelease(objectVal *obj){
    zfree(obj);
}

// 获取 key 的哈希值
unsigned int keyHashIndex(const void *key){

    objectKey *k = (objectKey*)key;
    int val = k->val;

    if (val < 0)
        return 0 - val;
    else 
        return val;
}

// 对比两个节点键的自定义函数
int keyCompare(void *privdata,const void *key1,const void *key2){

    DICT_NOTUSED(privdata);

    objectKey *k1 = (objectKey*)key1;
    objectKey *k2 = (objectKey*)key2;

    return (k1->val == k2->val);
}

// 销毁键的自定义函数
void keyDestructor(void *privdata, void *key){
    DICT_NOTUSED(privdata);
    keyRelease(key);
}

// 销毁值的自定义函数
void valDestructor(void *privdata, void *val){
    DICT_NOTUSED(privdata);
    valRelease(val);
}

// 字典的自定义函数
dictType initDictType = {
    keyHashIndex,
    NULL,
    NULL,
    keyCompare,
    keyDestructor,
    valDestructor
};