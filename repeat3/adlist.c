#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zmalloc.h"
#include "adlist.h"


// 创建链表
list *listCreate(void){

    list *list;
    
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;

    list->head = list->tail = NULL;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    list->len = 0;

    return list;
}

// 释放链表
void listRelease(list *list){
    
    listNode *current,*next;
    unsigned long len;

    len = list->len;
    current = list->head;

    // 释放所有节点
    while(len--){
        next = current->next;
        if (list->free) list->free(current->value);
        zfree(current);
        current = next;
    }

    // 释放链表
    zfree(list);
}

// 表头添加节点
list *listAddNodeHead(list *list,void *value){
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 空链表
    if (list->len <= 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    // 非空链表
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    list->len++;

    return list;
}

// 表尾添加节点
list *listAddNodeTail(list *list,void *value){
    
    listNode *node;
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    if (list->len <= 0) {
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

// 迭代指针重新指向链表头
listIter *listRewind(list *list,listIter *iter){
    iter->next = list->head;
    iter->direction = AL_START_HEAD;
}

// 迭代指针重新指向链表尾
listIter *listRewindTail(list *list,listIter *iter){
    iter->next = list->tail;
    iter->direction = AL_START_TAIL;
}

// 获取某链表的迭代器
listIter *listGetIterator(list *list,int direction){
    listIter *iter;
    if ((iter = zmalloc(sizeof(*iter))) == NULL)
        return NULL;

    if (direction == AL_START_HEAD) {
        iter->next = list->head;
    } else {
        iter->next = list->tail;
    }

    iter->direction = direction;
    return iter;
}

void listReleaseIter(listIter *iter){
    zfree(iter);
}

// 通过迭代器获取下一个节点
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

// 拷贝链表
list *listDup(list *orig){
    list *copy;
    listIter *iter;
    listNode *node;

    // 创建 复制链表
    if ((copy = listCreate()) == NULL)
        return NULL;
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // 遍历链表
    iter = listGetIterator(orig,AL_START_HEAD);
    while((node=listNext(iter)) != NULL){
        void *value;
        if (orig->dup) {
            value = orig->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                listReleaseIter(iter);
                return NULL;
            }
        } else 
            value = node->value;

        if (listAddNodeTail(copy,value) == NULL) {
            listRelease(copy);
            listReleaseIter(iter);
            return NULL;
        }
    }

    listReleaseIter(iter);

    return copy;
}

// 表尾节点移动到表头
list *listRotate(list *list){
    
    listNode *tail = list->tail;

    // 调整表尾指针
    list->tail = tail->prev;
    list->tail->next = NULL;

    // 调整表头指针
    tail->prev = NULL;
    tail->next = list->head;
    list->head->prev = tail;
    list->head = tail;

    return list;
}

// 根据 value 查找节点
listNode *listSearchKey(list *list,void *key){

    listIter *iter;
    listNode *node;

    // 遍历节点
    iter = listGetIterator(list,AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {

        if (list->match){
            if (list->match(node->value,key)){
                listReleaseIter(iter);
                return node;
            }
        } else {
            if (node->value == key) {
                listReleaseIter(iter);
                return node;
            }
        }
    }

    listReleaseIter(iter);

    return NULL;
}

// 在指定节点之前或之后添加节点
list *listInsertNode(list *list,listNode *old_node,void *value,int after){
    listNode *node;

    // 创建节点
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 添加节点
    if (after) {
        // old_node 之后添加
        node->prev = old_node;
        node->next = old_node->next;
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        // old_node 之前添加
        node->prev = old_node->prev;
        node->next = old_node;
        if (list->head == old_node) {
            list->head = node;
        }
    }

    if (node->prev != NULL) {
        node->prev->next = node;
    }

    if (node->next != NULL) {
        node->next->prev = node;
    }

    list->len++;

    return list;
}

// 删除节点
void listDelNode(list *list,listNode *node){
    
    // 前置节点调整指针
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    // 后置节点调整指针
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    list->len--;
}

// 索引搜索节点
listNode *listIndex(list *list,int index){
    listNode *n;

    if (index <= 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }

    return n;
}

// 比较两字符串是否相同
int keyMatch(void *str1,void *str2){
    return (strcmp(str1,str2)==0) ? 1 : 0;
}

// 打印链表的所有节点
void printList(list *list){

    listIter *iter;
    listNode *node;
    iter = listGetIterator(list,AL_START_HEAD);
    printf("list size is %d, elements : ",listLength(list));
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

    // 插入节点
    listSetMatchMethod(li, keyMatch);
    listNode *ln = listSearchKey(li, "it");
    printf("find key is :%s\n", (char*)ln->value);
    li = listInsertNode(li,ln,"insert1",1);
    printList(li);

    // 插入头节点
    printf("head insert node: ");
    ln = listSearchKey(li,"not");
    li = listInsertNode(li,ln,"insertHead",0);
    printList(li);

    // 删除节点
    printf("del node : ");
    listDelNode(li,ln);
    printList(li);

    // 删除头节点
    printf("del head node : ");
    ln = listSearchKey(li,"insertHead");
    listDelNode(li,ln);
    printList(li);

    // 索引搜索节点
    ln = listIndex(li,2);
    if (ln) {
        printf("listIndex 2 value : %s \n",ln->value);
    } else {
        printf("listIndex 2 value : NULL");
    }

    // 反转链表
    printf("reverse output the list : ");
    printf("li size is %d, elements: ", listLength(li));
    listRewindTail(li, &iter);
    while ((node = listNext(&iter)) != NULL) {
        printf("%s ", (char*)node->value);
    }
    printf("\n");

    listRelease(li);

    return 0;
}