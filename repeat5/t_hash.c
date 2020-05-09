#include "redis.h"
#include <math.h>

/*----------------------------- 迭代器 ------------------------------*/

// 初始化并返回迭代器
hashTypeIterator *hashTypeInitIterator(robj *subject) {
    hashTypeIterator *hi = zmalloc(sizeof(hashTypeIterator));

    hi->subject = subject;
    hi->encoding = subject->encoding;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        hi->fptr = NULL;
        hi->vptr = NULL;

    } else if (hi->encoding == REDIS_ENCODING_HT) {
        hi->di = dictGetIterator((dict*)subject->ptr);

    } else {
        redisPanic("Unknown hash encoding");
    }

    return hi;
}

// 释放迭代器
void hashTypeReleaseIterator(hashTypeIterator *hi) {

    if (hi->encoding == REDIS_ENCODING_HT) {
        dictReleaseIterator(hi->di);
    }

    zfree(hi);
}

// 取出当前节点的 field-value 值
// 返回 REDIS_OK, 表示还有节点, 可继续迭代
// 返回 REDIS_ERR, 表示迭代完了
int hashTypeNext(hashTypeIterator *hi) {

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {

        unsigned char *zl,*fptr,*vptr;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;

        // 获取下一个节点的 field
        // 首次迭代
        if (fptr == NULL) {
            redisAssert(vptr == NULL);
            fptr = ziplistIndex(zl,0);

        } else {
            redisAssert(vptr != NULL);
            fptr = ziplistNext(zl,vptr);
        }

        if (fptr == NULL) return REDIS_ERR;

        // 获取下一个节点的 value
        vptr = ziplistNext(zl,fptr);
        redisAssert(vptr != NULL);

        hi->fptr = fptr;
        hi->vptr = vptr;

    } else if (hi->encoding == REDIS_ENCODING_HT) {

        if ((hi->de = dictNext(hi->di)) == NULL) return REDIS_ERR;

    } else {
        redisPanic("Unknown hash encoding");
    }

    return REDIS_OK;
}

/*----------------------------- 转码转换 ------------------------------*/

// 从迭代器中取出 field 或 value
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll)
{
    int ret;

    redisAssert(hi->encoding == REDIS_ENCODING_ZIPLIST);

    if (what & REDIS_HASH_KEY) {    
        ret = ziplistGet(hi->fptr,vstr,vlen,vll);
        redisAssert(ret);
        
    } else {
        ret = ziplistGet(hi->vptr,vstr,vlen,vll);
        redisAssert(ret);
    }
}

// 从迭代器中取出 field 或 value
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst) {
    redisAssert(hi->encoding == REDIS_ENCODING_HT);

    if (what & REDIS_HASH_KEY) {
        *dst = dictGetKey(hi->de);

    } else {
        *dst = dictGetVal(hi->de);
    }
}

// 从迭代器中取出 field 或 value
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what) {
    robj *value;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi,what,&vstr,&vlen,&vll);

        if (vstr) {
            value = createStringObject((char*)vstr,vlen);
        } else {
            value = createStringObjectFromLongLong(vll);
        }

    } else if (hi->encoding == REDIS_ENCODING_HT) {
        hashTypeCurrentFromHashTable(hi,what,&value);

        incrRefCount(value);

    } else {
        redisPanic("Unknown hash encoding");
    }

    return value;
}

// 将 ziplist 编码的哈希对象转换成 enc 编码
void hashTypeConvertZiplist(robj *o, int enc) {

    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);

    if (enc == REDIS_ENCODING_ZIPLIST) {
        // nothing

    } else if (enc == REDIS_ENCODING_HT) {
        int ret;
        // 创建空字典
        dict *d = dictCreate(&hashDictType,NULL);

        // 迭代获取节点
        hashTypeIterator *hi = hashTypeInitIterator(o);
        while (hashTypeNext(hi) != REDIS_ERR) {
            robj *field, *value;
            
            field = hashTypeCurrentObject(hi,REDIS_HASH_KEY);
            field = tryObjectEncoding(field);

            value = hashTypeCurrentObject(hi,REDIS_HASH_VALUE);
            value = tryObjectEncoding(value);

            ret = dictAdd(d,field,value);
            if (ret != DICT_OK) {
                redisLogHexDump(REDIS_WARNING,"ziplist with dup elements dump",
                    o->ptr,ziplistBlobLen(o->ptr));
                redisAssert(ret == DICT_OK);
            }
        }

        // 释放迭代器
        hashTypeReleaseIterator(hi);

        // 更新编码
        o->encoding == REDIS_ENCODING_HT;

        // 释放原内部结构, 绑定新结构
        zfree(o->ptr);

        o->ptr = d;

    } else {
        redisPanic("Unknown hash encoding");
    }
}

void hashTypeConvert(robj *o, int enc) {

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        hashTypeConvertZiplist(o,REDIS_ENCODING_HT);

    } else if (o->encoding == REDIS_ENCODING_HT) {
        redisPanic("Not implemented");
    } else {
        redisPanic("Unknown hash encoding");
    }
}

// 对多个值进行检查, 查看长度是否超过 hash 限制而需要转码
void hashTypeTryConversion(robj *o, robj **argv, int start, int end) {
    int i;

    if (o->encoding != REDIS_ENCODING_ZIPLIST) return;

    for (i = start; i <= end; i++) {

        if (sdsEncodedObject(argv[i]) &&
            sdslen(argv[i]) > server.hash_max_ziplist_value) {

            hashTypeConvert(o,REDIS_ENCODING_HT);
            break;
        }
    }
}

/*----------------------------- 基础函数 ------------------------------*/

// 将 o1 , o2 转换成字符串
void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2) {

    if (subject->encoding == REDIS_ENCODING_HT) {
        *o1 = tryObjectEncoding(*o1);
        *o2 = tryObjectEncoding(*o2);
    }
}

// 查找 field 的值, 并写入 vstr 或 vll 中
// 找到返回 0, 否则返回 -1
int hashTypeGetFromZiplist(robj *o, robj *field, 
                            unsigned char **vstr, unsigned int *vlen, long long *vll) 
{

    // 提取哈希对象
    unsigned char *zl,*fptr = NULL, *vptr = NULL;
    int ret;

    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);

    field = getDecodedObject(field);

    // 查找 field 节点
    zl = o->ptr;
    fptr = ziplistIndex(zl,ZIPLIST_HEAD);
    if (fptr != NULL) {

        fptr = ziplistFind(fptr,field->ptr,sdslen(field->ptr),1);
        
        if (fptr != NULL) {
            vptr = ziplistNext(zl,fptr);
            redisAssert(vptr != NULL);
        }
    }

    decrRefCount(field);

    // 查找 value 节点, 提取值
    if (vptr != NULL) {
        ret = ziplistGet(vptr,vstr,vlen,vll);
        redisAssert(ret);
        return 0;
    }

    return -1;
}

// 查找 field 的值, 并写入 value
// 找到返回 0, 否则返回 -1
int hashTypeGetFromHashTable(robj *o, robj *field, robj **value) {

    dictEntry *de;

    redisAssert(o->encoding == REDIS_ENCODING_HT);

    de = dictFind(o->ptr, field->ptr);

    if (de == NULL) return -1;

    *value = dictGetVal(de);

    return 0;
}

// 查找 field 的值, 并以 robj 结构返回
robj *hashTypeGetObject(robj *o, robj *field) {
    robj *value = NULL;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        // 查找成功
        if (hashTypeGetFromZiplist(o,field,&vstr,&vlen,&vll) == 0) {
            if (vstr) {
                value = createStringObject(vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vll);
            }
        }

    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *aux;

        if (hashTypeGetFromHashTable(o,field,&aux) == 0) {
            incrRefCount(aux);
            value = aux;
        }
    } else {
        redisPanic("Unknown hash encoding");
    }

    return value;
}

// 查找 field 是否存在
// 存在返回 1, 否则返回 0
int hashTypeExists(robj *o, robj *field) {

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o,field,&vstr,&vlen,&vll) == 0)
            return 1;

    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *aux;
        if (hashTypeGetFromHashTable(o,field,&aux) == 0)
            return 1;
    }

    return 0;
}

/**
 * 将 field-value 添加到哈希表
 * 如果 field 已存在, 更新其 value
 * 
 * 返回 0 , 表示新增
 * 返回 1 , 表示更新
 */
int hashTypeSet(robj *o, robj *field, robj *value) {
    int update = 0;

    // ziplist
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl,*fptr,*vptr;

        field = getDecodedObject(field);
        value = getDecodedObject(value);

        // 查找 field
        zl = o->ptr;
        fptr = ziplistIndex(zl,ZIPLIST_HEAD);

        // 更新
        if (fptr != NULL) {

            // field 存在
            fptr = ziplistFind(fptr,field->ptr,sdslen(field->ptr),1);

            if (fptr != NULL) {

                // 定位 value
                vptr = ziplistNext(zl,fptr);
                redisAssert(vptr != NULL);

                // 删除旧 value
                zl = ziplistDelete(zl,&vptr);

                // 绑定新 value
                zl = ziplistInsert(zl,vptr,value->ptr,sdslen(value->ptr));

                update = 1;
            }
        }

        // 新增
        if (!update) {

            ziplistPush(zl,field->ptr,sdslen(field->ptr),ZIPLIST_TAIL);
            ziplistPush(zl,value->ptr,sdslen(value->ptr),ZIPLIST_TAIL);
        }

        o->ptr = zl;

        decrRefCount(field);
        decrRefCount(value);

                    
        // 节点数量是否超限制, 需要转码
        if (hashTypeLength(o) > server.hash_max_ziplist_entries) {
            hashTypeConvert(o,REDIS_ENCODING_HT);
        }

    // ht
    } else if (o->encoding == REDIS_ENCODING_HT) {
        // 添加
        if (dictReplace(o->ptr,field,value)) {
            incrRefCount(field);
        
        // 更新
        } else {
            update = 1;
        }
        incrRefCount(value);
    
    } else {
        redisPanic("Unknown hash encoding");
    }

    return update;
}

// 删除 field
// 成功返回 1, 否则返回 0
int hashTypeDelete(robj *o, robj *field) {
    int deleted = 0;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {

        unsigned char *zl, *fptr;

        field = getDecodedObject(field);

        zl = o->ptr;
        fptr = ziplistIndex(zl,ZIPLIST_HEAD);
        if (fptr != NULL) {
            fptr = ziplistFind(fptr,field->ptr,sdslen(field->ptr),1);

            if (fptr != NULL) {
                zl = ziplistDelete(zl,&fptr);
                zl = ziplistDelete(zl,&fptr);

                o->ptr = zl;
                deleted = 1;
            }
        }
        decrRefCount(field);

    } else if (o->encoding == REDIS_ENCODING_HT) {

        if (dictDelete((dict*)o->ptr,field->ptr) == REDIS_OK) {
            deleted = 1;
            
            // 字典是否需要收缩
            if (htNeedsResize(o->ptr)) dictResize(o->ptr);
        }

    } else {
        redisPanic("Unknown hash encoding");
    }

    return deleted;
}

// 节点数量
unsigned long hashTypeLength(robj *o) {
    unsigned long length = ULONG_MAX;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        length = ziplistLen(o->ptr) / 2;

    } else if (o->encoding == REDIS_ENCODING_HT) {
        length = dictSize((dict*)o->ptr);

    } else {
        redisPanic("Unknown hash encoding");   
    }
}

// 如果 key 不存在关联的哈希对象
// 创建一个并返回
robj *hashTypeLookupWriteOrCreate(redisClient *c, robj *key) {
    
    robj *o = lookupKeyWrite(c->db,key);

    // 新增
    if (o == NULL) {
        o = createHashObject();
        dbAdd(c->db,key,o);
    
    // 检查类型
    } else {
        if (o->type != REDIS_HASH) {
            addReply(c,shared.wrongtypeerr);
            return NULL;
        }
    }

    return o;
}

/*----------------------------- 命令 ------------------------------*/

// HSET key field value
void hsetCommand(redisClient *c) {
    robj *o;
    int update;

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // 是否需要转码
    hashTypeTryConversion(o,c->argv,2,3);

    hashTypeTryObjectEncoding(o,&c->argv[2],c->argv[3]);

    // 添加或更新 value
    update = hashTypeSet(o,c->argv[2],c->argv[3]);

    // 回复客户端
    addReply(c,update ? shared.czero : shared.cone);

    // 发送通知
    signalModifiedKey(c->db,c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",
        c->argv[1],c->db->id);

    server.dirty++;
    
}

// HSETNX key field value
void hsetnxCommand(redisClient *c) {
    robj *o;

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    hashTypeTryConversion(o,c->argv,2,3);

    // field 存在, 报错返回
    if (hashTypeExists(o, c->argv[2])) {

        addReply(c,shared.czero);
        return;

    // field 不存在, 添加
    } else {

        hashTypeTryObjectEncoding(o,&c->argv[2],&c->argv[3]);

        hashTypeSet(o,c->argv[2],c->argv[3]);

        // 回复客户端
        addReply(c,shared.cone);

        // 发送通知
        signalModifiedKey(c->db,c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",
            c->argv[1],c->db->id);

        server.dirty++;
    }
}

// HMSET key field value [field value ...]
void hmsetCommand(redisClient *c) {
    robj *o;
    int j;

    // 参数检查
    if (c->argc % 2 == 1) {
        addReplyError(c,"wrong number of arguments for HMSET");
        return;
    }

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    hashTypeTryConversion(o,c->argv,2,c->argc-1);

    // 添加节点
    for (j = 2; j < c->argc; j+=2) {

        // 尝试将参数转成字符串
        hashTypeTryObjectEncoding(o,&c->argv[j],&c->argv[j+1]);

        // 记录添加数量
        hashTypeSet(o,c->argv[j],c->argv[j+1]);
    }

    // 回复客户端
    addReply(c,shared.ok);

    // 发送通知
    signalModifiedKey(c->db,c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",
            c->argv[1],c->db->id);

    server.dirty++;
}

// HINCRBY key field increment
void hincrbyCommand(redisClient *c) {
    long long incr, value, oldvalue;
    robj *o, *current, *new;

    // 取出 incr 值
    if (getLongLongFromObjectOrReply(c,c->argv[3],&incr,NULL) != REDIS_OK) return;

    // 取出哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // 取出 field 的值
    if ((current = hashTypeGetObject(o,c->argv[2])) != NULL) {

        if (getLongLongFromObjectOrReply(c,current,&value,
            "hash value is not an integer") != REDIS_OK) {
            decrRefCount(current);
            return;
        }
        decrRefCount(current);
    } else {
        value = 0;
    }

    // 溢出检查
    oldvalue = value;
    if (incr < 0 && oldvalue < 0 && incr < (LLONG_MIN - oldvalue) ||
        incr > 0 && oldvalue > 0 && incr > (LLONG_MAX - oldvalue)) {

        addReplyError(c,"increment or decrement would overflow");
        return;
    }

    // 更新或写入新值
    value += incr;
    new = createStringObjectFromLongLong(value);

    hashTypeTryObjectEncoding(o,&c->argv[2],NULL);

    hashTypeSet(o,c->argv[2],new);

    decrRefCount(new);

    // 回复客户端
    addReplyLongLong(c,value);

    // 发送通知
    signalModifiedKey(c->db,c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hincrby",
        c->argv[1],c->db->id);

    server.dirty++;
}

// HINCRBYFLOAT key field increment
void hincrbyfloatCommand(redisClient *c) {
    double long incr, value;
    robj *o, *current, *new, *aux;

    // 取出 incr
    if (getLongDoubleFromObjectOrReply(c,c->argv[3],&incr,NULL) != REDIS_OK) return;

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // 取出 field 的值
    if ((current = hashTypeGetObject(o,c->argv[2])) != NULL) {
        if (getLongDoubleFromObjectOrReply(c,current,&value,
            "hash value is not a valid float") != REDIS_OK) {
            
            decrRefCount(current);
            return;
        }
        decrRefCount(current);
    } else {
        value = 0;
    }

    // 计算新值
    value += incr;
    new = createStringObjectFromLongDouble(value);

    hashTypeTryObjectEncoding(o,&c->argv[2],NULL);
    // 写入新值
    hashTypeSet(o,c->argv[2],new);

    // 回复客户端
    addReplyBulk(c,new);

    // 发送通知
    signalModifiedKey(c->db,c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hincrbyfloat",
        c->argv[1],c->db->id);

    server.dirty++;

    aux = createStringObject("HSET",4);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,3,new);
    decrRefCount(new);
}

// 将 field 的值添加到回复客户端的 buf
static void addHashFieldToReply(redisClient *c, robj *o, robj *field) {
    int ret;

    // 提取 field 的值
    if (o == NULL) {
        addReply(c,shared.nullbulk);
        return;
    }

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr, *vptr;
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        field = getDecodedObject(field);

        ret = hashTypeGetFromZiplist(o,field,&vstr,&vlen,&vll);

        if (ret < 0) {
            addReply(c,shared.nullbulk);
        } else {

            if (vstr) {
                addReplyBulkCBuffer(c,vstr,vlen);
            } else {
                addReplyLongLong(c,vll);
            }
        }

    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *value;
        ret = hashTypeGetFromHashTable(o,field,&value);

        if (ret < 0) {
            addReply(c,shared.nullbulk);
        } else {
            addReplyBulk(c,value);
        }
    } else {
        redisPanic("Unknown hash encoding");
    }
}

// HGET key field
void hgetCommand(redisClient *c) {
    robj *o;

    // 获取哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // 回复客户端
    addHashFieldToReply(c,o,c->argv[2]);
}

// HMGET key field [field ...]
void hmgetCommand(redisClient *c) {
    robj *o;
    int j;

    // 获取哈希对象
    o = lookupKeyRead(c,c->argv[1]);
    if (o == NULL || o->type != REDIS_HASH) {
        addReply(c,shared.wrongtypeerr);
        return;
    }

    // 回复客户端
    addReplyMultiBulkLen(c,c->argc-2);
    for (j = 2; j < c->argc; j++) {
        addHashFieldToReply(c,o,c->argv[j]);
    }
}

// HDEL key field [field ...]
void hdelCommand(redisClient *c) {
    robj *o;
    int j, deleted = 0, keyremoved = 0;

    // 获取哈希对象
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    for (j = 2; j < c->argc; j++) {
        if (hashTypeDelete(o,c->argv[j])) {
            deleted++;

            if (hashTypeLength(o) == 0) {
                keyremoved = 1;
                dbDelete(c->db,c->argv[1]);
                break;
            }
        }
    }

    if (deleted) {
        signalModifiedKey(c->db,c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hdel",
            c->argv[1],c->db->id);

        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                c->argv[1],c->db->id);

        server.dirty += deleted;
    }

    addReplyLongLong(c,deleted);
}

// HLEN key
void hlenCommand(redisClient *c) {
    robj *o;

    // 获取哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // 回复客户端
    addReplyLongLong(c,hashTypeLength(o));
}

// 从迭代器中取出 field 或 value, 然后回复客户端
static void addHashIteratorCursorToReply(redisClient *c, hashTypeIterator *hi, int what) {
    int ret;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi,what,&vstr,&vlen,&vll);

        if (vstr) {
            addReplyBulkCBuffer(c,vstr,vlen);
        } else {
            addReplyLongLong(c,vll);
        }

    } else if (hi->encoding == REDIS_ENCODING_HT) {
        robj *value;

        hashTypeCurrentFromHashTable(hi,what,&value);

        addReplyBulk(c,value);

    } else {
        redisPanic("Unknown hash encoding");
    }
}

// getall 的通用函数
void genericHgetallCommand(redisClient *c, int flags) {
    robj *o;
    int multiplier = 0, length, count = 0;
    hashTypeIterator *hi;

    // 获取哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // 计算发送的节点数
    if (flags & REDIS_HASH_KEY) multiplier++;
    if (flags & REDIS_HASH_VALUE) multiplier++;

    length = hashTypeLength(o) *multiplier;

    addReplyMultiBulkLen(c,length);

    // 迭代发送节点
    hi = hashTypeInitIterator(o);
    while (hashTypeNext(hi) == REDIS_OK) {
        robj *field,*value;
        
        if (flags & REDIS_HASH_KEY) {
            addHashIteratorCursorToReply(c,hi,REDIS_HASH_KEY);
            count++;
        }

        if (flags & REDIS_HASH_VALUE) {
            addHashIteratorCursorToReply(c,hi,REDIS_HASH_VALUE);
            count++;
        }
    }

    // 释放迭代
    hashTypeReleaseIterator(hi);
    redisAssert(length == count);
}

// HKEYS key
void hkeysCommand(redisClient *c) {
    genericHgetallCommand(c,REDIS_HASH_KEY);
}

// HVALS key
void hvalsCommand(redisClient *c) {
    genericHgetallCommand(c,REDIS_HASH_VALUE);
}

// HGETALL key
void hgetallCommand(redisClient *c) {
    genericHgetallCommand(c,REDIS_HASH_KEY|REDIS_HASH_VALUE);
}

// HEXISTS key field
void hexistsCommand(redisClient *c) {
    robj *o;

    // 获取哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    addReply(c,hashTypeExists(o,c->argv[2]) ? shared.cone : shared.czero);
}