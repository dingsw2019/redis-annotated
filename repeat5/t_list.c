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

void pushGenericCommand(redisClient *c, int where) {
    int j, waiting = 0, pushed = 0;

    // 获取值对象
    robj *lobj = lookupKeyWrite(c->db, c->argv[1]);

    
    // 如果列表对象不存在，那么可能有客户端在等待这个键的出现
    int may_have_waiting_clients = (lobj == NULL);

    if (lobj != NULL && lobj->type != REDIS_LIST) {
        addReply(c, shared.wrongtypeerr);
        return;
    }

    // 将列表状态设置为就绪
    if (may_have_waiting_clients) signalListAsReady(c,c->argv[1]);

    // 批量添加值到链表
    for (j=2; j<c->argc; j++) {

        // 如果值对象不存在, 创建一个
        if (lobj == NULL) {
            lobj = createZiplistObject();
            dbAdd(c->db, c->argv[1], lobj);
        }

        // 添加元素
        c->argv[j] = tryObjectEncoding(c->argv[j]);
        listTypePush(lobj, c->argv[j], where);

        pushed++;
    }

    addReplyLongLong(c, waiting + (lobj ? listTypeLength(lobj) : 0));

    // 发送通知
    if (pushed) {
        char *event = (where == REDIS_HEAD) ? "lpush" : "rpush";

        signalModifiedKey(c->db, c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_LIST,event,c->argv[1],c->db->id);
    }



    // 更新键改次数
    server.dirty += pushed;
}

void lpushCommand(redisClient *c) {
    pushGenericCommand(c, REDIS_HEAD);
}

void rpushCommand(redisClient *c) {
    pushGenericCommand(c,REDIS_TAIL);
}

// 如果值对象不存在, 不执行操作
// lpushx rpushx linsert 的通用方法
void pushxGenericCommand(redisClient *c, robj *refval, robj *val, int where) {
    robj *subject;
    listTypeIterator *iter;
    listTypeEntry entry;
    int inserted = 0;

    // 获取值对象
    if((subject = lookupKeyReadOrReply(c, c->argv[1], shared.czero)) == NULL ||
        checkType(c, subject, REDIS_LIST)) return;

    // INSERT
    if (refval != NULL) {

        // 遍历查找目标元素
        iter = listTypeInitIterator(subject, 0, REDIS_HEAD);
        while(listTypeNext(iter, &entry)) {
            listTypeGet(&entry);
            if (listTypeEqual(&entry, refval)) {
                // 添加元素
                inserted = 1;
                listTypeInsert(&entry,val,where);
                break;
            }
        }
        listTypeReleaseIterator(iter);

        // 添加成功, 发送通知
        if (inserted) {

            // 是否转码
            if (subject->encoding == REDIS_ENCODING_ZIPLIST && 
                ziplistLen(subject->ptr) > server.list_max_ziplist_entries) {
                    listTypeConvert(subject,REDIS_ENCODING_LINKEDLIST);
            }

            signalModifiedKey(c->db, c->argv[1]);

            notifyKeyspaceEvent(REDIS_NOTIFY_LIST, "linsert", c->argv[1],c->db->id);

            server.dirty++;

        // 添加失败, 回复客户端
        } else {
            addReply(c,shared.cnegone);
            return;
        }

    // LPUSHX RPUSHX
    } else {
        char *event = (where == REDIS_HEAD) ? "lpushx" : "rpushx";

        listTypePush(subject, val, where);

        signalModifiedKey(c->db, c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_LIST,event,c->argv[1],c->db->id);

        server.dirty++;
    }

    addReplyLongLong(c,listTypeLength(subject));
}

void lpushxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    pushxGenericCommand(c,NULL,c->argv[2],REDIS_HEAD);
}

void rpushxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    pushxGenericCommand(c,NULL,c->argv[2],REDIS_TAIL);
}

// LINSERT key BEFORE|AFTER pivot value
void linsertCommand(redisClient *c) {
    c->argv[4] = tryObjectEncoding(c->argv[4]);

    if (strcasecmp(c->argv[2]->ptr,"after") == 0) {
        pushxGenericCommand(c,c->argv[3],c->argv[4],REDIS_TAIL);

    } else if (strcasecmp(c->argv[2]->ptr,"before") == 0) {
        pushxGenericCommand(c,c->argv[3],c->argv[4],REDIS_HEAD);

    } else {
        addReply(c, shared.syntaxerr);
    }
}

void llenCommand(redisClient *c) {

    robj *o = lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk);

    if (o == NULL || checkType(c, o, REDIS_LIST))
        return;

    addReplyLongLong(c,listTypeLength(o));
}

void lindexCommand(redisClient *c) {

    // 获取值对象
    robj *o = lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk);
    if (o == NULL || checkType(c,o,REDIS_LIST)) return;

    long index;
    robj *value = NULL;

    if (getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != REDIS_OK)
        return;

    // ziplist
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p, *vstr;
        unsigned int vlen;
        long long vlong;

        p = ziplistIndex(o->ptr,index);
        if (ziplistGet(p, &vstr, &vlen,&vlong)) {
            if (vstr) {
                value = createStringObject((char*)vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vlong);
            }

            addReplyBulk(c,value);
            decrRefCount(value);
        } else {
            addReply(c,shared.nullbulk);
        }

    // linkedlist
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        listNode *ln = listIndex(o->ptr,index);
        if (ln != NULL) {
            value = listNodeValue(ln);
            addReplyBulk(c,value);
        } else {
            addReply(c,shared.nullbulk);
        }

    } else {
        redisPanic("Unknown list encoding");
    }
}

// LSET key index value
void lsetCommand(redisClient *c) {
    long index;

    // 获取值对象
    robj *o = lookupKeyWriteOrReply(c,c->argv[1],shared.nokeyerr);
    if (o == NULL && checkType(c,o,REDIS_LIST)) return;

    if (getLongFromObjectOrReply(c,c->argv[2],&index,NULL) != REDIS_OK)
        return;

    // 获取 value 值
    robj *value = tryObjectEncoding(c->argv[2]);

    listTypeTryConversion(o->ptr,value);

    // ziplist
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p;

        // 查找 index 的元素
        p = ziplistIndex(o->ptr, index);

        if (p == NULL) {
            addReply(c,shared.outofrangeerr);

        } else {

            // 删除元素
            o->ptr = ziplistDelete(o->ptr, &p);

            // 添加新元素
            value = getDecodedObject(value);
            o->ptr = ziplistInsert(o->ptr, p, value->ptr, sdslen(value->ptr));
            decrRefCount(value);

            addReply(c, shared.ok);
            signalModifiedKey(c->db, c->argv[1]);
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST, "lset", c->argv[1], c->db->id);
            server.dirty++;
        }


    // linkedlist
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {

        listNode *ln = listIndex(o->ptr, index);
        if (ln == NULL) {
            addReply(c, shared.outofrangeerr);

        } else {
            decrRefCount((robj*)listNodeValue(ln));
            listNodeValue(ln) = value;
            incrRefCount(value);

            addReply(c,shared.ok);
            signalModifiedKey(c->db,c->argv[1]);
            notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"lset",c->argv[1],c->db->id);
            server.dirty++;
            
        }
    } else {
        redisPanic("Unkonwn list encoding");
    }
        
}

void popGenericCommand(redisClient *c, int where) {
    robj *o, *value;
    
    // 获取值对象, 值对象不存在返回
    if ((o == lookupKeyWriteOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_LIST)) return;

    // 弹出元素
    value = listTypePop(o->ptr, where);

    if (value == NULL) {
        addReply(c,shared.nullbulk);

    } else {
        char *event = (where == REDIS_HEAD) ? "lpop" : "rpop";

        addReplyBulk(c,value);
        decrRefCount(value);
        // 发出通知
        notifyKeyspaceEvent(REDIS_NOTIFY_LIST,event,c->argv[1],c->db->id);

        // 如果列表为空, 删除 kv 关联关系
        if (listTypeLength(o) == 0) {
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
            dbDelete(c->db,c->argv[1]);
        }
        signalModifiedKey(c->db,c->argv[1]);
        // 更新键改次数
        server.dirty++;
    }
}

void lpopCommand(redisClient *c) {
    popGenericCommand(c,REDIS_HEAD);
}

void rpopCommand(redisClient *c) {
    popGenericCommand(c,REDIS_TAIL);
}

// LRANGE key start stop
void lrangeCommand(redisClient *c) {
    long start,end;

    // 获取值对象
    robj *o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk);
    if (o == NULL || checkType(c,o,REDIS_LIST)) return;

    // 获取索引
    if (getLongFromObjectOrReply(c, c->argv[2], &start,NULL) != REDIS_OK || 
        getLongFromObjectOrReply(c, c->argv[2], &end,NULL) != REDIS_OK) 
        return;
    
    long llen = listTypeLength(o);

    // 负索引转正索引
    if (start < 0) start += llen;
    if (end < 0) end += llen;
    if (start < 0) start = 0;

    if (start > end || start >= llen) {
        addReply(c,shared.emptymultibulk);
        return;
    }

    if (end >= llen) end = llen-1;
    long rangelen = (end-start)+1;

    addReplyMultiBulkLen(c,rangelen);

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p = ziplistIndex(o->ptr, start);
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        while (rangelen--) {
            if (ziplistGet(p,&vstr,&vlen,&vlong)) {
                if (vstr) {
                    addReplyBulkCBuffer(c,vstr,vlen);
                } else {
                    addReplyBulkLongLong(c,vlong);
                }
            }
            p = ziplistNext(o->ptr,p);
        }

    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        
        listNode *ln;
        if (start > llen/2) start -= llen;
        ln = listIndex(o->ptr, start);

        while(rangelen--) {
            addReplyBulk(c,ln->value);
            ln = ln->next;
        }

    } else {
        redisPanic("Unknow list encoding");
    }

}

// LTRIM key start stop
void ltrimCommand(redisClient *c) {
    robj *o;
    long start, end, llen, j, ltrim, rtrim;
    list *list;
    listNode *ln;

    // 获取值对象
    o = lookupKeyWriteOrReply(c, c->argv[1], shared.ok);
    if (o == NULL || checkType(c,o,REDIS_LIST)) return;

    // 获取索引
    if (getLongFromObjectOrReply(c,c->argv[1],&start,NULL) != REDIS_OK ||
        getLongFromObjectOrReply(c,c->argv[2],&end,NULL) != REDIS_OK)
        return;

    llen = listTypeLength(o);

    // 负索引转正索引
    if (start < 0) start += llen;
    if (end < 0) end += llen;
    if (start < 0) start = 0;
    
    // 左右删除的距离
    if (start > end || start > llen) {
        ltrim = llen;
        rtrim = 0;
    } else {
        if (end > llen) end = llen-1;
        ltrim = start;
        rtrim = llen - end -1;
    }

    // 截取元素
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {

        o->ptr = ziplistDeleteRange(o->ptr, 0, ltrim);

        o->ptr = ziplistDeleteRange(o->ptr, -rtrim,rtrim);

    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        list = o->ptr;

        for (j=0; j<ltrim; j++){
            ln = listFirst(list);
            listDelNode(o->ptr,ln);
        }

        for (j=0; j<rtrim; j++) {
            ln = listLast(list);
            listDelNode(o->ptr,ln);
        }
    } else {
        redisPanic("Unknown list encoding");
    }

    notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"ltrim",c->argv[1],c->db->id);

    if (listTypeLength(o) == 0) {
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
        dbDelete(c->db,c->argv[1]);
    }

    signalModifiedKey(c->db,c->argv[1]);

    server.dirty++;

    addReply(c,shared.ok);
}

// LREM key count value
void lremCommand(redisClient *c) {
    robj *subject,*obj;
    long removed = 0, toremove;
    listTypeEntry entry;

    // 目标元素
    obj = c->argv[3] = tryObjectEncoding(c->argv[3]);

    // 获取值对象
    subject = lookupKeyWriteOrReply(c,c->argv[1],shared.czero);
    if (subject == NULL || checkType(c,subject,REDIS_LIST)) return;

    // 提取 count
    if (getLongFromObjectOrReply(c,c->argv[2],&toremove,NULL) != REDIS_OK)
        return;

    if (subject->encoding == REDIS_ENCODING_ZIPLIST)
        obj = getDecodedObject(obj);

    // 获取迭代列表, 计算删除数量
    listTypeIterator *li;
    if (toremove < 0) {
        toremove = -toremove;
        li = listTypeInitIterator(subject,-1,REDIS_HEAD);
    } else {
        li = listTypeInitIterator(subject,0,REDIS_TAIL);
    }

    // 迭代删除
    while (listTypeNext(li,&entry)) {
        if (listTypeEqual(&entry, obj)) {
            removed++;
            server.dirty++;
            listTypeDelete(&entry);
            
            if (toremove && toremove == removed) break;
        }
    }

    listTypeReleaseIterator(li);

    if (subject->encoding == REDIS_ENCODING_ZIPLIST)
        decrRefCount(obj);

    // 空列表, 删除 kv 关联关系
    if (listTypeLength(subject) == 0) {
        dbDelete(c->db,c->argv[1]);
    }

    addReplyLongLong(c,removed);

    if (removed) signalModifiedKey(c->db,c->argv[1]);
}

void rpoplpushHandlePush(redisClient *c, robj *dstkey, robj *dstobj, robj *value) {

    if (!dstobj) {
        dstobj = createZiplistObject();
        dbAdd(c->db,dstkey,dstobj);
        signalListAsReady(c,dstkey);
    }

    signalModifiedKey(c->db,dstkey);

    listTypePush(dstobj,value,REDIS_HEAD);

    notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"lpush",dstkey,c->db->id);

    addReplyBulk(c,value);
}

// BRPOPLPUSH source destination timeout
void rpoplpushCommand(redisClient *c) {
    robj *sobj, *value;

    // 提取源列表值对象
    if ((sobj = lookupKeyWriteOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,sobj,REDIS_LIST)) return;

    // 源列表为空
    if (listTypeLength(sobj) == 0) {
        addReply(c,shared.nullbulk);
    
    } else {
        
        // 目标列表值对象
        robj *dobj = lookupKeyWrite(c->db,c->argv[2]);
        robj *touchedkey = c->argv[1];

        if (dobj && checkType(c,dobj, REDIS_LIST)) return;

       // 源列表弹出元素
       value = listTypePop(sobj,REDIS_TAIL);

       incrRefCount(touchedkey);

       // 目标列表添加元素
       rpoplpushHandlePush(c, c->argv[2], dobj, value);

       decrRefCount(value);

       notifyKeyspaceEvent(REDIS_NOTIFY_LIST,"rpop",touchedkey,c->db->id);

       if (listTypeLength(sobj) == 0) {
           dbDelete(c->db,touchedkey);
           notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",touchedkey,c->db->id);
       }

       signalModifiedKey(c->db,touchedkey);

       decrRefCount(touchedkey);

       server.dirty++;

    }

}

