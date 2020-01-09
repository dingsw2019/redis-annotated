#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"
#include <stdio.h>


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

list *listAddNodeHead(list *list,void *value){

    // 新节点
    listNode *node;

    // 为新节点分配内存
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 保存值指针
    node->value = value;

    // 空链表,头尾指向空
    // 非空链表，将链表头指向新节点
    if (list->len == 0) {
        // 链表头尾指向 新节点
        list->head = list->tail = node;
        // 节点的前后节点不存在，指向 NULL
        node->prev = node->next = NULL;
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
    list *li = listCreate();

    for (int i = 0; i < sizeof(b)/sizeof(*b); ++i) {
        listAddNodeHead(li, b[i]);
    }

    printList(li);

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