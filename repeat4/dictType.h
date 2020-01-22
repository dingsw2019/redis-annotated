#include <stdlib.h>

typedef struct keyObject {
    int val;
} keyObject;

typedef struct valObject {
    int val;
} valObject;

// 创建一个键
keyObject *keyCreate(int index);
// 销毁一个键
void keyRelease(keyObject *key);

// 创建一个值
valObject *valCreate(int val);
// 销毁一个值
void valRelease(valObject *val);

// dict type 的函数

// 获取 key 的哈希值
unsigned int hashKeyIndex(const void *key);
// 对比两个键
int keyCompare(void *privdata,const void *key1,const void *key2);
// 销毁指定键
void keyDestructor(void *privdata,void *key);
// 销毁指定值
void valDestructor(void *privdata,void *val);