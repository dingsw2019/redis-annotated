#ifndef AD_LIST_H
#define AD_LIST_H

// 节点
typedef struct listNode {

    // 前置节点
    struct listNode *prev;
    // 后置节点
    struct listNode *next;
    // 节点值
    void *value;
} listNode;

// 链表
typedef struct list {
    // 头节点
    listNode *head;
    // 尾节点
    listNode *tail;
    // 复制
    void *(*dup)(void *p);
    // 释放
    void (*free)(void *p);
    // 比对
    int (*match)(void *p1, void *p2);
    // 节点计数器
    unsigned long len;
} list;

// 迭代器
typedef struct listIter {
    // 方向
    int direction;
    // 节点
    listNode *next;
} listIter;

#define AD_START_HEAD 0
#define AD_START_TAIL 1

// 节点数
#define listLength(l) ((l)->len)

// 设置比对函数
#define listSetMatchMethod(l,m) ((l)->match = (m))

list *listCreate(void);
list *listAddNodeHead(list *list,void *value);
list *listAddNodeTail(list *list, void *value);
list *listInsertNode(list *list,listNode *old_node,void *value,int after);
void listRelease(list *list);
listNode *listSearchKey(list *list, void *val);
listNode *listIndex(list *list,long index);
void listDelNode(list *list,listNode *node);
void listRotate(list *list);
void listRewindTail(list *list, listIter *iter);
list *listDup(list *orig);
listIter *listGetIterator(list *list,int direction);
listNode *listNext(listIter *iter);
void listReleaseIterator(listIter *iter);

#endif