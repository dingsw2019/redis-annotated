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




/*----------------------------- 命令 ------------------------------*/