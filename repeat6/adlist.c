#include "adlist.h"

// 创建并返回一个空链表
list *listCreate(void)
{
    // 申请内存空间
    // list *li = zmalloc(sizeof(*li));
    list *li;
    if ((li = zmalloc(sizeof(*li))) == NULL)
        return NULL;
    // 初始化属性
    li->len = 0;
    li->head = li->tail = NULL;
    li->dup = NULL;
    li->free = NULL;
    li->match = NULL;

    return li;
}

// 头部添加节点
list *listAddNodeHead(list *list,void *value)
{   
    // 申请节点内存
    // listNode *node = zmalloc(sizeof(*node));
    listNode *node;
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 空添加
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } 
    // 非空添加
    else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    // 更新节点数
    list->len++;

    return list;
}

// 获取迭代器
listIter *listGetIterator(list *list,int direction)
{
    // listIter *iter = zmalloc(sizeof(*iter));
    listIter *iter;
    if((iter = zmalloc(sizeof(*iter))) == NULL)
        return NULL;
        
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else 
        iter->next = list->tail;
    iter->direction = direction;

    return iter;
}

// 执行迭代器
listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;

    if (current != NULL) {

        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }

    return current;
}

// 释放迭代器
void listReleaseIterator(listIter *iter)
{
    zfree(iter);
}

/*----------------- debug -----------------*/
void printList(list *list)
{
    listIter *iter = listGetIterator(list,AL_START_HEAD);
    listNode *node;
    printf("list size is %d, elements: ",listLength(list));
    while ((node = listNext(iter)) != NULL) {
        printf("%s ",(char*)node->value);
    }

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

    // listRelease(li);
    // li = listCreate();
    // // 表尾添加, 结果：li size is 4, elements:believe it or not
    // for (int i = 0; i < sizeof(b)/sizeof(*b); ++i) {
    //     listAddNodeTail(li, b[i]);
    // }
    // printf("listAddNodeTail : ");
    // printList(li);

    // printf("search a key :");
    // listSetMatchMethod(li, keyMatch);
    // listNode *ln = listSearchKey(li, "it");
    // if (ln != NULL) {
    //     printf("find key is :%s\n", (char*)ln->value);
    // } else {
    //     printf("not found\n");
    // }

    // // 插入节点
    // li = listInsertNode(li,ln,"insert1",1);
    // printList(li);

    // // 插入头节点
    // printf("head insert node: ");
    // ln = listSearchKey(li,"believe");
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
    // ln = listIndex(li,-1);
    // if (ln) {
    //     printf("listIndex : %s \n",ln->value);
    // } else {
    //     printf("listIndex : NULL");
    // }

    // // 表尾变表头
    // listRotate(li);
    // printList(li);

    // // 反转链表
    // printf("reverse output the list : ");
    // printf("li size is %d, elements:", listLength(li));
    // listRewindTail(li, &iter);
    // while ((node = listNext(&iter)) != NULL) {
    //     printf("%s ", (char*)node->value);
    // }
    // printf("\n");

    // // 复制链表
    // printf("duplicate a new list : ");
    // list *lidup = listDup(li);
    // printList(lidup);

    // listRelease(li);

    return 0;
}