/**
 * 
 * 压缩列表中存储的是值（字符串、数字）
 * 双端链表中存储的是Redis对象的指针 (robj)
 */

#include "redis.h"

/*-----------------------内部函数 ------------------------*/
/*----------------------- 迭代器 ------------------------*/
/**
 * 创建并返回一个列表迭代器
 * - index, 迭代的起始节点的索引
 * - direction, 迭代方向
 */
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction) {

    // 申请迭代器, 初始化属性
    listTypeIterator *li = zmalloc(sizeof(listTypeIterator));

    li->subject = subject;
    li->encoding = subject->encoding;
    li->direction = direction;

    // 压缩列表
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        li->zi = ziplistIndex(subject->ptr, index);

    // 双端链表
    } else if(li->encoding == REDIS_ENCODING_LINKEDLIST) {
        li->ln = listIndex(subject->ptr, index);

    // 位置编码
    } else {
        redisPanic("Unknown list encoding");
    }

    return li;
}

/**
 * 释放迭代器
 */
void listTypeReleaseIterator(listTypeIterator *li) {
    zfree(li);
}

/**
 * 返回当前节点, 将指针移向下一个节点
 * 存在可迭代元素返回 1, 否则返回 0
 */
int listTypeNext(listTypeIterator *li, listTypeEntry *entry) {

    entry->li = li;

    // 压缩列表
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {

        // 当前节点
        entry->zi = li->zi;

        // 移动指针到下一个节点
        if (entry->zi != NULL) {

            if (li->direction == REDIS_TAIL) {
                li->zi = ziplistNext(li->subject->ptr,li->zi);
            } else {
                li->zi = ziplistPrev(li->subject->ptr,li->zi);
            }
            return 1;
        }

    // 双端链表
    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {

        // 当前节点
        entry->ln = li->ln;

        // 移动指针到下一个节点
        if (entry->ln != NULL) {
            if (li->direction == REDIS_TAIL) {
                li->ln = li->ln->next;
            } else {
                li->ln = li->ln->prev;
            }
            return 1;
        }
    
    // 未知编码
    } else {
        redisPanic("Unknown list encoding");
    }

    return 0;
}

/**
 * 从列表节点中提取值, 并放到Redis对象结构中返回
 * 如果节点中无值, 返回 NULL
 */
robj *listTypeGet(listTypeEntry *entry) {
    listTypeIterator *li = entry->li;
    robj *value = NULL;

    // 压缩列表
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        redisAssert(entry->zi != NULL);
        // 提取节点值
        if (ziplistGet(entry->zi,&vstr,&vlen,&vlong)) {

            // 值为字符串
            if (vstr) {
                value = createStringObject((char*)vstr,vlen);

            // 值为数字
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
        }

    // 双端链表
    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        redisAssert(entry->ln != NULL);
        value = listNodeValue(entry->ln);
        incrRefCount(value);

    } else {
        redisPanic("Unknown list encoding");
    }

    return value;
}

/*----------------------- 编码转换 ------------------------*/
/**
 * 将列表的编码从 压缩列表 转换成 双端链表
 */
void listTypeConvert(robj *subject, int enc) {

    listTypeIterator *li;
    listTypeEntry entry;

    redisAssertWithInfo(NULL, subject, subject->type == REDIS_LIST);

    // 转换成双端链表
    if (enc == REDIS_ENCODING_LINKEDLIST) {

        // 创建一个空链表
        list *l = listCreate();

        listSetFreeMethod(l, decrRefCountVoid);

        // 遍历压缩列表, 迁移节点到双端链表
        li = listTypeInitIterator(subject, 0, REDIS_TAIL);
        while (listTypeNext(li,&entry)) listAddNodeTail(l, listTypeGet(&entry));
        listTypeReleaseIterator(li);

        // 更新编码
        subject->encoding = REDIS_ENCODING_LINKEDLIST;

        // 释放原值
        zfree(subject->ptr);

        // 更新值指针
        subject->ptr = l;

    } else {
        redisPanic("Unsupported list conversion");
    }
}

/**
 * 确保 value 不超过单节点最大内存空间限制
 * 值对 raw 编码检查, 因为整数编码不可能超过最大内存限制
 */
void listTypeTryConversion(robj *subject, robj *value) {

    // 确保只有 ziplist 才会执行检查
    if (subject->encoding != REDIS_ENCODING_ZIPLIST) return;

    if (sdsEncodedObject(value) && 
        sdslen(value) > server.list_max_ziplist_value) {
            // 将编码转换成双端链表
            listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);
    }
}

/*----------------------- 基础数据操作函数 ------------------------*/
/**
 * 将 value 添加到列表
 * where 控制添加方向
 * - REDIS_HEAD 表头添加
 * - REDIS_TAIL 表尾添加
 */
void listTypePush(robj *subject, robj *value, int where) {

    // value 超过最大长度限制, 导致转换编码
    listTypeTryConversion(subject, value);

    // 压缩列表节点超过最大限制, 导致转换编码
    if (subject->encoding == REDIS_ENCODING_ZIPLIST &&
        ziplistLen(subject->ptr) >= server.list_max_ziplist_entries) {
            listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);
    }

    // 压缩列表编码
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        int pos = (where == REDIS_HEAD) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
        // 将整型值转为字符串, 方便计算长度
        value = getDecodedObject(value);
        ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), pos);
        decrRefCount(value);
    
    // 双端链表编码
    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {

        if (where == REDIS_HEAD) {
            listAddNodeHead(subject->ptr, value);
        } else {
            listAddNodeTail(subject->ptr, value);
        }
        incrRefCount(value);

    // 未知编码
    } else {
        redisPanic("Unknown list encoding");
    }
}

/**
 * 列表节点数
 */
unsigned long listTypeLength(robj *subject) {

    // 压缩列表
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        return ziplistLen(subject->ptr);

    // 双端链表
    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        return listLength((list*)subject->ptr);

    // 未知编码
    } else {
        redisPanic("Unknown list encoding");
    }
}


