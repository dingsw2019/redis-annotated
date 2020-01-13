#include <string.h>
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

// 释放链表所有的节点 和链表
void listRelease(list *list){

    listNode *current,*next;
    unsigned long len = list->len;
    // 释放所有节点
    current = list->head;
    while(len--){
        next = current->next;

        if(list->free) list->free(current->value);

        zfree(current);

        current = next;
    }
    
    // 释放链表
    zfree(list);
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

// 从表尾给双链表添加一个节点
list *listAddNodeTail(list *list,void *value){

    listNode *node;
    // 为新节点创建内存
    if ((node=zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 节点值
    node->value = value;

    // 空链表，添加节点
    // 非空链表，新节点添加到链表表尾
    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }

    // 链表长度+1
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

void listRewindTail(list *list,listIter *iter){
    iter->next = list->tail;
    iter->direction = AL_START_TAIL;
}

// 构建指定链表的迭代器
listIter *listGetIterator(list *list,int direction){
    // 申请迭代器的内存
    listIter *iter;
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;

    // 根据方向，将迭代器开始指向链表头或尾
    if (direction == AL_START_HEAD) {
        iter->next = list->head;
    } else {
        iter->next = list->tail;
    }
    iter->direction = direction;
    return iter;
}

// listNode *listNext(listIter *iter){
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

// 释放构造器
void listReleaseIterator(listIter *iter){
    zfree(iter);
}

// listNode *listSearchKey(list *list,void *key){

//     listNode *node;
//     listIter *iter;

//     // 迭代所有节点，查找 key 相同的节点值
//     iter = listGetIterator(list,AL_START_HEAD);
//     while((node = listNext(iter)) != NULL){
        
//         if (list->match) {
//             if (list->match(node->value,key)) {
//                 listReleaseIterator(iter);
//                 return node;
//             }
//         } else {
//             if (node->value == key) {
//                 listReleaseIterator(iter);
//                 return node;
//             }
//         }
//     }

//     return NULL;
// }
listNode *listSearchKey(list *list,void *key){

    listNode *node;
    listIter *iter;

    // 迭代所有节点，查找 key 相同的节点值
    iter = listGetIterator(list,AL_START_HEAD);
    while((node = listNext(iter)) != NULL){
        
        if (list->match) {
            if (list->match(node->value,key)) {
                listReleaseIterator(iter);
                return node;
            }
        } else {
            if (node->value == key) {
                listReleaseIterator(iter);
                return node;
            }
        }
    }

    listReleaseIterator(iter);

    return NULL;
}

// 指定节点前/后添加新节点
list *listInsertNode(list *list,listNode *old_node,void *value,int after){

    listNode *node;
    // 创建新节点
    if ((node = zmalloc(sizeof(*node))) == NULL) return NULL;
    node->value = value;

    // 添加新节点到链表指定节点之前或之后
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        node->prev = old_node->prev;
        node->next = old_node;
        if (list->head == old_node) {
            list->head = node;
        }
    }
    
    // 新节点的前置节点指向变更
    if (node->prev != NULL) {
        node->prev->next = node;
    }

    // 新节点的后置节点指向变更
    if (node->next != NULL) {
        node->next->prev = node;
    }

    // 节点数量+1
    list->len++;

    return list;
}

// 删除指定节点
// void listDelNode(list *list,listNode *node){

//     if (list->head == node){
//         list->head = node->next;
//     } else if (list->tail == node){
//         list->tail = node->prev;
//     }

//     if (node->prev != NULL) {
//         node->prev->next = node;
//     }

//     if (node->next != NULL) {
//         node->next->prev = node;
//     }

//     zfree(node);

//     list->len--;
// }
void listDelNode(list *list,listNode *node){

    // 调整前置节点
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    // 调整后置节点
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    // 释放值
    if (list->free) list->free(node->value);

    // 释放节点
    zfree(node);

    // 节点数量-1
    list->len--;
}

// 按索引顺序获取节点
listNode *listIndex(list *list,long index){
    
    listNode *node;
    // 倒叙变正序
    if (index < 0) {
        node = list->tail;
        index = (-index)-1;
        while (index-- && node) node = node->prev;
    } else {
        node = list->head;
        while (index-- && node) node = node->next;
    }

    return node;
}

// 复制链表
// list *listDup(list *li){

//     list *copy;
//     listIter *iter;
//     listNode *node;

//     // 申请链表内存
//     if ((copy=zmalloc(sizeof(*copy))) == NULL) return NULL;

//     // 申请迭代器
//     iter = listGetIterator(li,AL_START_HEAD);

//     // 遍历链表,添加节点
//     while((node=listNext(iter)) != NULL){
//         void *value;
//         if (li->dup) 
//             value = li->dup(node->value);
//         else
//             value = node->value;

//         // 添加节点
//         if(listAddNodeTail(copy,value) == NULL){
//             // 失败,释放内存
//             listRelease(copy);
//             listReleaseIterator(iter);
//             return NULL;
//         }
//     }

//     // 释放迭代器
//     listReleaseIterator(iter);

//     return copy;
// }

list *listDup(list *orig){

    list *copy;
    listIter *iter;
    listNode *node;

    // 申请链表内存
    if ((copy=listCreate()) == NULL) 
        return NULL;

    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // 申请迭代器
    iter = listGetIterator(orig,AL_START_HEAD);

    // 遍历链表,添加节点
    while((node=listNext(iter)) != NULL){
        void *value;
        if (copy->dup){
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else
            value = node->value;

        // 添加节点
        if(listAddNodeTail(copy,value) == NULL){
            // 失败,释放内存
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }

    // 释放迭代器
    listReleaseIterator(iter);
    
    return copy;
}

list *listRotate(list *list){

    listNode *tail = list->tail;

    // 尾节点调整
    list->tail = tail->prev;
    list->tail->next = NULL;

    // 头节点调整
    tail->prev = NULL;
    tail->next = list->head;
    list->head->prev = tail;
    list->head = tail;

    return list;
}

// 字符串是否相等，相等返回 1 ，不等返回 0
int keyMatch(void *str1,void *str2){
    return (strcmp(str1,str2)==0) ? 1 : 0; 
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
    
    printf("list size is %d, elements : ", listLength(list));
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
    ln = listIndex(li,2);
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

    // listRelease(li);

    return 0;
}