#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"
#include <stdio.h>

/**
 * 创建一个新的空链表
 * 
 * 创建成功返回链表，失败返回 NULL
 * 
 * T = O(1)
 */
list *listCreate(void){

    struct list *list;

    // 分配内存
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;

    // 初始化属性
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;

    return list;
}

/**
 * 释放整个链表，以及链表中所有节点
 * 
 * T = O(N)
 */
void listRelease(list *list){

    // 释放所有节点
    unsigned long len;
    listNode *current,*next;

    // 指向头指针
    current = list->head;
    // 遍历整个链表
    len = list->len;
    while(len--){
        next = current->next;

        // 如果设置了值释放函数，那么调用它
        if (list->free) list->free(current->value);

        // 释放节点
        zfree(current);

        current = next;
    }
    
    // 释放链表
    zfree(list);
}

/**
 * 将一个包含有给定值指针 value 的新节点添加到链表的表头
 * 
 * 如果新节点分配内存出错，那么不执行任何动作，返回 NULL
 * 
 * 如果执行成功，返回传入的链表指针
 * 
 * T = O(1)
 */
list *listAddNodeHead(list *list,void *value){

    // 新节点
    listNode *node;

    // 为新节点分配内存
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 保存值指针
    node->value = value;

    // 添加节点到空链表
    if (list->len == 0) {
        // 链表头尾指向 新节点
        list->head = list->tail = node;
        // 节点的前后节点不存在，指向 NULL
        node->prev = node->next = NULL;
    // 添加节点到非空链表
    } else {
        // 新节点的前一个节点不存在，指向 NULL
        node->prev = NULL;
        // 新节点的下一个节点 指向旧表头
        node->next = list->head;
        // 旧表头的前一个节点 指向新节点
        list->head->prev = node;
        // 链表头指向新节点
        list->head = node;
    }

    // 增加链表长度
    list->len++;

    return list;
}

/**
 * 将一个包含有给定值指针 value 的新节点添加到链表的表尾
 * 
 * 如果新节点分配内存出错，那么不执行任何动作，返回 NULL
 * 
 * 如果执行成功，返回传入的链表指针
 * 
 * T = O(1)
 */
list *listAddNodeTail(list *list,void *value){
    
    listNode *node;
    // 新节点申请内存
    if((node=zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 节点赋值
    node->value = value;

    // 空链表,链表头尾均指向新节点
    // 非空链表,表尾指向新节点
    if (list->len == 0){
        // 新节点无前后节点
        node->prev = node->next = NULL;
        // 只有一个节点,链表头尾均指向新节点
        list->head = list->tail = node;
    } else {
        // 尾节点后没有节点
        node->next = NULL;
        // 尾节点的前一个节点,是旧的尾节点
        node->prev = list->tail;
        // 旧的尾节点的后一个节点,是新节点
        list->tail->next = node;
        // 链表的尾节点指向新节点
        list->tail = node;
    }

    // 链表节点数+1
    list->len++;

    return list;
}

/**
 * 将迭代器的方向设置为 从表头向表尾迭代
 * 并将迭代指针重新指向表头节点
 * 
 * T = O(1)
 */
void listRewind(list *list,listIter *li){
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

/**
 * 返回迭代器当前所指向的节点
 * 删除当前节点是允许的，但不能修改链表里的其他节点
 * 函数要么返回一个节点，要么返回 NULL
 * 
 * T = O(1)
 */
listNode *listNext(listIter *iter){
    listNode *current = iter->next;

    if (current != NULL) {
        // 根据方向选择下一个节点
        if (iter->direction == AL_START_HEAD)
            // 保存下一个节点，防止当前节点被删除而造成指针丢失
            iter->next = current->next;
        else
            // 保存下一个节点，防止当前节点被删除而造成指针丢失
            iter->next = current->prev;
    }

    return current;
}

void printList(list *li) {
    printf("li size is %d, elements:", listLength(li));
    listIter iter;
    listNode *node;
    listRewind(li, &iter);
    while ((node = listNext(&iter)) != NULL) {
        printf("%s ", (char*)node->value);
    }
    printf("\n");
}

//gcc -g zmalloc.c adlist.c
int main(void){

    char b[][10] = {"believe", "it", "or", "not"};
    // listIter iter;
    listNode *node;
    list *list = listCreate();

    // 表头添加，结果：li size is 4, elements:not or it believe
    for (int i = 0; i < sizeof(b)/sizeof(*b); ++i) {
        listAddNodeHead(list, b[i]);
    }
    printList(list);

    listRelease(list);
    list = listCreate();
    // 表尾添加, 结果：li size is 4, elements:believe it or not
    for (int i = 0; i < sizeof(b)/sizeof(*b); ++i) {
        listAddNodeTail(list, b[i]);
    }
    printList(list);


    

    // printf("\nSearch a key :\n");
    // listSetMatchMethod(li, keyMatch);
    // listNode *ln = listSearchKey(li, "believe");
    // if (ln != NULL) {
    //     printf("find key is :%s\n", (char*)ln->value);
    // } else {
    //     printf("not found\n");
    // }

    // printf("\nReverse output the list :\n");
    // printf("li size is %d, elements:", listLength(li));
    // listRewindTail(li, &iter);
    // while ((node = listNext(&iter)) != NULL) {
    //     printf("%s ", (char*)node->value);
    // }
    // printf("\n");

    // printf("\nduplicate a new list :\n");
    // list *lidup = listDup(li);
    // printList(lidup);


    // printf("\nConnect two linked lists :\n");
    // listJoin(li, lidup);
    // printList(li);


    // listRelease(li);

    return 0;
}