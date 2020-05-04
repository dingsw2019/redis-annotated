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

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {


    } else if (hi->encoding == REDIS_ENCODING_HT) {


    } else {
        redisPanic("Unknown hash encoding");
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