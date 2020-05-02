#include "redis.h"
#include <math.h>

/*----------------------------- 迭代器 ------------------------------*/

/**
 * 初始化哈希结构迭代器
 */
hashTypeIterator *hashTypeInitIterator(robj *subject) {

    // 申请内存空间
    hashTypeIterator *hi = zmalloc(sizeof(hashTypeIterator));

    // 初始化属性
    hi->subject = subject;
    hi->encoding = subject->encoding;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        hi->fptr = NULL;
        hi->vptr = NULL;

    } else if (hi->encoding == REDIS_ENCODING_HT) {
        hi->di = dictGetIterator(subject->ptr);
    
    } else {
        redisPanic("Unknown hash encoding");
    }

    return hi;
}

/**
 * 释放哈希结构迭代器
 */
void hashTypeReleaseIterator(hashTypeIterator *hi) {

    if (hi->encoding == REDIS_ENCODING_HT) {
        dictReleaseIterator(hi->di);
    }

    zfree(hi);
}

/**
 * 移动哈希结构迭代器
 * 移动成功, 返回 REDIS_OK
 * 移动失败, 返回 REDIS_ERR
 */
int hashTypeNext(hashTypeIterator *hi) {

    // ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl;
        unsigned char *fptr, *vptr;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;

        // 首次迭代
        if (fptr == NULL) {
            redisAssert(vptr == NULL);
            fptr = ziplistIndex(zl, 0);
        
        // 移动至下一个哈希的键
        } else {
            redisAssert(vptr != NULL);
            fptr = ziplistNext(zl, vptr);
        }

        // 获取到下一个哈希的键失败
        if (fptr == NULL) return REDIS_ERR;

        // 获取下一个哈希的值
        vptr = ziplistNext(zl, fptr);
        redisAssert(vptr != NULL);

        // 赋值给迭代器的哈希键值指针
        hi->fptr = fptr;
        hi->vptr = vptr;


    } else if (hi->encoding == REDIS_ENCODING_HT) {
        if ((hi->de = dictNext(hi->di)) == NULL) return REDIS_ERR;
    
    } else {
        redisPanic("Unknown hash encoding");
    }

    return REDIS_OK;
}

/**
 * 从 ZIPLIST 编码的哈希中, 取出迭代器指针当前指向节点的域或值
 */
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll)
{
    int ret;

    redisAssert(hi->encoding == REDIS_ENCODING_ZIPLIST);

    // 提取域
    if (what & REDIS_HASH_KEY) {
        ret = ziplistGet(hi->fptr, vstr, vlen, vll);
        redisAssert(ret);

    // 提取值
    } else {
        ret = ziplistGet(hi->vptr, vstr, vlen, vll);
        redisAssert(ret);
    }
}

/**
 * 从 ENCODING_HT 编码的哈希中, 取出迭代器指针当前指向节点的域或值
 */
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst) {

    redisAssert(hi->encoding == REDIS_ENCODING_HT);

    if (what & REDIS_HASH_KEY) {
        *dst = dictGetKey(hi->de);

    } else {
        *dst = dictGetVal(hi->de);
    }
}

/**
 * 从迭代器中提取当前编码的键或值, 以 robj 格式返回
 * 函数负责 incrRefCount, 调用方负责 decrRefCount
 */
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what) {
    robj *dst;

    // ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vlong = LLONG_MAX;

        // 取出域或值
        hashTypeCurrentFromZiplist(hi,what,&vstr,&vlen,&vlong);

        if (vstr) {
            dst = createStringObject(vstr,vlen);
        } else {
            dst = createStringObjectFromLongLong(vlong);
        }

    // hash
    } else if (hi->encoding == REDIS_ENCODING_HT) {

        // 取出域或值
        hashTypeCurrentFromHashTable(hi,what,&dst);
        incrRefCount(dst);

    } else {
        redisPanic("Unknown hash encoding");
    }

    return dst;
}

/**
 * 将 ZIPLIST 编码的哈希对象转换成 enc 编码
 */
void hashTypeConvertZiplist(robj *o, int enc) {
    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);
    
    if (enc == REDIS_ENCODING_ZIPLIST) {
        // nothing to do

    } else if (enc == REDIS_ENCODING_HT){
        hashTypeIterator *hi;
        dict *dict;
        int ret;

        // 迭代器
        hi = hashTypeInitIterator(o->ptr);

        // 创建空哈希表
        dict = dictCreate(&hashDictType,NULL);

        // 迭代压缩列表, 迁移元素
        while(hashTypeNext(hi) != REDIS_ERR) {
            robj *field, *value;

            // 取出 ziplist 的键
            field = hashTypeCurrentObject(hi,REDIS_HASH_KEY);
            field = tryObjectEncoding(field);

            // 取出 ziplist 的值
            value = hashTypeCurrentObject(hi,REDIS_HASH_VALUE);
            value = tryObjectEncoding(value);

            // 键值添加到字典
            ret = dictAdd(dict,field,value);
            if (ret != DICT_OK) {
                redisLogHexDump(REDIS_WARNING,"ziplist with dup elements dump",
                    o->ptr,ziplistBlobLen(o->ptr));
                redisAssert(ret == DICT_OK);
            }
        }

        // 释放迭代器
        hashTypeReleaseIterator(hi);

        // 释放压缩列表
        zfree(o->ptr);

        // 更新编码, 绑定哈希表
        o->encoding = REDIS_ENCODING_HT;
        o->ptr = dict;
    
    } else {
        redisPanic("Unknown hash encoding");
    }
}

/**
 * 将哈希对象的编码进行转换
 * 只对 REDIS_ENCODING_ZIPLIST 执行
 */
void hashTypeConvert(robj *o, int enc) {
    
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        hashTypeConvertZiplist(o, REDIS_ENCODING_HT);

    } else if (o->encoding == REDIS_ENCODING_HT) {
        redisPanic("Not implemented");

    } else {
        redisPanic("Unknown hash encoding");
        
    }
}

/**
 * 对 argv 数组中的多个对象进行检查
 * 尝试将 REDIS_ENCODING_ZIPLIST 转换成 REDIS_ENCODING_HT
 * 只对字符串转换, 因为数值不会超过长度限制
 */
void hashTypeTryConversion(robj *o, robj **argv, int start, int end) {
    int i;

    if (o->encoding != REDIS_ENCODING_ZIPLIST) return;

    for (i = start; i <= end; i++) {
        
        if (sdsEncodedObject(argv[i]) && 
            sdslen(argv[i]) > server.hash_max_ziplist_value)
        {
            hashTypeConvert(o, REDIS_ENCODING_HT);
            break;
        }
    }
}

