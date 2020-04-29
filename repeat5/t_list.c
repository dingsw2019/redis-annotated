#include "redis.h"

/*-----------------------内部函数 ------------------------*/
/*----------------------- 迭代器 ------------------------*/
// 初始化迭代器
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction) {
    listTypeIterator *li = zmalloc(sizeof(*li));

    li->direction = direction;
    li->encoding = subject->encoding;
    li->subject = subject;

    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        li->zi = ziplistIndex(subject->ptr,index);

    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        li->ln = listIndex(subject->ptr, index);

    } else {
        redisPanic("Unknown list encoding");
    }

    return li;
}

void listTypeReleaseIterator(listTypeIterator *li) {
    zfree(li);
}

// 取出当前节点放去 entry, 并将指针指向下一个节点
// 还有节点返回 1, 否则返回 0
int listTypeNext(listTypeIterator *li, listTypeEntry *entry) {

    entry->li = li;
    
    if (li->encoding == REDIS_ENCODING_ZIPLIST) {

        entry->zi = li->zi;

        if (entry->zi != NULL) {
            if (li->direction == REDIS_HEAD) {
                li->zi = ziplistPrev(li->subject->ptr, li->zi);
            } else {
                li->zi = ziplistNext(li->subject->ptr, li->zi);
            }
            return 1;
        }

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {

        entry->ln = li->ln;

        if (entry->ln != NULL) {

            if (li->direction == REDIS_HEAD) {
                li->ln = li->ln->prev;
            } else {
                li->ln = li->ln->next;
            }
            return 1;
        }

    } else {
        redisPanic("Unknown list encoding");
    }

    return 0;
}

// 从节点中取出值, 已 robj 格式返回值
robj *listTypeGet(listTypeEntry *entry) {
    
    listTypeIterator *li = entry->li;
    robj *value;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr;
        int vlen;
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
    }

    return value;
}

/*----------------------- 编码转换 ------------------------*/
// enc 是要转换的编码, 将 ziplist 转换成 linkedlist
void listTypeConvert(robj *subject, int enc) {
    listTypeIterator *li;
    listTypeEntry entry;

    if (enc == REDIS_ENCODING_LINKEDLIST) {

        list *l = listCreate();

        li = listTypeInitIterator(subject, 0, REDIS_TAIL);
        while(listTypeNext(li,&entry)){
            listAddNodeTail(l, listTypeGet(&entry));
        }
        listTypeReleaseIterator(li);

        subject->encoding = REDIS_ENCODING_LINKEDLIST;

        zfree(subject->ptr);

        subject->ptr = l;

    } else {
        redisPanic("Unsupported list conversion");
    }
}

// 转码控制
void listTypeTryConversion(robj *subject, robj *value) {

    // 单个字符串长度超过限制, 转换编码
    if (subject->encoding != REDIS_ENCODING_ZIPLIST) return;

    if (sdsEncodedObject(value) && sdslen(value) > server.list_max_ziplist_value) {
        listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);
    }
}


/*----------------------- 基础数据操作函数 ------------------------*/

// 表头或表尾添加元素
void listTypePush(robj *subject, robj *value, int where) {

    // 尝试转换编码
    listTypeTryConversion(subject, value);

    // 元素数量超限制, 转码
    if (subject->encoding == REDIS_ENCODING_ZIPLIST && 
        ziplistLen(subject->ptr) > server.list_max_ziplist_entries) {
        
        listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);
    }

    // 添加元素
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {

        value = getDecodedObject(value);
        int pos = (where == REDIS_HEAD) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
        ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), pos);
        decrRefCount(value);

    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        if (where == REDIS_TAIL) {
            listAddNodeTail(subject->ptr, value);
        } else {
            listAddNodeHead(subject->ptr, value);
        }
        incrRefCount(value);

    } else {
        redisPanic("Unknown list encoding");
    }

}

// 在元素 entry 之前或之后添加元素 value
void listTypeInsert(listTypeEntry *entry, robj *value, int where) {

    robj *subject = entry->li->subject;

    if (entry->li->encoding == REDIS_ENCODING_ZIPLIST) {
        
        value = getDecodedObject(value);
        if (where == REDIS_TAIL) {

            unsigned char *p = ziplistNext(subject->ptr, entry->zi);
            if (p == NULL) {
                subject->ptr = ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), ZIPLIST_TAIL);
            } else {
                subject->ptr = ziplistInsert(subject->ptr, p, value->ptr, sdslen(value->ptr));
            }

        } else {
            subject->ptr = ziplistInsert(subject->ptr, entry->zi, value->ptr, sdslen(value->ptr));
        }

        decrRefCount(value);

    } else if (entry->li->encoding == REDIS_ENCODING_LINKEDLIST) {

        if (where == REDIS_TAIL) {
            listInsertNode(subject->ptr, entry->ln, value, AL_START_TAIL);
        } else {
            listInsertNode(subject->ptr, entry->ln, value, AL_START_HEAD);
        }
        incrRefCount(value);

    } else {
        redisPanic("Unknown list encoding");
    }

}

// 弹出元素, 并以 robj 格式返回
robj *listTypePop(robj *subject, int where) {
    robj *value;

    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        int pos = (where == REDIS_HEAD) ? 0 : -1;
        unsigned char *p = ziplistIndex((char*)subject->ptr,pos);
        char *vstr;
        unsigned int vlen;
        long long vlong;
        if (ziplistGet(p, &vstr, &vlen, &vlong)) {
            if (vstr) {
                value = createStringObject((char*)vstr, vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
        }
        subject->ptr = ziplistDelete(subject->ptr, &p);

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
            incrRefCount(value);
            listDelNode(subject->ptr, ln);
        }

    } else {
        redisPanic("Unknown list encoding");
    }

    return value;
}

// 删除 entry 节点, 同时移动迭代器的指针
void listTypeDelete(listTypeEntry *entry) {

    listTypeIterator *li = entry->li;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p = entry->zi;

        li->subject->ptr = ziplistDelete(li->subject->ptr, &p);

        if (li->direction == REDIS_TAIL) {
            li->zi = p;
        } else {
            li->zi = ziplistPrev(li->subject->ptr, p);
        }

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        listNode *next;

        if (li->direction == REDIS_TAIL) {
            next = li->ln->next;
        } else {
            next = li->ln->prev;
        }

        listDelNode(li->subject->ptr, entry->ln);

        li->ln = next;

    } else {
        redisPanic("Unknown list encoding");
    }
}

int listTypeEqual(listTypeEntry *entry, robj *o) {
    listTypeIterator *li = entry->li;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        redisAssertWithInfo(NULL, o, sdsEncodedObject(o));
        return ziplistCompare(entry->zi, o->ptr, sdslen(o->ptr));

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        return equalStringObjects(o,listNodeValue(entry->ln));

    } else {
        redisPanic("Unknown list encoding");
    }
}

unsigned long listTypeLength(robj *subject) {

    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        return ziplistLen(subject->ptr);

    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        return listLength((list*)subject->ptr);

    } else {
        redisPanic("Unknown list encoding");
    }
}

/*----------------------- List 命令函数 ------------------------*/