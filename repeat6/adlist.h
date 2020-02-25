#ifndef ADLIST_H
#define ADLIST_H

// 节点
typedef struct listNode
{
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

// 链表
typedef struct list 
{
    listNode *head;
    listNode *tail;
    void *(*dup)(void *ptr);
    void (*free)(void *ptr);
    int (*match)(void *ptr,void *key);
    unsigned long len;
} list;

// 迭代器
typedef struct listIter
{
    listNode *next;
    int direction;
} listIter;

// 链表长度
#define listLength(l) (l)->len

// 自定义链表匹配函数
#define listSetMatchMethod(l,m) ((l)->match = (m))

// 迭代方向
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif