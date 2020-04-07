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