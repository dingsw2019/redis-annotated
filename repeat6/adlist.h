#ifndef AD_LIST_H
#define AD_LIST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zmalloc.h"

// 双端链表节点
typedef struct listNode 
{
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

// 双端链表
typedef struct list 
{
    listNode *head;
    listNode *tail;
    void *(*dup)(void *val);
    void (*free)(void *val);
    // void (*match)(void *v1,void *v2);
    int (*match)(void *v1,void *v2);

    unsigned long len;// myerr 缺少
} list;

// 迭代器
typedef struct listIter
{
    listNode *next;
    int direction;
} listIter;

// 链表长度
// #define listLength(l) (l)->len myerr
#define listLength(l) ((l)->len)

// 设置链表对比函数
// #define listSetMatch(l,m) (l)->match = (m) myerr
#define listSetMatchMethod(l,m) ((l)->match = (m))

// 迭代方向
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif