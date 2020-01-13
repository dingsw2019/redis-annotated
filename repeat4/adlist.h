#ifndef _ADLIST_H__
#define _ADLIST_H__

// 节点
typedef struct listNode {
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

// 链表
typedef struct list {
    listNode *head;
    listNode *tail;

    void *(*dup)(void *ptr);
    void (*free)(void *ptr);
    int (*match)(void *ptr,void *key);

    unsigned long len;
} list;

// 迭代器
typedef struct listIter {
    listNode *next;
    int direction;
} listIter;

#define listLength(l) ((l)->len)

#define listSetMatchMethod(l,m) ((l)->match = (m))

#define AL_START_HEAD 0

#define AL_START_TAIL 1 

#endif