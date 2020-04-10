#ifndef __ADLIST_H__
#define __ADLIST_H__

/**
 * 双端链表节点
 */
typedef struct listNode {

    // 前置节点
    struct listNode *prev;

    // 后置节点
    struct listNode *next;

    // 节点的值, void 说明对类型不做限制
    void *value;
} listNode;

/**
 * 双端链表迭代器
 */
typedef struct listIter {
    
    // 当前迭代的节点
    listNode *next;

    // 迭代方向
    int direction;
} listIter;

/**
 * 双端链表结构 
 */
typedef struct list {

    // 表头节点
    listNode *head;

    // 表尾节点
    listNode *tail;

    // 因为value无类型限制,所以调用方需提供
    // 处理value的复制,释放,对比函数
    // 节点值复制函数
    void *(*dup)(void *ptr);

    // 节点值释放函数
    void (*free)(void *ptr);

    // 节点值对比函数
    int (*match)(void *ptr, void *key);

    // 链表所包含的节点数量
    unsigned long len;
} list;

// 以宏形式实现的函数
/**
 * 返回给定链表所包含的节点数量
 * T = O(1)
 */
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

/**
 * 迭代器进行迭代的方向
 */
// 从表头向表尾进行迭代
#define AL_START_HEAD 0

// 从表尾向表头进行迭代
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
void *listRewindTail(list *list,listIter *li);
void listRotate(list *list);

#endif
