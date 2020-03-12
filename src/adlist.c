/**
 * 
 * 双端链表作用
 *   1.作为 Redis 列表类型的底层结构之一
 *   2.作为通用数据结构，被其他功能模块所使用
 * 
 * 双端链表及其节点的性能特征
 *   1.节点有指向前后节点的指针,访问前后节点的复杂度为O(1),
 *      并且可以从表头迭代到表尾或表尾迭代到表头
 *   2.链表有表头和表尾指针,因此对表头或表尾处理的复杂度为O(1)
 *   3.链表有节点数量的属性,可在O(1)复杂度内返回节点数
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adlist.h"
#include "zmalloc.h"

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
 * 创建一个包含值 value 的新节点，并将它添加到 old_node 之前或之后
 * 
 * 如果 after = 0，将新节点添加到 old_node 之后
 * 如果 after = 1，将新节点添加到 old_node 之前
 *
 * T = O(1)
 */
list *listInsertNode(list *list,listNode *old_node,void *value,int after){

    listNode *node;

    // 创建新节点
    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    node->value = value;

    // 将新节点加入链表
    if (after) {
        // 新节点添加到旧节点之后
        node->prev = old_node;
        node->next = old_node->next;
        // 旧节点为尾节点,更新尾节点
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        // 新节点添加到旧节点之前
        node->prev = old_node->prev;
        node->next = old_node;
        // 旧节点为头节点，更新头结点
        if (list->head == old_node) {
            list->head = node;
        }
    }

    // 新节点的前一个节点，原来指向旧节点，
    // 改为指向新节点
    if (node->prev != NULL) {
        node->prev->next = node;
    }

    // 新节点的后一个节点，原来指向旧节点
    // 改为指向新节点
    if (node->next != NULL) {
        node->next->prev = node;
    }

    // 节点数量+1
    list->len++;

    return list;
}

/**
 * 从链表 list 中删除给定节点 node
 * 
 * T = O(1)
 */
void listDelNode(list *list,listNode *node){

    // 调整前置节点的指针
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        // 头节点调整
        list->head = node->next;
    }

    // 调整后置节点的指针
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        // 尾节点调整
        list->tail = node->prev;
    }

    // 释放值
    if (list->free) list->free(node->value);

    // 释放节点
    zfree(node);

    // 链表数量-1
    list->len--;
}

/**
 * 为指定链表创建一个迭代器
 * 同时指定方向，迭代时使用
 * 
 * T = O(1)
 */
listIter *listGetIterator(list *list,int direction){

    // 为迭代器分配内存
    listIter *iter;
    if ((iter=zmalloc(sizeof(*iter))) == NULL) return NULL;
    
    // 根据迭代方向，设置迭代器的起始节点
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else 
        iter->next = list->tail;
    
    // 记录迭代方向
    iter->direction = direction;

    return iter;
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


/**
 * 释放迭代器
 * 
 * T = O(1)
 */
void listReleaseIterator(listIter *iter){
    zfree(iter);
}

/**
 * 复制整个链表
 * 
 * 复制成功返回输入链表的副本
 * 如果因为内存不足而造成复制失败，返回NULL
 * 
 * 优先使用自定义的 节点值复制函数 dup
 * 
 * 无论复制成功还是失败，输入节点都不会修改
 * 
 * T = O(N)
 */
list *listDup(list *orig){

    list *copy;
    listIter *iter;
    listNode *node;

    // 创建新链表
    if ((copy = listCreate()) == NULL) {
        return NULL;
    }
    // 设置节点值处理函数
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // 创建迭代器
    iter = listGetIterator(orig,AL_START_HEAD);

    // 复制链表的所有节点
    while ((node = listNext(iter)) != NULL){

        void *value;

        // 复制节点值
        if (copy->dup) {
            value = copy->dup(node->value);
            // 复制异常,释放申请的内存
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else {
            value = node->value;
        }

        // 将节点添加到链表
        if (listAddNodeTail(copy,value) == NULL) {
            // 添加节点异常，释放申请的内存
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }

    // 释放迭代器
    listReleaseIterator(iter);

    // 返回副本
    return copy;
}

/**
 * 查找链表 list 中值和 key 匹配的节点
 * 
 * 优先使用自定义的 match 函数进行匹配
 * 
 * 匹配成功，返回节点地址
 * 匹配失败，返回NULL
 * 
 * T = O(N)
 */
listNode *listSearchKey(list *list,void *key){

    listIter *iter;
    listNode *node;

    // 获取 list 的迭代器
    iter = listGetIterator(list,AL_START_HEAD);

    // 匹配 key ，优先使用自定义匹配函数
    while((node=listNext(iter)) != NULL){

        if (list->match) {
            if(list->match(node->value,key)){
                // 匹配成功，释放迭代器
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

/**
 * 返回链表在给定索引上的节点
 * 
 * 索引可以是任一个整数
 * 如果索引超出范围，返回NULL
 *
 * T = O(N)
 */
listNode *listIndex(list *list,long index) {
    listNode *n;

    // 如果索引为负数，从表尾开始查找
    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    // 如果索引为正数，从表头开始查找
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }

    return n;
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
 * 将迭代器的方向设置为 从表尾到表头
 * 并将迭代指针重新指向表尾节点
 * 
 * T = O(1)
 */
void *listRewindTail(list *list,listIter *li){
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/**
 * 将链表尾节点，移动为表头节点
 * 
 * T = O(1)
 */
void listRotate(list *list){

    // 尾节点
    listNode *tail = list->tail;

    // 新链表尾节点
    list->tail = tail->prev;
    list->tail->next = NULL;

    // 新头节点
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}

/**
 * 字符串 str1 与 str2 是否相等
 * 
 * 返回 相等返回 1
 *      不等返回 0
 */
int keyMatch(void *str1,void *str2){
    return (strcmp(str1,str2))==0 ? 1 : 0;
}

// 打印链表的所有节点
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

    printf("search a key :");
    listSetMatchMethod(li, keyMatch);
    listNode *ln = listSearchKey(li, "it");
    if (ln != NULL) {
        printf("find key is :%s\n", (char*)ln->value);
    } else {
        printf("not found\n");
    }

    // 插入节点
    li = listInsertNode(li,ln,"insert1",1);
    printList(li);

    // 插入头节点
    printf("head insert node: ");
    ln = listSearchKey(li,"believe");
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
    ln = listIndex(li,-1);
    if (ln) {
        printf("listIndex : %s \n",ln->value);
    } else {
        printf("listIndex : NULL");
    }

    // 表尾变表头
    listRotate(li);
    printList(li);

    // 反转链表
    printf("reverse output the list : ");
    printf("li size is %d, elements:", listLength(li));
    listRewindTail(li, &iter);
    while ((node = listNext(&iter)) != NULL) {
        printf("%s ", (char*)node->value);
    }
    printf("\n");

    // 复制链表
    printf("duplicate a new list : ");
    list *lidup = listDup(li);
    printList(lidup);

    listRelease(li);

    return 0;
}