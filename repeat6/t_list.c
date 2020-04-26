#include "redis.h"

/*-----------------------内部函数 ------------------------*/
/*----------------------- 迭代器 ------------------------*/

// 创建并返回一个列表迭代器, 处理不同编码的列表
// index 指定当前的节点, subject是列表的首地址
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction) {
    
    listTypeIterator *li = zmalloc(sizeof(listTypeIterator));

    li->subject = subject;
    li->encoding = subject->encoding;
    li->direction = direction;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        li->zi = ziplistIndex(li->subject->ptr, index);

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        li->ln = listIndex(li->subject->ptr, index);

    } else {
        redisPanic("Unknown list encoding");
        return;
    }

    return li;
}

void listTypeReleaseIterator(listTypeIterator *li ) {
    zfree(li);
}

// 将当前节点放入 *entry, li 的节点指针移动一次
// 如果返回 1, 表示还可移动
// 返回 0, 表示无法移动
int listTypeNext(listTypeIterator *li, listTypeEntry *entry) {

    entry->li = li;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        entry->zi = li->zi;
        if (entry->zi != NULL) {
            if (li->direction == REDIS_TAIL) {
                li->zi = ziplistNext(li->subject->ptr, li->zi);
            } else {
                li->zi = ziplistPrev(li->subject->ptr, li->zi);
            }
            return 1;
        }

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        entry->ln = li->ln;
        if (entry->ln != NULL) {
            if (li->direction == REDIS_TAIL) {
                li->ln = li->ln->next;
            } else {
                li->ln = li->ln->prev;
            }
            return 1;
        }

    } else {
        redisPanic("Unknown list encoding");
        return;
    }

    return 0;
}

// 从节点中提取值, 并填充到 robj 中返回
robj *listTypeGet(listTypeEntry *entry) {
    listTypeIterator *li = entry->li;
    robj *value = NULL;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        redisAssert(entry->zi != NULL);
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        if (ziplistGet(entry->zi, &vstr, &vlen, &vlong)) {

            if (vstr) {
                value = createStringObject((char*)vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
        }

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        redisAssert(entry->ln != NULL);
        value = listNodeValue(entry->ln);
        incrRefCount(value);

    } else {
        redisPanic("Unknown list encoding");
        return;   
    }

    return value;
}