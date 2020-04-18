#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zmalloc.h"
#include "adlist.h"


/*----------------- API -----------------*/

// 创建并返回一个空列表
list *listCreate(void) {

    list *li;
    // 申请内存 
    if((li = zmalloc(sizeof(*li))) == NULL)
        return NULL;
    // 初始化属性
    li->head = li->tail = NULL;
    li->len = 0;
    li->dup = NULL;
    li->free = NULL;
    li->match = NULL;

    return li;
}

// 头部添加节点
list *listAddNodeHead(list *list, void *value) {

    // 创建一个新节点
    listNode *node;
    if ((node = zmalloc(sizeof(*node))) == NULL) 
        return NULL;
    node->value = value;

    // 空链表添加
    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    // 非空链表添加
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    // 更新节点计数器
    list->len++;

    return list;
}

// 尾部添加节点
list *listAddNodeTail(list *list, void *value) {

    listNode *node;
    // 创建新节点
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 空链表添加
    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;

    // 非空链表添加
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }

    // 更新节点计数器
    list->len++;

    return list;
}

// 释放链表
void listRelease(list *list) {

    unsigned long len = list->len;
    listNode *current, *next;

    current = list->head;

    // 释放节点
    while (len--) {
        next = current->next;

        if (list->free) {
            list->free(current->value);
        } 

        zfree(current);

        current = next;
    }

    // 释放链表
    zfree(list);
}

// 获取迭代器
listIter *listGetIterator(list *list, int direction) {

    // 申请内存空间
    listIter *iter = zmalloc(sizeof(*iter));
    if (iter == NULL) return NULL;

    // 起始节点
    if (direction == AD_START_HEAD) {
        iter->next = list->head;
    } else {
        iter->next = list->tail;
    }

    // 方向
    iter->direction = direction;

    return iter;
}

// 获取下一个节点
listNode *listNext(listIter *iter) {

    listNode *current = iter->next;

    if (current != NULL) {

        if (iter->direction == AD_START_HEAD) {
            iter->next = current->next;
        } else {
            iter->next = current->prev;
        }
    }

    return current;
}

// 释放迭代器
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

// 匹配相同节点值
listNode *listSearchKey(list *list, void *key) {

    listNode *node;
    listIter *iter = listGetIterator(list,AD_START_HEAD);

    while((node = listNext(iter)) != NULL) {

        if (list->match) {
            if (list->match(node->value, key)) {
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

// 指定节点两端添加新节点
// after=0, 在之后添加
// after=1, 在之前添加
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {

    listNode *node;

    // 创建新节点
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 处理新节点的指针指向
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        // 尾节点
        if (list->tail == old_node)
            list->tail = node;
    } else {
        node->prev = old_node->prev;
        node->next = old_node;
        // 头节点
        if (list->head == old_node) 
            list->head = node;
    }

    // 处理新节点相邻节点的指针指向
    if (node->prev != NULL) {
        node->prev->next = node;
    }

    if (node->next != NULL) {
        node->next->prev = node;
    }

    // 更新节点计数器
    list->len++;

    return list;
}

// 删除节点
void listDelNode(list *list, listNode *node) {

    // 调整前置节点指针
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    // 调整后置节点指针
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    // 释放节点
    if (list->free) list->free(node->value);
    zfree(node);

    // 更新节点数
    list->len--;

}

// 通过索引值获取节点
listNode *listIndex(list *list, long index) {

    listNode *n;
    // 负索引
    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    // 正索引
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }

    return n;
}

// 将表尾节点放到表头
void listRotate(list *list) {

    listNode *tail = list->tail;

    // 处理尾节点
    list->tail = tail->prev;
    list->tail->next = NULL;

    // 处理头节点
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}

void listRewindTail(list *list, listIter *iter) {
    iter->direction = AD_START_TAIL;
    iter->next = list->tail;
}

// 复制链表
list *listDup(list *orig) {

    list *copy;
    listNode *node;
    listIter *iter = listGetIterator(orig, AD_START_HEAD);

    // 创建一个空链表
    if ((copy = listCreate()) == NULL)
        return NULL;

    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    // 遍历添加节点
    while((node = listNext(iter)) != NULL) {

        void *value;
        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listReleaseIterator(iter);
                listRelease(copy);
                return NULL;
            }
        } else {
            value = node->value;
        }
        if (!listAddNodeTail(copy,value)) {
            listReleaseIterator(iter);
            listRelease(copy);
            return NULL;
        }
    }
    listReleaseIterator(iter);

    return copy;
}

#ifdef ZIPLIST_TEST_MAIN
/*----------------- debug -----------------*/
int keyMatch(void *p1, void *p2) {
    return (strcmp(p1,p2) == 0) ? 1 : 0;
}

void printList(list *li) {

    listNode *node;
    listIter *iter = listGetIterator(li,AD_START_HEAD);
    while ((node = listNext(iter)) != NULL) {
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
#endif