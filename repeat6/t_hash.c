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