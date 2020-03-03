#ifndef _DICT_TYPE_H
#define _DICT_TYPE_H

// 键
typedef struct keyObject
{
    int val;
} keyObject;

// 值
typedef struct valObject
{
    int val;
} valObject;


// 创建键
keyObject *keyCreate(int n);
// 创建值
valObject *valCreate(int n);
// 释放键
void keyDestructor(void *privdata,void *key);
// 释放值
void valDestructor(void *privdata,void *val);
// 计算哈希值
unsigned int keyHashIndex(const void *key);
// 对比键
int keyCompare(void *privdata,const void *key1,const void *key2);


#endif