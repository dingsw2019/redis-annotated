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