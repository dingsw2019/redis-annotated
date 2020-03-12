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

// 尾部添加节点
list *listAddNodeTail(list *list, void *value)
{
    // 申请内存空间
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 空添加
    if (list->len == 0) {
        node->prev = node->next = NULL;
        list->head = list->tail = node;
    }
    // 非空添加
    else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }

    // 更新节点数
    list->len++;

    return list;
}

// 指定节点前后添加节点
// after=0, 之前添加
// after=1, 之后添加
list *listInsertNode(list *list,listNode *old_node,void *value,int after)
{
    // 申请内存空间
    listNode *node;

    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    // 之后添加
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        // if (list->head == old_node) myerr 向前是头
        //     list->head = node;
        if (list->tail == old_node)
            list->tail = node;
    } else {
        node->prev = old_node->prev;
        node->next = old_node;
        // if (list->tail == old_node)
        //     list->tail = node;
        if (list->head == old_node) //myerr 向前是头
            list->head = node;
    }

    // 新节点左右节点的链接
    if (node->prev != NULL) {
        node->prev->next = node;
    }

    if (node->next != NULL) {
        node->next->prev = node;
    }

    list->len++;

    return list;
}

// 释放链表
void listRelease(list *list)
{
    unsigned long len = list->len;
    listNode *current,*next;

    current = list->head;
    while (len--) {
        next = current->next;
        // 释放节点值
        if (list->free)
            list->free(current->value);
        // 释放节点
        zfree(current);
        current = next;
    }

    // 释放链表
    zfree(list);
}

// 根据 value 搜索节点
listNode *listSearchKey(list *list, void *val)
{
    listIter *iter;
    listNode *node;

    if ((iter = listGetIterator(list,AL_START_HEAD)) == NULL)
        return NULL;
    
    while((node = listNext(iter)) != NULL) {
        // myerr
        // if (list->match(node->value,val)) {
        //     listReleaseIterator(iter);
        //     return node;
        // }
        if (list->match) {
            if (list->match(node->value,val)) {
                listReleaseIterator(iter);
                return node;
            }
        } else {
            if (node->value == val) {
                listReleaseIterator(iter);
                return node;
            }
        }
    }

    listReleaseIterator(iter);
    return NULL;
}

// 按索引查找节点
// 负索引是从后向前找
listNode *listIndex(list *list,long index)
{
    listNode *n;
    // 负索引转正索引
    if (index < 0) {
        // index = -index; myerr
        index = (-index)-1;
        n = list->tail;
        while(n && index--) n = n->prev;
    } else {
        n = list->head;
        while(index--) n = n->next;
    }

    return n;
}

// 删除节点
void listDelNode(list *list,listNode *node)
{
    if (list->tail == node){
        // node->prev->next = NULL; myerr 可删
        list->tail = node->prev;
    } else {
        node->next->prev = node->prev;
    }

    if (list->head == node) {
        // node->next->prev = NULL; myerr 可删
        list->head = node->next;
    } else {
        node->prev->next = node->next;
    }

    // myerr 缺少
    if (list->free) list->free(node->value);
    zfree(node);

    list->len--;
}

// 表尾节点移动到表头
void listRotate(list *list)
{
    listNode *tail = list->tail;
    // 表尾
    tail->prev->next = NULL;
    list->tail = tail->prev;

    // 表头
    list->head->prev = tail;
    tail->next = list->head;
    tail->prev = NULL;

    list->head = tail;
}

// 迭代器方向,从表尾开始
void listRewindTail(list *list, listIter *iter)
{
    iter->next = list->tail;
    iter->direction = AL_START_TAIL;
}

// 拷贝链表
list *listDup(list *orig)
{
    list *copy;
    listNode *node;
    listIter *iter;

    if ((copy = listCreate()) == NULL)
        return NULL;

    // 设置节点值处理函数 myerr 缺少
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    if ((iter = listGetIterator(orig,AL_START_HEAD)) == NULL)
        return NULL;

    // 遍历节点
    while ((node = listNext(iter)) != NULL) {
        void *val;
        // 复制值
        // if (orig->dup) { myerr
        if (copy->dup) {

            // val = orig->dup(node->value); myerr
            val = copy->dup(node->value);
            if (val == NULL) {
                listReleaseIterator(iter);
                listRelease(copy);
                return NULL;
            }
        }
        else 
            val = node->value;

        // 添加节点到新表
        if(listAddNodeTail(copy,val) == NULL) {
            listReleaseIterator(iter);
            listRelease(copy);
            return NULL;
        }
    }

    listReleaseIterator(iter);
    return copy;
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
    printf("\n");

    listReleaseIterator(iter);
}

int keyMatch(void *v1,void *v2)
{
    return (strcmp(v1,v2) == 0) ? 1 : 0;
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