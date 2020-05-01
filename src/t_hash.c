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