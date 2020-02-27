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
keyObject *keyCreate(int val);
// 创建值
valObject *valCreate(int val);
// 销毁键
void keyDestructor(void *privdata,void *key);
// 销毁值
void valDestructor(void *privdata,void *val);
// 对比键
int keyCompare(void *privdata,const void *key1,const void *key2);
// 计算哈希值
unsigned int keyHashIndex(const void *key);
#endif