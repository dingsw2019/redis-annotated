#include <stdlib.h>

// 字典哈希表节点的键
typedef struct keyObject {
    int val;
} keyObject;

// 字典哈希表节点的值
typedef struct valObject {
    int val;
} valObject;

/**
 * 私有函数
 */
// 创建一个键,并指定该键的哈希表索引值
keyObject *keyCreate(int index);
// 销毁指定键
void keyRelease(keyObject *k);
// 创建一个值,并制定该值的值
valObject *valCreate(int val);
// 销毁指定值
void valRelease(valObject *v);


/**
 * dict type 函数
 */
// 根据 key 获取哈希值的自定义函数
unsigned int keyHashIndex(const void *key);
// 比较两个节点键的自定义函数
int keyCompare(void *privdata,const void *key1,const void *key2);
// 销毁键的自定义函数
void keyDestructor(void *privdata,void *key);
// 销毁值的自定义函数
void valDestructor(void *privdata,void *val);
