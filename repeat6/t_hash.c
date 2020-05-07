#include "redis.h"
#include <math.h>

/*----------------------------- 迭代器 ------------------------------*/

// 初始化迭代器
hashTypeIterator *hashTypeInitIterator(robj *subject) {
    hashTypeIterator *hi;

    // 申请内存
    hi = zmalloc(sizeof(hashTypeIterator));

    hi->subject = subject;
    hi->encoding = subject->encoding;
    
    // ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        hi->fptr = NULL;
        hi->vptr = NULL;

    // hashtable
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

// 移动指针, 返回当前元素
// 返回 REDIS_OK, 表示还可移动
// 返回 REDIS_ERR, 表示没有元素可迭代了
int hashTypeNext(hashTypeIterator *hi) {

    // ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl,*fptr = NULL, *vptr = NULL;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;

        // 首次迭代, 定位第一个元素
        if (fptr == NULL) {
            redisAssert(vptr == NULL);
            fptr = ziplistIndex(zl, 0);

        // 指向下一个元素, 获取域
        } else {
            redisAssert(vptr != NULL);
            fptr = ziplistNext(zl,vptr);
        }

        if (fptr == NULL) return REDIS_ERR;

        // 获取域成功, 提取值
        vptr = ziplistNext(zl,fptr);
        redisAssert(vptr != NULL);

        hi->fptr = fptr;
        hi->vptr = vptr;

    // hashtable
    } else if (hi->encoding == REDIS_ENCODING_HT) {
        // 指向下一个节点
        if((hi->de = dictNext(hi->di)) == NULL) return REDIS_ERR;

    } else {
        redisPanic("Unknown hash encoding");
    }
    
    // 迭代成功
    return REDIS_OK;
}

/*----------------------------- 转码转换 ------------------------------*/

// 从 ziplist 中提取值
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,unsigned char **vstr, 
                                unsigned int *vlen,long long *vll) 
{
    int ret;

    redisAssert(hi->encoding == REDIS_ENCODING_ZIPLIST);

    if (what & REDIS_HASH_KEY) {
        ret = ziplistGet(hi->fptr,vstr,vlen,vll);
        redisAssert(ret);

    } else {
        ziplistGet(hi->vptr,vstr,vlen,vll);
        redisAssert(ret);
    }
}

// 从 hashtable 中提取值
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst) {

    redisAssert(hi->encoding == REDIS_ENCODING_HT);

    if (what & REDIS_HASH_KEY) {
        *dst = dictGetKey(hi->de);
    } else {
        *dst = dictGetVal(hi->de);
    }
}


// 从 hash 结构中提取域或值的内容, 并以 robj 结构返回
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what) {
    robj *value;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi,what,&vstr,&vlen,&vll);

        if (vstr) {
            value = createStringObject(vstr,vlen);
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

void hashTypeConvertZiplist(robj *o, int enc) {

    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);

    if (enc == REDIS_ENCODING_ZIPLIST) {
        // nothing
    } else if (enc == REDIS_ENCODING_HT) {
        hashTypeIterator *hi;
        dict *d;
        int ret;

        // 生成迭代器
        hi = hashTypeInitIterator(o);

        // 生成字典结构
        d = dictCreate(&hashDictType,NULL);


        // 迁移元素
        while (hashTypeNext(hi) == REDIS_OK) {
            robj *field;
            robj *value;

            // 提取域
            field = hashTypeCurrentObject(hi,REDIS_HASH_KEY);
            field = tryObjectEncoding(field);

            // 提取值
            value = hashTypeCurrentObject(hi,REDIS_HASH_VALUE);
            value = tryObjectEncoding(value);

            // 添加到字典结构
            ret = dictAdd(d, field, value);
            if (ret != DICT_OK) {
                redisLogHexDump(REDIS_WARNING,"ziplist with dup elements dump",
                    o->ptr,ziplistBlobLen(o->ptr));
                redisAssert(ret == DICT_OK);
            }
        }

        // 释放迭代器
        hashTypeReleaseIterator(hi);

        // 释放ziplist结构
        zfree(o->ptr);

        // 更新编码
        o->encoding = REDIS_ENCODING_HT;

        // 绑定字典结构
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

void hashTypeTryConversion(robj *o, robj **argv, int start, int end) {

    int i;

    if (o->encoding != REDIS_ENCODING_ZIPLIST) return;

    for (i = start; i <= end; i++) {

        if (sdsEncodedObject(argv[i]) && 
            sdslen(argv[i]->ptr) > server.hash_max_ziplist_value) {
            
            hashTypeConvert(o,REDIS_ENCODING_HT);
            break;
        }
    }
}

/*----------------------------- 基础函数 ------------------------------*/

// 压缩值
void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2) {

    if (subject->encoding == REDIS_ENCODING_HT) {
        *o1 = tryObjectEncoding(*o1);
        *o2 = tryObjectEncoding(*o2);
    }
}

// 从 ziplist 提取 field 的值内容
// 找到返回 0, 否则返回 -1
int hashTypeGetFromZiplist(robj *o, robj *field, 
                            unsigned char **vstr, unsigned int *vlen, long long *vll) 
{
    unsigned char *zl,*fptr = NULL,*vptr = NULL;
    int ret;

    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);

    field = getDecodedObject(field);

    zl = o->ptr;
    fptr = ziplistIndex(zl,ZIPLIST_HEAD);
    if (fptr != NULL) {
        // 查找域
        fptr = ziplistFind(fptr,field->ptr,sdslen(field->ptr),1);

        // 查找值
        if (fptr != NULL) {
            vptr = ziplistNext(zl,fptr);
            redisAssert(vptr != NULL);
        }
    }

    decrRefCount(field);

    // 查找值
    if (vptr != NULL) {
        ret = ziplistGet(vptr,vstr,vlen,vll);
        redisAssert(ret);
        return 0;
    }

    return -1;
}

// 从 hashtable 提取 field 的值内容
int hashTypeGetFromHashTable(robj *o, robj *field, robj **value) {
    dictEntry *de;

    redisAssert(o->encoding == REDIS_ENCODING_HT);

    de = dictFind(o->ptr,field);

    if (de == NULL) return -1;

    *value = dictGetVal(de);

    return 0;
}

// 提取 field 的值内容
robj *hashTypeGetObject(robj *o, robj *field) {
    robj *value = NULL;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o,field,&vstr,&vlen,&vll) == 0) {

            if (vstr) {
                value = createStringObject((char*)vstr,vlen);
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
}

// 判断域是否存在于哈希结构中
// 存在返回 1, 否则返回 0
int hashTypeExists(robj *o, robj *field) {

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o,field,&vstr,&vlen,&vll) == 0) {

            return 1;
        }

    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *aux;

        if (hashTypeGetFromHashTable(o,field,&aux) == 0) {
            return 1;
        }

    } else {
        redisPanic("Unknown hash encoding");
    }

    return 0;
}

// 新增或更新 field-value 对到哈希结构
// 新增返回 0, 更新返回 1
int hashTypeSet(robj *o, robj *field, robj *value) {
    int update = 0;


    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr, *vptr;

        field = getDecodedObject(field);
        value = getDecodedObject(value);

        // 查找 field
        zl = o->ptr;
        fptr = ziplistIndex(zl,ZIPLIST_HEAD);
        if (fptr != NULL) {

            fptr = ziplistFind(fptr,field->ptr,sdslen(field->ptr),1);
            if (fptr != NULL) {
                
                // 释放原值
                vptr = ziplistNext(zl,fptr);
                redisAssert(vptr != NULL);

                update = 1;

                zl = ziplistDelete(zl,&vptr);

                // 绑定新值
                zl = ziplistInsert(zl,vptr,value->ptr,sdslen(value->ptr));
            }
        }

        // 新增
        if (!update) {
            zl = ziplistPush(zl,field->ptr,sdslen(field->ptr),ZIPLIST_TAIL);
            zl = ziplistPush(zl,value->ptr,sdslen(value->ptr),ZIPLIST_TAIL);
        }

        o->ptr = zl;

        decrRefCount(field);
        decrRefCount(value);

        // 尝试转换编码
        if (hashTypeLength(o) > server.hash_max_ziplist_entries) {
            hashTypeConvert(o,REDIS_ENCODING_HT);
        }


    } else if (o->encoding == REDIS_ENCODING_HT) {

        if (dictReplace(o->ptr,field,value)) {
            incrRefCount(field);
        } else {
            update = 1;
        }

        incrRefCount(value);

    } else {
        redisPanic("Unknown hash encoding");
    }

    return update;
}

// 删除域和值, 删除成功返回 1, 否则返回 0
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

    } else if (o->encoding == REDIS_ENCODING_HT) {

        if (dictDelete((dict*)o->ptr,field) == DICT_OK) {
            deleted = 1;

            if (htNeedsResize(o->ptr)) dictResize(o->ptr);
        }

    } else {
        redisPanic("Unknown hash encoding");
    }

    return deleted;
}

// field-value 对的数量
unsigned long hashTypeLength(robj *o) {
    unsigned long length = ULONG_MAX;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        length = ziplistLen(o->ptr) / 2;

    } else if (o->encoding == REDIS_ENCODING_HT) {
        length = dictSize((dict*)o->ptr);

    } else {
        redisPanic("Unknown hash encoding");
    }    

    return length;
}


/**
 * 按 key 查找并返回哈希对象
 * 如果对象不存在, 那么创建一个哈希对象并返回
 */
robj *hashTypeLookupWriteOrCreate(redisClient *c, robj *key) {
    robj *o;

    o = lookupKeyWrite(c->db,key);

    if (o == NULL) {
        o = createHashObject();
        dbAdd(c->db,key,o);
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

    // 是否转码
    hashTypeTryConversion(o,c->argv,2,3);

    // 转码成字符串
    hashTypeTryObjectEncoding(o,&c->argv[2],&c->argv[3]);

    // 设置 field-value
    update = hashTypeSet(o,c->argv[2],c->argv[3]);

    // 回复客户端
    addReply(c, update ? shared.czero : shared.cone);

    // 发送键改通知
    signalModifiedKey(c->db,c->argv[1]);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",c->argv[1],c->db->id);

    // 更新键改次数
    server.dirty++;
}

// HSETNX key field value
void hsetnxCommand(redisClient *c) {
    robj *o;

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // 尝试转码
    hashTypeTryConversion(o,c->argv,2,3);

    // field 已经存在, 回复客户端
    if (hashTypeExists(o,c->argv[2])) {
        addReply(c,shared.czero);
        return;

    // 添加 field-value
    } else {
        // 转换成字符串格式
        hashTypeTryObjectEncoding(o,&c->argv[2],&c->argv[3]);

        hashTypeSet(o,c->argv[2],c->argv[3]);

        // 回复客户端
        addReply(c, shared.cone);

        // 发送键改通知
        signalModifiedKey(c->db,c->argv[1]);

        // 发送事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",c->argv[1],c->db->id);

        // 更新键改次数
        server.dirty++;
    }

}

// HMSET key field value [field value ...]
void hmsetCommand(redisClient *c) {
    int i;
    robj *o;

    // 参数数量检查
    if (c->argc % 2 == 1) {
        addReplyError(c,"wrong number of arguments for HMSET");
        return;
    }

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // 尝试转码
    hashTypeTryConversion(o,c->argv,2,c->argc-1);

    // 遍历参数
    for (i = 2; i < c->argc; i += 2) {

        // 转成字符串
        hashTypeTryObjectEncoding(o,&c->argv[i],&c->argv[i+1]);

        // 添加 field-value
        hashTypeSet(o,c->argv[i],c->argv[i+1]);
    }

    // 回复客户端
    addReply(c,shared.ok);

    // 发送键改通知
    signalModifiedKey(c->db,c->argv[1]);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hset",c->argv[1],c->db->id);

    // 更新键改次数
    server.dirty++;
}

// HINCRBY key field increment
void hincrbyCommand(redisClient *c) {
    long long incr, value, oldvalue;
    robj *o, *new, *current;

    // 取出 incr
    if (getLongLongFromObjectOrReply(c,c->argv[3],&incr,NULL) != REDIS_OK)
        return;

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    // 获取 field 的值
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

    // 是否溢出
    oldvalue = value;
    if (incr < 0 && oldvalue < 0 && incr < (LLONG_MIN - oldvalue) ||
        incr > 0 && oldvalue > 0 && incr > (LLONG_MAX - oldvalue)) {

        addReplyError(c,"increment or decrement would overflow");
        return;
    }

    // 写入新值
    value += incr;
    new = createStringObjectFromLongLong(value);
    hashTypeTryObjectEncoding(o,&c->argv[2],NULL);
    hashTypeSet(o,c->argv[2],new);

    decrRefCount(new);

    // 回复客户端
    addReplyLongLong(c,value);

    // 发送键改通知
    signalModifiedKey(c->db,c->argv[1]);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hincrby",c->argv[1],c->db->id);

    // 更新键改次数
    server.dirty++;
}

// HINCRBYFLOAT key field increment
void hincrbyfloatCommand(redisClient *c) {
    double long incr, value;
    robj *o, *new, *current, *aux;

    // 取出 incr
    if (getLongDoubleFromObjectOrReply(c,c->argv[3],&incr,NULL) != REDIS_OK)
        return;

    // 取出或创建哈希对象
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL)
        return;

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

    // 写入新值
    value += incr;
    new = createStringObjectFromLongDouble(value);
    hashTypeTryObjectEncoding(o,&c->argv[2],NULL);
    hashTypeSet(o,c->argv[2],new);

    // 回复客户端
    addReplyBulk(c,new);

    // 发送键改通知
    signalModifiedKey(c->db,c->argv[1]);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hincrbyfloat",c->argv[1],c->db->id);

    // 更新键改次数
    server.dirty++;

    aux = createStringObject("HSET",4);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,3,new);
    decrRefCount(new);
}

/**
 * 辅助函数, 将 field 的值添加到回复中
 */
static void addHashFieldToReply(redisClient *c, robj *o, robj *field) {
    int ret;

    if (o == NULL) {
        addReply(c,shared.nullbulk);
        return;
    }

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

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

    // 取出哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // 取出 field 的值
    addHashFieldToReply(c,o,c->argv[2]);
}

// HMGET key field [field ...]
void hmgetCommand(redisClient *c) {
    robj *o;
    int i;

    // 取出哈希对象
    o = lookupKeyRead(c,c->argv[1]);
    if (o == NULL || o->type != REDIS_HASH){
        addReply(c,shared.wrongtypeerr);
        return;
    }

    // 遍历提取 field 的值
    addReplyMultiBulkLen(c,c->argc-2);
    for (i = 2; i < c->argc; i++) {
        addHashFieldToReply(c,o,c->argv[i]);
    }   
}

// HDEL key field [field ...]
void hdelCommand(redisClient *c) {
    int i, deleted = 0, keyremoved = 0;
    robj *o;

    // 取出哈希对象
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    // 遍历删除
    for (i = 2; i < c->argc; i++) {
        
        // 删除成功
        if (hashTypeDelete(o,c->argv[i])) {
            deleted++;
            
            // 是否无节点了
            if (hashTypeLength(o) == 0) {
                keyremoved = 1;
                dbDelete(c->db,c->argv[1]);
                break;
            }
        }
    }

    // 发送通知
    if (deleted) {
        signalModifiedKey(c->db,c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hdel",c->argv[1],c->db->id);

        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);

        server.dirty += deleted;
    }

    // 回复客户端
    addReplyLongLong(c,deleted);
}

// HLEN key
void hlenCommand(redisClient *c) {
    robj *o;

    // 取出哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
    checkType(c,o,REDIS_HASH)) return;

    // 返回长度
    addReplyLongLong(c,hashTypeLength(o));
}

/**
 * 从迭代器当前指向的节点中取出 fiele 或 value
 * 并回复给客户端
 */
static void addHashIteratorCursorToReply(redisClient *c, hashTypeIterator *hi, int what) {

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

/**
 * 遍历哈希表, 取出 field 或 value, 并回复客户端
 */
void genericHgetallCommand(redisClient *c, int flags) {
    int count=0, length=0, multiplier = 0;
    robj *o;
    hashTypeIterator *hi;

    // 取出哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
    checkType(c,o,REDIS_HASH)) return;

    // 计算需获取 field, value 的数量
    if (flags & REDIS_HASH_KEY) multiplier++;
    if (flags & REDIS_HASH_VALUE) multiplier++;

    length = hashTypeLength(o) * multiplier;

    addReplyMultiBulkLen(c,length);
    // 初始化迭代器
    hi = hashTypeInitIterator(o);

    // 迭代,提取field 或 value
    while (hashTypeNext(hi) != REDIS_ERR) {
        
        if (flags & REDIS_HASH_KEY) {
            addHashIteratorCursorToReply(c,hi,REDIS_HASH_KEY);
            count++;
        }

        if (flags & REDIS_HASH_VALUE) {
            addHashIteratorCursorToReply(c,hi,REDIS_HASH_VALUE);
            count++;
        }
    }

    // 释放迭代器
    hashTypeReleaseIterator(hi);
    redisAssert(count == length);
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
    genericHgetallCommand(c,REDIS_HASH_KEY | REDIS_HASH_VALUE);
}

// HEXISTS key field
void hexistsCommand(redisClient *c) {
    robj *o;

    // 取出哈希对象
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;

    addReply(c,hashTypeExists(o,c->argv[2]) ? shared.cone : shared.czero);
}

// HSCAN key cursor [MATCH pattern] [COUNT count]
void hscanCommand(redisClient *c) {
    robj *o;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == REDIS_ERR) return;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,o,REDIS_HASH)) return;
    scanGenericCommand(c,o,cursor);
}