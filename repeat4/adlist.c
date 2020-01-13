#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zmalloc.h"
#include "adlist.h"

list *listCreate(void){
    struct list *list;
    if((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;

    return list;
}

void listRelease(list *list){
    listNode *current,*next;
    unsigned long len = listLength(list);

    current = list->head;
    while(len--){
        next = current->next;
        if (list->free) list->free(current->value);
        zfree(current);
        current = next;
    }

    zfree(list);
}

listIter *listGetIterator(list *list,int direction){
    listIter *iter;
    if((iter = zmalloc(sizeof(*iter))) == NULL)
        return NULL;

    if (direction == AL_START_HEAD) {
        iter->next = list->head;
    } else {
        iter->next = list->tail;
    }

    iter->direction = direction;

    return iter;
}

void listReleaseIterator(listIter *iter){
    zfree(iter);
}

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

list *listAddNodeHead(list *list,void *value){
    listNode *node;

    if ((node=zmalloc(sizeof(*node))) == NULL)
        return NULL;

    node->value = value;

    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    list->len++;

    return list;
}

list *listAddNodeTail(list *list,void *value){
    listNode *node;
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }

    list->len++;

    return list;
}

list *listDup(list *orig){
    list *copy;
    listIter *iter;
    listNode *node;

    if ((copy = listCreate()) == NULL)
        return NULL;
    
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    iter = listGetIterator(orig,AL_START_HEAD);
    while((node = listNext(iter)) != NULL){
        void *value;
        if (orig->dup) {
            value = orig->dup(node->value);
            if (value == NULL){
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else {
            value = node->value;
        }

        if(listAddNodeTail(copy,node->value) == NULL){
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }

    listReleaseIterator(iter);

    return copy;
}

list *listRotate(list *list){

    listNode *tail = list->tail;
    // 表尾处理
    list->tail = tail->prev;
    list->tail->next = NULL;

    // 节点处理
    tail->prev = NULL;
    tail->next = list->head;

    // 表头处理
    list->head->prev = tail;
    list->head = tail;

    return list;
}

int keyMatch(void *str1,void *str2){
    return (strcmp(str1,str2)==0) ? 1 : 0;
}

void printList(list *list){
    listIter *iter;
    listNode *node;

    iter = listGetIterator(list,AL_START_HEAD);
    printf("list size %d, element : ",list->len);
    while((node=listNext(iter)) != NULL){
        printf("%s ",node->value);
    }
    printf("\n");
}

//gcc -g zmalloc.c adlist.c
int main(void){

    char b[][10] = {"believe", "it", "or", "not"};
    listIter iter;
    listNode *node;
    list *li = listCreate();

    // 表头添加，结果：li size is 4, elements:not or it believe
    for (int i = 0; i < sizeof(b)/sizeof(*b); ++i) {
        listAddNodeHead(li, b[i]);
    }
    printf("listAddNodeHead : ");
    printList(li);

    listRelease(li);
    li = listCreate();
    // 表尾添加, 结果：li size is 4, elements:believe it or not
    for (int i = 0; i < sizeof(b)/sizeof(*b); ++i) {
        listAddNodeTail(li, b[i]);
    }
    printf("listAddNodeTail : ");
    printList(li);

    // 复制链表
    printf("duplicate a new list : ");
    list *lidup = listDup(li);
    printList(lidup);

    // 表尾移动到表头
    listRotate(li);
    printList(li);

    // // 插入节点
    // listSetMatchMethod(li, keyMatch);
    // listNode *ln = listSearchKey(li, "it");
    // printf("find key is :%s\n", (char*)ln->value);
    // li = listInsertNode(li,ln,"insert1",1);
    // printList(li);

    // // 插入头节点
    // printf("head insert node: ");
    // ln = listSearchKey(li,"not");
    // li = listInsertNode(li,ln,"insertHead",0);
    // printList(li);

    // // 删除节点
    // printf("del node : ");
    // listDelNode(li,ln);
    // printList(li);

    // // 删除头节点
    // printf("del head node : ");
    // ln = listSearchKey(li,"insertHead");
    // listDelNode(li,ln);
    // printList(li);

    // // 索引搜索节点
    // ln = listIndex(li,2);
    // if (ln) {
    //     printf("listIndex 2 value : %s \n",ln->value);
    // } else {
    //     printf("listIndex 2 value : NULL");
    // }

    // // 反转链表
    // printf("reverse output the list : ");
    // printf("li size is %d, elements: ", listLength(li));
    // listRewindTail(li, &iter);
    // while ((node = listNext(&iter)) != NULL) {
    //     printf("%s ", (char*)node->value);
    // }
    // printf("\n");

    // listRelease(li);

    return 0;
}