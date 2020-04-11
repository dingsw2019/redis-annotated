/**
 * 
 * 列表值(subject)的首地址以 robj 格式保存在 Db 中, 
 * 列表值可以是以下两种数据结构
 *  - 压缩列表, 提取调用方传入 robj 格式中的值(字符串、数字), 
 *             以字符串、数字的内容存入压缩列表的节点
 *  - 双端链表, 直接将调用方传入的 robj 存入链表节点
 * 
 * 将 subject->ptr 看成 ziplist 或 linkedlist 的首地址
 * 当 subject->ptr 是 ziplist 时, ziplist->node 是字符串或数字
 * 当 subject->ptr 是 linkedlist 时, linkedlist->node 是 robj 结构的值
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
 * 从列表的表头或表尾弹出一个节点
 * 以 robj 格式返回弹出的节点值
 * where 控制弹出方向
 * - REDIS_HEAD 表头添加
 * - REDIS_TAIL 表尾添加
 */
robj *listTypePop(robj *subject, int where) {

    robj *value;

    // 压缩列表弹出元素
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        char *vstr;
        unsigned int vlen;
        long long vlong;

        // 获取节点
        int pos = (where == REDIS_HEAD) ? 0 : -1;
        unsigned char *p = ziplistIndex((char*)subject->ptr,pos);

        // 节点值填充到 value
        if (ziplistGet(p, &vstr, &vlen, &vlong)) {
            if (vstr) {
                value = createStringObject((char*)vstr, vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
            subject->ptr = ziplistDelete(subject->ptr, &p);
        }

    // 双端链表弹出元素
    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        list *list = subject->ptr;
        listNode *ln;

        if (where == REDIS_HEAD) {
            ln = listFirst(list);
        } else {
            ln = listLast(list);
        }

        if (ln != NULL) {
            value = listNodeValue(ln);
            // ?? 为什么 ziplist 里面没有增加引用计数
            incrRefCount(value);
            listDelNode(list, ln);
        }

    } else {
        redisPanic("Unknown list encoding");
    }

    return value;
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

/**
 * 将对象 value 添加到指定节点的之前或之后
 * where 控制添加方向
 * - REDIS_HEAD 指定节点前
 * - REDIS_TAIL 指定节点后
 */
void listTypeInsert(listTypeEntry *entry, robj *value, int where) {

    robj *subject = entry->li->subject;

    // 压缩列表
    if (entry->li->encoding == REDIS_ENCODING_ZIPLIST) {

        value = getDecodedObject(value);
        // entry 之后添加
        if (where == REDIS_TAIL) {
            // 获取 entry 的下一个节点
            unsigned char *next = ziplistNext(subject->ptr, entry->zi);

            // 下一个节点不存在, 从尾部添加
            if (next == NULL) {
                subject->ptr = ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), REDIS_TAIL);

            // 添加在 next 之前
            } else {
                subject->ptr = ziplistInsert(subject->ptr, next, value->ptr, sdslen(value->ptr));
            }

        // entry 之前添加
        } else {
            subject->ptr = ziplistInsert(subject->ptr, entry->zi, value->ptr, sdslen(value->ptr));
        }
        decrRefCount(value);

    // 双端链表
    } else if (entry->li->encoding == REDIS_ENCODING_LINKEDLIST) {

        if (where == REDIS_TAIL) {
            listInsertNode(subject->ptr, entry->ln, value, AL_START_TAIL);
        } else {
            listInsertNode(subject->ptr, entry->ln, value, AL_START_HEAD);
        }
        incrRefCount(value);

    // 未知编码
    } else {
        redisPanic("Unknown list encoding");
    }
}

/**
 * 将节点的值与对象 o 的值进行比对
 * 相同返回 1, 否则返回 0
 */
int listTypeEqual(listTypeEntry *entry, robj *o) {

    listTypeIterator *li = entry->li;

    // 压缩列表
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        redisAssertWithInfo(NULL,o,sdsEncodedObject(o));
        return ziplistCompare(entry->zi, o->ptr, sdslen(o->ptr));

    // 双端链表
    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        return equalStringObjects(o, listNodeValue(entry->ln));

    // 未知编码
    } else {
        redisPanic("Unknown list encoding");
    }
}

/**
 * 删除 entry 指向的节点
 */
void listTypeDelete(listTypeEntry *entry) {

    listTypeIterator *li = entry->li;

    // 压缩列表
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p = entry->zi;

        li->subject->ptr = ziplistDelete(li->subject->ptr, &p);

        // 删除节点之后, 更新迭代器的指针
        if (li->direction == REDIS_TAIL) {
            li->zi = p;
        } else {
            li->zi = ziplistPrev(li->subject->ptr, p);
        }

    // 双端链表
    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {

        // 记录移动的节点
        listNode *next;

        if (li->direction == REDIS_TAIL) {
            next = li->ln->next;
        } else {
            next = li->ln->prev;
        }

        listDelNode(li->subject->ptr, entry->ln);

        // 更新节点指针指向
        li->ln = next;
        
    // 未知编码
    } else {
        redisPanic("Unknown list encoding");
    }
}