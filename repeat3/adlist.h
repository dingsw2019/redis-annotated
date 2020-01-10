#ifndef __ADLIST_H__
#define __ADLIST_H__

// 双链表节点
typedef struct listNode {
    // 指针，指向前一个节点的首地址
    struct listNode *prev;
    // 指针，指向后一个节点的首地址
    struct listNode *next;
    // 指针，指向节点值的首地址
    // void 可以是任何类型的值
    void *value;
} listNode;

// 迭代器
typedef struct listIter {
    // 指针，指向下一个节点的首地址
    listNode *next;
    // 迭代方向
    int direction;
} listIter;


// 双链表
typedef struct list
{
    // 指针，指向头/尾节点的首地址
    listNode *head;
    listNode *tail;

    // 复制，释放，比对函数，用来处理节点值
    void (*dup)(void *ptr);
    void (*free)(void *ptr);
    void (*match)(void *ptr,void *key);

    // 节点数量
    unsigned long len;
} list;

// 链表长度
#define listLength(l) ((l)->len)

// 从表头向表尾迭代
#define AL_START_HEAD 0

// 从表尾向表头迭代
#define AL_START_TAIL 1

#endif