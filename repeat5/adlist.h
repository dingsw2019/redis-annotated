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
// 链表匹配函数
#define listSetMatchMethod(l,m) ((l)->match = (m))
// 返回复制函数
#define listGetDupMethod(l) ((l)->dup)
// 返回释放函数
#define listGetFreeMethod(l) ((l)->free)
// 返回比对函数
#define listGetMatchMethod(l) ((l)->match)

// 迭代方向
#define AL_START_HEAD 0

#define AL_START_TAIL 1

list *listCreate(void);
void listRelease(list *list);
list *listAddNodeHead(list *list,void *value);
list *listAddNodeTail(list *list,void *value);
list *listInsertNode(list *list,listNode *old_node,void *value,int after);
void listDelNode(list *list,listNode *node);
listIter *listGetIterator(list *list,int direction);
listNode *listNext(listIter *iter);
void listReleaseIterator(listIter *iter);
list *listDup(list *orig);
listNode *listSearchKey(list *list,void *key);
listNode *listIndex(list *list,long index);
void listRewind(list *list,listIter *li);
void listRewindTail(list *list,listIter *li);
void listRotate(list *list);

#endif