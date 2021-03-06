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
        li->zi = ziplistIndex(subject->ptr, index);

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        li->ln = listIndex(subject->ptr, index);

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
    }

    return 0;
}

// 从节点中提取值, 并填充到 robj 中返回
robj *listTypeGet(listTypeEntry *entry) {
    listTypeIterator *li = entry->li;
    robj *value = NULL;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        redisAssert(entry->zi != NULL);

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
// 编码转换, subject是列表, enc 是要转换的编码
void listTypeConvert(robj *subject, int enc) {
    listTypeIterator *li;
    listTypeEntry entry;

    redisAssertWithInfo(NULL, subject, subject->type == REDIS_LIST);

    if (enc == REDIS_ENCODING_LINKEDLIST) {

        list *l = listCreate();

        listSetFreeMethod(l, decrRefCountVoid);

        // 遍历元素, 将所有元素移动到列表中
        li = listTypeInitIterator(subject, 0, REDIS_TAIL);
        while(listTypeNext(li,&entry)) listAddNodeTail(l, listTypeGet(&entry));
        listTypeReleaseIterator(li);

        // 变更编码
        subject->encoding = REDIS_ENCODING_LINKEDLIST;

        // 释放原结构
        zfree(subject->ptr);

        // 挂载新结构
        subject->ptr = l;
    } else {
        redisPanic("Unsupported list conversion");
    }
}

// 编码转换控制, ziplist编码的单元素长度超过 64字节时, 转换编码
void listTypeTryConversion(robj *subject, robj *value) {

    if (subject->encoding != REDIS_ENCODING_ZIPLIST) return;

    if (sdsEncodedObject(value) &&
        sdslen(value) > server.list_max_ziplist_value) {
        
        listTypeConvert(subject,REDIS_ENCODING_LINKEDLIST);
    }
}

/*----------------------- 基础数据操作函数 ------------------------*/

void listTypePush(robj *subject, robj *value, int where) {

    // 尝试转码
    listTypeTryConversion(subject, value);

    // 元素超过限制, 转码
    if (subject->encoding == REDIS_ENCODING_ZIPLIST && 
        ziplistLen(subject->ptr) >= server.list_max_ziplist_entries) {
            listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);
    }

    // ziplist
    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        int pos = (where == REDIS_TAIL) ? ZIPLIST_TAIL : ZIPLIST_HEAD;
        // 将 value 解码, 统一变成字符串格式
        value = getDecodedObject(value);
        // 添加
        ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), pos);
        // 释放 value
        decrRefCount(value);

    // linkedlist
    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {

        // 添加
        if (where == REDIS_TAIL) {
            listAddNodeTail(subject->ptr, value);
        } else {
            listAddNodeHead(subject->ptr, value);
        }
        // 增加 value 引用计数
        incrRefCount(value);
    
    // 未知编码
    } else {
        redisPanic("Unknown list encoding");
    }
}

// 指定元素之前或之后添加元素
void listTypeInsert(listTypeEntry *entry, robj *value, int where) {

    robj *subject = entry->li->subject;

    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        value = getDecodedObject(value);

        if (where == REDIS_TAIL) {

            unsigned char *next = ziplistNext(subject->ptr, entry->zi);
            if (next == NULL) {
                ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), REDIS_TAIL);
            } else {
                ziplistInsert(subject->ptr, next, value->ptr,sdslen(value->ptr));
            }
        } else {
             ziplistInsert(subject->ptr, entry->zi, value->ptr,sdslen(value->ptr));
        }
        decrRefCount(value);

    } else if (subject->encoding == REDIS_ENCODING_LINKEDLIST) {
        if (where == REDIS_TAIL) {
            listInsertNode(subject->ptr,entry->ln,value->ptr,AD_START_TAIL);
        } else {
            listInsertNode(subject->ptr,entry->ln,value->ptr,AD_START_HEAD);
        }
        incrRefCount(value);

    } else {
        redisPanic("Unknown list encoding");
    }
}

// 从指定方向弹出一个元素, 并将元素值以 robj 结构返回
robj *listTypePop(robj *subject, int where) {
    robj *value;


    if (subject->encoding == REDIS_ENCODING_ZIPLIST) {
        int pos = (where == REDIS_HEAD) ? 0 : -1;
        unsigned char *p = ziplistIndex((char*)subject->ptr, pos);
        char *vstr;
        unsigned int vlen;
        long long vlong;
        if (ziplistGet(p,&vstr,&vlen,&vlong)) {
            if (vstr) {
                value = createStringObject((char*)vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
            subject->ptr = ziplistDelete(subject->ptr, &p);
        }

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
            listDelNode(list,ln);
        }

    } else {
        redisPanic("Unknown list encoding");
    }

    return value;
}

// 删除当前节点, 并处理迭代器指向的节点
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

// 比对值
int listTypeEqual(listTypeEntry *entry, robj *o) {

    listTypeIterator *li = entry->li;

    if (li->encoding == REDIS_ENCODING_ZIPLIST) {
        redisAssertWithInfo(NULL, o, sdsEncodedObject(o));
        return ziplistCompare(entry->zi, o->ptr, sdslen(o->ptr));

    } else if (li->encoding == REDIS_ENCODING_LINKEDLIST) {
        return equalStringObjects(o, listNodeValue(entry->ln));

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

// push 的通用函数
void pushGenericCommand(redisClient *c, int where) {
    int j, waiting = 0, pushed = 0;
    // 获取值对象
    robj *lobj = lookupKeyWrite(c->db, c->argv[1]);

    int may_have_waiting_clients = (lobj == NULL);

    // 检查值类型
    if (lobj && lobj->type != REDIS_LIST) {
        addReply(c, shared.wrongtypeerr);
        return;
    }

    if (may_have_waiting_clients) signalListAsReady(c, c->argv[1]);

    // 添加元素
    for (j=2; j<c->argc; j++) {

        // 如果值对象不存在, 创建一个
        if (!lobj) {
            lobj = createZiplistObject();
            dbAdd(c->db, c->argv[1], lobj);
        }

        // 添加元素
        c->argv[j] = tryObjectEncoding(c->argv[j]);
        listTypePush(lobj, c->argv[j], where);

        // 记录添加的元素数量
        pushed++;
    }

    // 回复客户端添加元素数量
    addReplyLongLong(c, waiting + (lobj ? listTypeLength(lobj) : 0));

    // 发送通知
    if (pushed) {
        char *event = (where == REDIS_HEAD) ? "lpush" : "rpush";

        // 键改通知
        signalModifiedKey(c->db, c->argv[1]);
        // 事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST, event, c->argv[1], c->db->id);
    }

    // 键改次数
    server.dirty += pushed;
}

void lpushCommand(redisClient *c) {
    pushGenericCommand(c, REDIS_HEAD);
}

void rpushCommand(redisClient *c) {
    pushGenericCommand(c, REDIS_TAIL);
}