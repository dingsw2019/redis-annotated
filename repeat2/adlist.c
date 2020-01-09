#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"
#include <stdio.h>

// 创建一个空的双链表
list *listCreate(void){

    struct list *list;

    // 申请内存
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

// 从表头给双链表添加一个节点
list *listAddNodeHead(list *list,void *value){

    listNode *node;
    // 申请内存
    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 节点值指针
    node->value = value;

    // 空链表,表头表尾指向新节点,节点的前后节点指向NULL
    // 非空链表,表头指向新节点,原表头节点的前一个节点指向新节点
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
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

// listNode *listRewind(list *list,listIter *iter){
//     iter->next = list->head;
//     iter->direction = AL_START_HEAD;
//     return iter;
// }
void listRewind(list *list,listIter *iter){
    iter->next = list->head;
    iter->direction = AL_START_HEAD;
}

// listNode *listNextNode(listIter *iter){
//     listNode *current = iter->next;
//     if (iter->direction == AL_START_HEAD) {
//         iter->next = current->next;
//     } else {
//         iter->next = current->prev;
//     }

//     return current;
// }
listNode *listNext(listIter *iter){
    listNode *current = iter->next;

    if (current != NULL){
        if (iter->direction == AL_START_HEAD) {
            iter->next = current->next;
        } else {
            iter->next = current->prev;
        }
    }

    return current;
}

// void printList(list *list){
//     listIter *iter;
//     listNode *node;
//     printf("list size is %d, elements:",list->len);
//     node = listRewind(list,iter);
//     while((node = listNextNode(iter))){
//         printf("%s,",node->value);
//     }

//     printf("\n");
// }
void printList(list *list){
    
    printf("list size is %d, elements:", listLength(list));
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