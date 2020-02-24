#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zmalloc.h"
#include "adlist.h"


// 创建空链表
list *listCreate(){

    list *list;
    // 创建内存
    if ((list = zmalloc(sizeof(*list))) == NULL) {
        return NULL;
    }
    // 初始化参数
    list->head = list->tail = NULL;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    list->len = 0;

    return list;
}

// 从表头添加新节点
void listAddNodeHead(list *list,void *value){

    listNode *node;
    // 创建节点
    if ((node = zmalloc(sizeof(*node))) == NULL){
        return ;
    }
    node->value = value;
    
    if (list->len == 0) {
        // 列表无节点添加
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        // 列表有节点添加
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    
    // 列表长度增加
    list->len++;
}

// 获取迭代器
listIter *listGetIterator(list *list,int direction){

    listIter *iter;
    // 申请内存
    if ((iter = zmalloc(sizeof(*iter))) == NULL) {
        return NULL;
    }

    // 节点
    if (direction == AL_START_HEAD) {
        iter->next = list->head;
    } else {
        iter->next = list->tail;
    }
    iter->direction = direction;

    return iter;
}

// 迭代器获取下一个节点
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
void listReleaseIterator(listIter *iter){
    zfree(iter);
}

void printList(list *li){

    listNode *node;
    listIter *iter = listGetIterator(li,AL_START_HEAD);
    printf("list size %d , elements : ",listLength(li));
    while ((node = listNext(iter)) != NULL) {
        printf("%s ",node->value);
    }
    printf("\n");
    listReleaseIterator(iter);
}

void listRelease(list *list) {

    listNode *next, *current;
    unsigned long len = list->len;

    current = list->head;
    // 释放节点
    while (len--) {
        next = current->next;
        // 释放value
        if (list->free) list->free(current->value);
        zfree(current);
        current = next;
    }
    // 释放链表
    zfree(list);
}

list *listAddNodeTail(list *list,void *value){

    listNode *node;
    // 创建节点
    if ((node = zmalloc(sizeof(*node))) == NULL) {
        return NULL;
    }
    // 节点赋值
    node->value = value;
    
    if (list->len == 0) {
        // 空链表新增节点
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        // 非空链表新增节点
        node->next = NULL;
        node->prev = list->tail;
        list->tail->next = node;
        list->tail = node;
    }

    // 链表长度增加
    list->len++;

    return list;
}

// 拷贝链表
list *listDup(list *orig){

    list *copy;
    listNode *node;
    listIter *iter;

    // 申请链表空间
    if ((copy = listCreate()) == NULL) {
        return NULL;
    }
    // 拷贝链表函数
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // 拷贝所有节点
    iter = listGetIterator(orig,AL_START_HEAD);
    while((node = listNext(iter)) != NULL){

        void *value;
        if (orig->dup) {
            value = orig->dup(node->value);
            if (value == NULL) {
                listReleaseIterator(iter);
                listRelease(copy);
                return NULL;
            }
        } else {
            value = node->value;
        }

        if (listAddNodeTail(copy,value) == NULL){
            listReleaseIterator(iter);
            listRelease(copy);
            return NULL;
        }
    }

    listReleaseIterator(iter);
    return copy;
}

// 表尾节点移动到表头
list *listRotate(list *list){

    listNode *tail = list->tail;

    // 表尾节点
    tail->prev->next = NULL;
    list->tail = tail->prev;

    // 移动的节点
    tail->next = list->head;
    tail->prev = NULL;

    // 表头指针变更
    list->head->prev = tail;
    list->head = tail;

    return list;
}

int keyMatch(void *str1,void *str2){
    return (strcmp(str1,str2)==0) ? 1 : 0;
}

listNode *listSearchKey(list *list,void *value){

    listNode *node;
    listIter *iter;
    // 迭代器
    iter = listGetIterator(list,AL_START_HEAD);

    // 搜索指定节点值
    while((node = listNext(iter)) != NULL){
        if (list->match) {
            if (list->match(node->value,value)){
                listReleaseIterator(iter);
                return node;
            }
        } else {
            if (node->value == value){
                listReleaseIterator(iter);
                return node;
            }
        }
    }

    // 返回
    listReleaseIterator(iter);
    return NULL;
}

list *listInsertNode(list *list,listNode *old_node,void *value,int after){

    listNode *node;
    // 创建新节点
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    if (after) {
        // 指定节点后添加
        node->prev = old_node;
        node->next = old_node->next;
        if (old_node == list->tail){
            list->tail = node;
        }
    } else {
        // 指定节点前添加
        node->prev = old_node->prev;
        node->next = old_node;
        if (old_node == list->head) {
            list->head = node;
        }
    }

    // 新节点的前置节点修改
    if (node->prev != NULL) {
        node->prev->next = node;
    }

    // 新节点的后置节点修改
    if (node->next != NULL) {
        node->next->prev = node;
    }

    list->len++;

    return list;
}

// 删除节点
void listDelNode(list *list,listNode *node){

    // 指定节点的前置节点指针变更
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    // 指定节点的后置节点指针变更
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    // 减少节点数量
    list->len--;
}

// 按索引搜索节点
listNode *listIndex(list *list,int index){

    listNode *n = list->head;
    // 负索引转正索引
    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while (index-- && n) n = n->prev;
    } else {
        n = list->head;
        while (index-- && n) n = n->next;
    }

    return n;
}

void listRewindTail(list *list,listIter *iter){
    iter->next = list->tail;
    iter->direction = AL_START_TAIL;
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
    printf("list rotate : ");
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
    printf("del 'not' node : ");
    listDelNode(li,ln);
    printList(li);

    // 删除头节点
    printf("del head node : ");
    ln = listSearchKey(li,"insertHead");
    listDelNode(li,ln);
    printList(li);

    // 删除尾节点
    printf("del tail node : ");
    ln = listSearchKey(li,"or");
    listDelNode(li,ln);
    printList(li);

    // 索引搜索节点
    ln = listIndex(li,2);
    if (ln) {
        printf("listIndex 2 value,(insert1) : %s \n",ln->value);
    } else {
        printf("listIndex 2 value,(insert1) : NULL");
    }

    ln = listIndex(li,-1);
    if (ln) {
        printf("listIndex 2 value,(insert1) : %s \n",ln->value);
    } else {
        printf("listIndex 2 value,(insert1) : NULL");
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