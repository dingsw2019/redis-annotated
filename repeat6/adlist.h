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

#define listLength(l) ((l)->len)
// 表头节点
#define listFirst(l) ((l)->head)
// 表尾节点
#define listLast(l) ((l)->tail)
// 前置节点
#define listPrevNode(n) ((n)->prev)
// 后置节点
#define listNextNode(n) ((n)->next)
// 节点值
#define listNodeValue(n) ((n)->value)

// 设置复制函数
#define listSetDupMethod(l,m) ((l)->dup = (m))

// 将链表 l 的值释放函数设置为 m
#define listSetFreeMethod(l,m) ((l)->free = (m))

// 将链表的对比函数设置为 m
// T = O(1)
#define listSetMatchMethod(l,m) ((l)->match = (m))

// 返回复制函数
#define listGetDupMethod(l) ((l)->dup)
// 返回释放函数
#define listGetFreeMethod(l) ((l)->free)
// 返回比对函数
#define listGetMatchMethod(l) ((l)->match)

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