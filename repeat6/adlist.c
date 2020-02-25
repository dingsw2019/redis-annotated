#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zmalloc.h"
#include "adlist.h"

// 创建一个空链表
list *listCreate(){

    list *list;
    // 申请链表内存
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    // 初始化链表属性和方法
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
    int len = list->len;
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

// 链表头部添加节点
list *listAddNodeHead(list *list,void *value){

    listNode *node;
    // 申请节点内存,节点赋值
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    
    if (list->len == 0) {
        // 空链表添加节点
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        // 非空链表添加节点
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    
    // 增加链表长度
    list->len++;

    return list;
}

list *listAddNodeTail(list *list,void *value){

    listNode *node;
    // 申请节点内存,节点赋值
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    if (list->len == 0) {
        // 空链表添加节点
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        // 非空链表添加节点
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }

    // 链表节点数量增加
    list->len++;
    return list;
}

// 获取迭代器
listIter *listGetIterator(list *list,int direction){

    listIter *iter;
    // 申请迭代器内存
    if ((iter = zmalloc(sizeof(*iter))) == NULL)
        return NULL;
    
    // 根据迭代方向设置节点
    if (direction == AL_START_HEAD) {
        iter->next = list->head;
    } else {
        iter->next = list->tail;
    }
    iter->direction = direction;

    return iter;
}

// 释放迭代器
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

// 迭代器方式获取下一个节点
listNode *listNext(listIter *iter){

    listNode *current = iter->next;

    // 获取下一个节点
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
    listNode *node;
    listIter *iter;
    // 申请链表内存,拷贝函数
    if ((copy = listCreate()) == NULL) {
        return NULL;
    }
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // 拷贝所有节点
    iter = listGetIterator(orig,AL_START_HEAD);
    while((node = listNext(iter)) != NULL){
        
        void *value;
        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else {
            value = node->value;
        }

        // 添加节点
        if (listAddNodeTail(copy,value) == NULL) {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }

    listReleaseIterator(iter);
    return copy;
}

// 尾节点移动成首节点
void listRotate(list *list){

    listNode *tail = list->tail;
    // 尾节点变更
    tail->prev->next = NULL;
    list->tail = tail->prev;

    // 移动节点指针变更
    tail->prev = NULL;
    tail->next = list->head;

    // 首节点变更
    list->head->prev = tail;
    list->head = tail;
}

// 节点值查找节点
listNode *listSearchKey(list *list,void *key){

    listNode *node;
    listIter *iter;

    if ((iter = listGetIterator(list,AL_START_HEAD)) == NULL) {
        return NULL;
    }
    // 迭代节点,查找指定节点值
    while((node = listNext(iter)) != NULL){

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

// 在指定节点插入前置或后置节点
list *listInsertNode(list *list,listNode *old_node,void *value,int after){

    listNode *node;
    // 申请节点内存,节点赋值
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    if (after) {
        // 添加后置节点,新节点的指针赋值,尾节点边界处理
        node->prev = old_node;
        node->next = old_node->next;
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        // 添加前置节点,新节点的指针赋值,首节点边界处理
        node->prev = old_node->prev;
        node->next = old_node;
        if (list->head == old_node) {
            list->head = node;
        }
    }

    // 新节点的左节点指针
    if (node->prev != NULL) {
        node->prev->next = node;
    }

    // 新节点的右节点指针
    if (node->next != NULL) {
        node->next->prev = node;
    }

    // 链表节点数增加
    list->len++;

    return list;
}

// 从链表中移除指定节点
void *listDelNode(list *list,listNode *node){

    // 指定节点的左节点指针变更
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    // 指定节点的右节点指针变更
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    // 释放节点
    if (list->free) list->free(node->value);
    zfree(node);

    // 链表节点数减少
    list->len--;
}

// 索引查找节点,正负代表方向
listNode *listIndex(list *list,int index){
    
    listNode *n;
    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }

    return n;
}

// 迭代方向为从尾到首
void listRewindTail(list *list,listIter *iter){
    iter->next = list->tail;
    iter->direction = AL_START_TAIL;
}

// 自定义比较函数
int keyMatch(void *str1,void *str2){
    return (strcmp(str1,str2)==0) ? 1 : 0;
}

// 打印链表
void printList(list *list){

    listNode *node;
    listIter *iter = listGetIterator(list,AL_START_HEAD);
    printf("list size %d , elements : ",listLength(list));
    while((node = listNext(iter)) != NULL){
        printf("%s ",node->value);
    }
    printf("\n");
    listReleaseIterator(iter);
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