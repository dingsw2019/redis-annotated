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

/*----------------------- List 命令函数 ------------------------*/

/**
 * lpush, rpush的通用函数
 * 添加成功, 回复客户端添加后的节点数量
 * 添加失败, 回复客户端错误, 终止程序
 */
void pushGenericCommand(redisClient *c, int where) {
    int j, waiting = 0, pushed = 0;

    // 获取 key 的值
    robj *lobj = lookupKeyWrite(c->db, c->argv[1]);

    // 不存在值, 可能有客户端在等待这个键的出现
    int may_have_waiting_clients = (lobj == NULL);

    // 检查值对象类型
    if (lobj && lobj->type != REDIS_LIST) {
        addReply(c, shared.wrongtypeerr);
        return ;
    }

    // 将列表状态设置为就绪
    if (may_have_waiting_clients) signalListAsReady(c, c->argv[1]);

    // 向列表对象中添加节点值
    for (j=2; j<c->argc; j++) {

        // 压缩节点值剩余空间
        c->argv[j] = getDecodedObject(c->argv[j]);

        // 如果列表键不存在, 创建一个
        if (!lobj) {
            lobj = createZiplistObject();
            dbAdd(c->db, c->argv[1], c->argv[j]);
        }

        // 添加节点
        listTypePush(lobj, c->argv[j], where);
        pushed++;
    }

    // 返回节点数量
    addReplyLongLong(c, waiting + (lobj ? listTypeLength(lobj) : 0));

    // 只要有一个节点添加成功,
    if (pushed) {
        char *event = (where == REDIS_HEAD) ? "lpush" : "rpush";

        // 发送键被修改的信号
        signalModifiedKey(c->db, c->argv[1]);

        // 发送事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST, event, c->argv[1], c->db->id);
    }

    // 更新修改次数
    server.dirty += pushed;
}

void lpushCommand(redisClient *c) {
    pushGenericCommand(c, REDIS_HEAD);   
}

void rpushCommand(redisClient *c) {
    pushGenericCommand(c, REDIS_TAIL);   
}

/**
 * LPUSHX / RPUSHX 无需传入 *refval, 将 val 添加到列表的表头或表尾
 * LINSERT 将 value 添加到 refval 之前或之后
 */
void pushxGenericCommand(redisClient *c, robj *refval, robj *val, int where) {
    robj *subject;
    listTypeIterator *iter;
    listTypeEntry entry;
    int inserted = 0;

    // 如果列表键不存在或者不是列表类型, 返回
    if ((subject = lookupKeyReadOrReply(c, c->argv[1], shared.czero)) == NULL ||
        checkType(c,subject,REDIS_LIST)) {
            return;
    }

    // 执行的是 LINSERT 命令
    if (refval != NULL) {
        // 判断值的长度是否超过限制而转码
        listTypeTryConversion(subject, val);

        // 获取指定节点的
        iter = listTypeInitIterator(subject, 0, REDIS_TAIL);
        while(listTypeNext(iter, &entry)) {
            if (listTypeEqual(&entry, refval)) {
                // 找到指定节点, 将值添加到该节点之前或之后
                listTypeInsert(&entry,val, where);
                inserted = 1;
                break;
            }
        }
        listTypeReleaseIterator(iter);

        // 添加新节点
        if (inserted) {
            // 检查节点数量是否达到转码标准
            if (subject->encoding == REDIS_ENCODING_ZIPLIST && 
                ziplistLen(subject->ptr) > server.list_max_ziplist_entries) {
                    listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);
            }

            // 发送键被修改的信号
            signalModifiedKey(c->db, c->argv[1]);
            // 发送事件通知
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST, "linsert", c->argv[1], c->db->id);
            server.dirty++;

        // 未添加新节点, 回复客户端添加失败
        } else {
            addReply(c, shared.cnegone);
            return;
        }

    // 执行的是 LPUSHX 或 RPUSHX 命令
    } else {
        char *event = (where == REDIS_HEAD) ? "lpushx" : "rpushx";

        // 添加节点
        listTypePush(subject, val, where);

        // 发送键被修改的信号
        signalModifiedKey(c->db, c->argv[1]);

        // 发送事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST, event,c->argv[1] ,c->db->id);

        // 更新修改次数
        server.dirty++;
    }

    // 回复客户端节点的数量
    addReplyLongLong(c, listTypeLength(subject));
}

// value 只有一个, LPUSH key value
void lpushxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    pushxGenericCommand(c, NULL, c->argv[2], REDIS_HEAD);
}

void rpushxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    pushxGenericCommand(c, NULL, c->argv[2], REDIS_TAIL);
}

//LINSERT KEY_NAME BEFORE EXISTING_VALUE NEW_VALUE 
void linsertCommand(redisClient *c) {

    c->argv[4] = tryObjectEncoding(c->argv[4]);

    if (strcasecmp(c->argv[2]->ptr, "after") == 0) {
        pushxGenericCommand(c, c->argv[3], c->argv[4], REDIS_TAIL);

    } else if (strcasecmp(c->argv[2]->ptr, "before") == 0) {
        pushxGenericCommand(c, c->argv[3], c->argv[4], REDIS_HEAD);

    } else {
        addReply(c, shared.syntaxerr);
    }
}

void llenCommand(redisClient *c) {

    // 获取列表对象
    robj *o = lookupKeyReadOrReply(c, c->argv[1], shared.czero);

    if (o == NULL || checkType(c, o, REDIS_LIST)) return;

    // 回复客户端, 节点数量
    addReplyLongLong(c, listTypeLength(o));
}

// LINDEX KEY_NAME INDEX_POSITION
void lindexCommand(redisClient *c) {

    // 获取列表值
    robj *o = lookupKeyReadOrReply(c, c->argv[1], shared.czero);
    if (o == NULL || checkType(c, o, REDIS_LIST)) return;

    long index;
    robj *value = NULL;

    // 获取 index 值
    if ((getLongFromObjectOrReply(c, c->argv[3], &index, NULL)) != REDIS_OK)
        return;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        
        p = ziplistIndex(o->ptr, index);

        if (ziplistGet(p, &vstr, &vlen, &vlong)) {

            if (vstr) {
                value = createStringObject(vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }
            addReplyBulk(c, value);
            decrRefCount(value);
        
        } else {
            addReply(c, shared.nullbulk);
        }
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        listNode *ln = listIndex(o->ptr, index);

        if (ln != NULL) {

            addReplyBulk(c,listNodeValue(ln));
        } else {
            addReply(c, shared.nullbulk);
        }

    } else {
        redisPanic("Unknown list encoding");
    }
}

// LSET KEY_NAME INDEX VALUE
void lsetCommand(redisClient *c) {

    // 获取列表键对象, 检查键的类型是否为列表
    robj *o = lookupKeyWriteOrReply(c, c->argv[1], shared.nokeyerr);
    if (o == NULL || checkType(c, o, REDIS_LIST)) return;

    long index;
    // 取出值
    robj *value = (c->argv[3] = tryObjectEncoding(c->argv[3]));
    // 取出索引
    if (getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != REDIS_OK)
        return;

    // value 是否需要转码
    listTypeTryConversion(o, value);

    // 压缩列表
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p, *zl = o->ptr;

        // 获取 index 指向的值
        p = ziplistIndex(zl, index);

        if (p == NULL) {
            addReply(c, shared.outofrangeerr);
        } else {
            // 删除旧值
            o->ptr = ziplistDelete(o->ptr, &p);
            // 添加新值
            value = getDecodedObject(value);
            o->ptr = ziplistInsert(o->ptr, p, value->ptr, sdslen(value->ptr));
            // getDecodeObject对value进行了incrRef操作
            // 所以这里执行 decrRef
            decrRefCount(value);

            addReply(c, shared.ok);
            signalModifiedKey(c->db, c->argv[1]);
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST, "lset", c->argv[1], c->db->id);
            server.dirty++;
        }

    // 双端链表
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {

        // 获取 index 指向的值
        listNode *ln = listIndex(o->ptr, index);

        if (ln == NULL) {
            addReply(c, shared.outofrangeerr);
        } else {
            // 删除旧节点
            decrRefCount((robj*)listNodeValue(ln));
            // 指向新节点
            listNodeValue(ln) = value;
            incrRefCount(value);

            addReply(c, shared.ok);
            signalModifiedKey(c->db, c->argv[1]);
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST, "lset", c->argv[1], c->db->id);
            server.dirty++;
        }

    } else {
        redisPanic("Unknown list encoding");
    }

}

void popGenericCommand(redisClient *c, int where) {

    // 获取列表值
    robj *o = lookupKeyWriteOrReply(c, c->argv[1], shared.nullbulk);
    if (o == NULL || checkType(c, o, REDIS_LIST))
        return;

    robj *value = listTypePop(o, where);

    if (value == NULL) {
        addReply(c, shared.nullbulk);
    } else {
        char *event = (where == REDIS_HEAD) ? "lpop" : "rpop";

        // 回复客户端删除的值
        addReply(c,value);
        decrRefCount(value);

        // 发送事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST, event, c->argv[1], c->db->id);

        // 列表对象空了, 从 db 中删除这个列表键
        if (listTypeLength(o) == 0) {
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC, "del", c->argv[1], c->db->id);
            dbDelete(c->db, c->argv[1]);
        }

        signalModifiedKey(c->db, c->argv[1]);

        server.dirty++;
    }
}

void lpopCommand(redisClient *c) {
    popGenericCommand(c, REDIS_HEAD);   
}

void rpopCommand(redisClient *c) {
    popGenericCommand(c, REDIS_TAIL);   
}

void signalListAsReady(redisClient *c, robj *key) {

}