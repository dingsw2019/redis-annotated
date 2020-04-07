/**
 * Redis对象操作实现
 * 
 * 对象生存周期
 * 1. 创建对象
 * 2. 操作对象
 * 3. 释放对象
 * 
 * 此类完成 1、3 步骤, 2 在每个对象类型的文件中完成 
 * (t_string.c, t_hash.c, t_list.c, t_zset.c, t_set.c)
 */

#include "redis.h"
#include <math.h>
#include <ctype.h>

/**
 * 创建并返回 robj 对象
 */
robj *createObject(int type, void *ptr) {

    // 申请内存空间
    robj *o = zmalloc(sizeof(*o));

    // 设置属性
    o->type = type;
    o->encoding = REDIS_ENCODING_RAW;
    o->ptr = ptr;
    o->refcount = 1;

    o->lru = LRU_CLOCK();

    return o;
}

/**
 * 创建并返回一个 REDIS_ENCODING_RAW 编码的字符串
 * 对象的指针指向一个 sds 结构
 */
robj *createRawStringObject(char *ptr, size_t len) {
    return createObject(REDIS_STRING,sdsnewlen(ptr,len));
}

/**
 * 创建并返回一个 REDIS_ENCODING_EMBSTR 编码的字符对象
 * 在此函数中分配 sds 内存, 因为 embstr字符不可修改
 */
robj *createEmbeddeStringObject(char *ptr, size_t len) {

    robj *o = zmalloc(sizeof(robj)+sizeof(struct sdshdr)+len+1);
    struct sdshdr *sh = (void*)(o+1);

    o->type = REDIS_STRING;
    o->encoding = REDIS_ENCODING_EMBSTR;
    o->ptr = sh+1;
    o->refcount = 1;
    o->lru = LRU_CLOCK();

    // 设置 sds
    sh->len = len;
    sh->free = 0;
    if (ptr) {
        memcpy(sh->buf, ptr, len);
        sh->buf[len] = '\0';
    } else {
        memset(sh->buf, 0, len+1);
    }

    return o;
}

#define REDIS_ENCODING_EMBSTR_SIZE_LIMIT 39
/**
 * 创建并返回一个字符串对象
 * 字符串长度小于 REDIS_ENCODING_EMBSTR_SIZE_LIMIT, 使用 embstr存储
 * 字符串长度大于 REDIS_ENCODING_EMBSTR_SIZE_LIMIT, 使用 raw 存储
 */
robj *createStringObject(char *ptr, size_t len) {

    if (len < REDIS_ENCODING_EMBSTR_SIZE_LIMIT) 
        return createEmbeddeStringObject(ptr, len);
    else 
        return createRawStringObject(ptr, len);
}

/**
 * 存储 long 型整数到字符串对象
 * 保存的可以是 INT 编码的 long 值, 
 * 也可以是 RAW 编码的, 被转换成字符串的 long long 值
 */
robj *createStringObjectFromLongLong(long long value) {

    robj *o;

    // value 的大小符合 REDIS共享整数的范围
    // 返回共享对象
    if (value >= 0 && value < REDIS_SHARED_INTEGERS) {
        
        incrRefCount(shared.integers[value]);
        o = shared.integers[value];
    // 不符合共享范围, 创建一个新的整数对象
    } else {

        // 值可以用 long 类型保存
        // 创建一个 REDIS_ENCODING_INT 编码的字符串对象
        if (value >= LONG_MIN && value <= LONG_MAX) {
            o = createObject(REDIS_STRING, NULL);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void *)((long)value);
        
        // 值不能用 long 类型保存, 将值转换成字符串
        // 并创建一个 REDIS_ENCODING_RAW 的字符串对象来保存值
        } else {
            o = createObject(REDIS_STRING, sdsfromlonglong(value));
        }
    }

    return o;
}

/**
 * 将 long double 转为字符串存入字符串对象
 */
robj *createStringObjectFromLongDouble(long double value) {

    char buf[256];
    int len;

    // 使用 17 位小数精度, 这种精度可以在大部分机器上被 rounding 而不改变
    len = snprintf(buf, sizeof(buf), '%.17f', value);

    // 移除尾部的 0
    // 例如将 3.140000000 变成 3.14
    // 将 3.0000 变成 3
    if (strchr(buf, '.') != NULL) {
        char *p = buf+len-1;
        if (*p == '0') {
            p--;
            len--;
        }

        if (*p == '.') len--;
    }

    return createStringObject(buf, len);
}







/**
 * 对象的引用计数加 1
 */
void incrRefCount(robj *o) {
    o->refcount++;
}

/**
 * 将对象的引用计数置为 0, 但不释放对象
 * 返回对象是方便其他函数调用, 注入变量达到链式调用的形式, 如下
 * 
 * functionThatWillIncrementRefCount(resetRefCount(CreateObject(...)));
 */
robj *resetRefCount(robj *obj) {
    obj->refcount = 0;
    return obj;
}