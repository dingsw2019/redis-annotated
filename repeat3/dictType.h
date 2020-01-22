#ifndef __DICT_TYPE_H
#define __DICT_TYPE_H

#include <stdlib.h>

// 键
typedef struct objectKey
{
    int val;
} objectKey;

// 值
typedef struct objectVal
{
    int val;
} objectVal;

// 创建一个键
objectKey *keyCreate(int key);
// 创建一个值
objectVal *valCreate(int val);

// 销毁一个键
void keyRelease(objectKey *obj);

// 销毁一个值
void valRelease(objectVal *obj);


/**
 * dict type
 */
unsigned int keyHashIndex(const void *key);
int keyCompare(void *privdata,const void *key1,const void *key2);
void keyDestructor(void *privdata, void *key);
void valDestructor(void *privdata, void *val);

#endif