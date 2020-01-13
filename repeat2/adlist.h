#ifndef __ADLIST_H__
#define __ADLIST_H__

// 双链表节点
typedef struct listNode {

    // 前一个节点
    struct listNode *prev;
    // 后一个节点
    struct listNode *next;
    // 节点值指针
    void *value;
} listNode;

// 双链表迭代器
typedef struct listIter {

    // 下一个节点
    listNode *next;

    // 迭代方向
    int direction;
} listIter;

// // 双链表
// typedef struct list {

//     // 表头节点
//     struct listNode *head;
//     // 表尾节点
//     struct listNode *tail;

//     // 复制,释放,比较函数
//     void (*dup)();
    
//     // 节点数量
//     unsigned int len;
// } list;

// 双链表
typedef struct list {

    // 表头节点
    listNode *head;
    // 表尾节点
    listNode *tail;

    // 复制,释放,比较函数
    void (*dup)(void *ptr);

    void (*free)(void *ptr);

    int (*match)(void *ptr,void *key);
    
    // 节点数量
    unsigned long len;
} list;

// #define listLength(l) (l)->len;
#define listLength(l) ((l)->len)

// #define listSetMatchMethod(l,match) ((l)->match)
#define listSetMatchMethod(l,m) ((l)->match = (m))

// 从表头向表尾
#define AL_START_HEAD 0

// 从表尾向表头
#define AL_START_TAIL 1

void listReleaseIterator(listIter *iter);
listIter *listGetIterator(list *list,int direction);
listNode *listSearchKey(list *list,void *key);
list *listInsertNode(list *list,listNode *old_node,void *value,int after);

#endif