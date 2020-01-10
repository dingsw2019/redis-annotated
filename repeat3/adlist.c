#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"
#include <stdio.h>

// 创建一个空链表
list *listCreate(void){

    list *list;
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

// 从链表表头添加新节点
list *listAddNodeHead(list *list,void *value){

    listNode *node;
    // 新节点，内存空间申请
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 节点赋值
    node->value = value;

    // 空链表,链表头尾为新节点,新节点的前后节点为 NULL
    // 非空链表,链表头指向新节点,旧头节点的前一个节点指向新节点
    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    // 节点数量+1
    list->len++;

    return list;
}

// 迭代器,将迭代器指针移动到列表表头
void listRewind(list *list,listIter *li){
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

// 迭代器,指向下一个节点
listNode *listNext(listIter *iter){
    listNode *current = iter->next;

    if (current != NULL) {
        if (iter->direction == AL_START_HEAD) {
            iter->next = current->next;
        } else {
            iter->next = current->prev;
        }
    }

    return current;
}

// 打印链表内数据
void printList(list *list){
    printf("count %d,elements:",listLength(list));
    listIter iter;
    listNode *node;
    listRewind(list,&iter);
    while((node = listNext(&iter)) != NULL){
        printf("%s ",(char*)node->value);
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