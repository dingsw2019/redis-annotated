#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zmalloc.h"
#include "adlist.h"


// 创建一个空链表
list *listCreate(void){

    list *list;
    // 申请内存
    if((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;

    // 初始化属性
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
    unsigned long len = list->len;

    current = list->head;

    // 迭代链表,释放节点
    while(len--){
        next = current->next;
        if (list->free) list->free(current->value);
        zfree(current);
        current = next;
    }

    // 释放链表
    zfree(list);
}

// 从表头添加一个节点
list *listAddNodeHead(list *list,void *value){
    listNode *node;
    // 新增节点
    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 新节点添加到链表
    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    // 节点数增加
    list->len++;

    return list;
}

// 从表尾添加一个节点
list *listAddNodeTail(list *list,void *value){
    listNode *node;
    // 新增节点
    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 新节点添加到链表
    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    
    // 节点数增加
    list->len++;

    return list;
}

// 复制链表
list *listDup(list *orig){

    list *copy;
    listIter *iter;
    listNode *node;
    // 新建一个链表
    if ((copy = listCreate()) == NULL)
        return NULL;
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // 遍历链表，添加节点
    iter = listGetIterator(orig,AL_START_HEAD);
    while((node = listNext(iter)) != NULL){
        void *value;
        if (orig->dup) {
            value = orig->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        }else {
            value = node->value;
        }
        if (listAddNodeTail(copy,value) == NULL) {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }
    listReleaseIterator(iter);
    return copy;
}

// 创建一个指定链表的迭代器
listIter *listGetIterator(list *list,int direction){
    listIter *iter;
    // 申请内存
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

void listReleaseIterator(listIter *iter){
    zfree(iter);
}

// 移动迭代器
listNode *listNext(listIter *iter){
    listNode *current = iter->next;

    if (current != NULL) {
        // 迭代指针指向下一个节点
        if (iter->direction == AL_START_HEAD) {
            iter->next = current->next;
        } else {
            iter->next = current->prev;
        }
    }

    return current;
}

// 表尾节点移动到表头
void listRotate(list *list){

    listNode *tail = list->tail;

    // 表尾指针变更
    tail->prev->next = NULL;
    list->tail = tail->prev;

    // 节点指针变更
    tail->prev = NULL;
    tail->next = list->head;

    // 表头指针变更
    list->head->prev = tail;
    list->head = tail;
}

// 匹配 value
listNode *listSearchKey(list *list,void *key){
    
    listIter *iter = listGetIterator(list,AL_START_HEAD);
    listNode *node;

    while((node=listNext(iter)) != NULL){

        if (list->match) {
            if (list->match(node->value,key)) {
                listReleaseIterator(iter);
                return node;
            }
        } else {
            if (node->value == key){
                listReleaseIterator(iter);
                return node;
            }
        }
    }

    listReleaseIterator(iter);
    return NULL;
}

list *listInsertNode(list *list,listNode *old_node,void *value,int after){

    listNode *node;
    // 创建节点
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 已有节点之后添加新节点
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        if (list->tail == old_node) {
            list->tail = node;
        }
    // 已有节点之前添加新节点
    } else {
        node->prev = old_node->prev;
        node->next = old_node;
        if (list->head == old_node) {
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

void listDelNode(list *list,listNode *node){

    // 前置节点指针变更
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    // 后置节点指针变更
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    list->len--;
}

listNode *listIndex(list *list,long index){

    listNode *n;
    // 负数转正数
    if (index < 0) {
        index  = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }

    return n;
}

void listRewindTail(list *list,listIter *iter){
    iter->next = list->tail;
    iter->direction = AL_START_TAIL;
}

// 自定义比对函数
int keyMatch(void *str1,void *str2){
    return (strcmp(str1,str2)==0) ? 1 : 0;
}

// 打印链表的所有节点值
void printList(list *list){
    listIter *iter = listGetIterator(list,AL_START_HEAD);
    listNode *node;
    printf("list size %d , elements : ",listLength(list));
    while((node = listNext(iter)) != NULL){
        printf("%s ",node->value);
    }
    printf("\n");
    listReleaseIterator(iter);
}

#ifdef ADLIST_TEST_MAIN
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
#endif