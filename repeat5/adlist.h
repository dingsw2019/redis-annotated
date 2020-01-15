#ifndef ADLIST_H__
#define ADLIST_H__

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

list *listCreate(void);
void listRelease(list *list);
list *listAddNodeHead(list *list,void *value);
list *listAddNodeTail(list *list,void *value);
listIter *listGetIterator(list *list,int direction);
listNode *listNext(listIter *iter);
int matchKey(void *str1,void *str2);
void printList(list *list);
list *listDup(list *orig);
void listReleaseIterator(listIter *iter);

// 链表长度
#define listLength(l) ((l)->len)

// 链表匹配函数
#define listSetMatchMethod(l,m) ((l)->match = (m))

// 迭代方向
#define AL_START_HEAD 0

#define AL_START_TAIL 1

#endif