#ifndef __ADLIST_H__
#define __ADLIST_H__

// 节点
typedef struct listNode {

    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

// 迭代器
typedef struct listIter {
    listNode *next;
    int direction;
} listIter;

// 链表结构
typedef struct list {

    listNode *head;
    listNode *tail;

    void *(*dup)(void *ptr);
    void (*free)(void *ptr);
    int (*match)(void *ptr,void *key);

    unsigned long len;

} list;



#define listLength(l) ((l)->len)

#define listSetMatchMethod(l,m) ((l)->match = (m))

// 从表头到表尾
#define AL_START_HEAD 0

// 从表尾到表头
#define AL_START_TAIL 1

#endif