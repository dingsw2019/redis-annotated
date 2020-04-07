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
        
        // incrRefCount(shared.integers[value]);
        // o = shared.integers[value];
    // 不符合共享范围, 创建一个新的整数对象
    } else {

        // 值可以用 long 类型保存
        // 创建一个 REDIS_ENCODING_INT 编码的字符串对象
        if (value >= LONG_MIN && value <= LONG_MAX) {
            o = createObject(REDIS_STRING, NULL);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*)((long)value);
        
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
    len = snprintf(buf, sizeof(buf), "%.17Lf", value);

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
 * 复制一个字符串对象
 * 估计在 COW 时使用
 */
robj *dupStringObject(robj *o) {
    robj *d;

    // redisAssert(o->type == REDIS_STRING);

    switch(o->encoding) {
    case REDIS_ENCODING_RAW:
        return createRawStringObject(o->ptr, sdslen(o->ptr));
    case REDIS_ENCODING_EMBSTR:
        return createEmbeddeStringObject(o->ptr, sdslen(o->ptr));
    case REDIS_ENCODING_INT:
        d = createObject(REDIS_STRING, NULL);
        d->encoding = REDIS_ENCODING_INT;
        d->ptr = o->ptr;
        return d;
    default:
        // redisPanic("Wrong encoding.");
        break;
    }
}

/**
 * 创建一个 LINKEDLIST 编码的列表对象
 */
robj *createListObject(void) {

    // 创建链表
    list *l = listCreate();
    
    robj *o = createObject(REDIS_LIST, l);

    listSetFreeMethod(l,decrRefCountVoid);

    o->encoding = REDIS_ENCODING_LINKEDLIST;

    return o;
}

/**
 * 创建一个 ZIPLIST 编码的列表对象
 */
robj *createZiplistObject(void) {

    unsigned char *zl = ziplistNew();

    robj *o = createObject(REDIS_LIST, zl);

    o->encoding = REDIS_ENCODING_ZIPLIST;

    return o;
}

/**
 * 创建一个 HT 编码的空集合对象
 */
// robj *createSetObject(void) {
//     dict *d = dictCreate(&setDictType, NULL);

//     robj *o = createObject(REDIS_SET, d);

//     o->encoding = REDIS_ENCODING_HT;

//     return o;
// }

/**
 * 创建一个 INTSET 编码的空集合对象
 */
robj *createIntsetObject(void) {

    intset *is = intsetNew();

    robj *o = createObject(REDIS_SET, is);

    o->encoding = REDIS_ENCODING_INTSET;

    return o;
}


/**
 * 创建一个 ZIPLIST 编码的空哈希对象
 */
robj *createHashObject(void) {

    unsigned char *zl = ziplistNew();

    robj *o = createObject(REDIS_HASH, zl);

    o->encoding = REDIS_ENCODING_ZIPLIST;

    return o;
}

/**
 * 创建一个 SKIPLIST 编码的空的有序集合
 */
robj *createZsetObject(void) {

    zset *zs = zmalloc(sizeof(*zs));

    robj *o;

    zs->dict = dictCreate(&zsetDictType, NULL);
    zs->zsl = zslCreate();

    o = createObject(REDIS_ZSET, zs);

    o->encoding = REDIS_ENCODING_SKIPLIST;

    return o;
}

/**
 * 创建一个 ZIPLIST 编码的空的有序集合
 */
robj *createZsetZiplistObject(void) {

    unsigned char *zl = ziplistNew();

    robj *o = createObject(REDIS_ZSET, zl);

    o->encoding = REDIS_ENCODING_ZIPLIST;

    return o;
}


/**
 * 对象的引用计数减 1
 * 当对象的引用计数为 0 时, 释放对象
 */
void decrRefCount(robj *o) {

    if (o->refcount <= 0) 
        return ;
        // redisPanic("decrRefCount against refcount <= 0");

    // 释放对象
    if (o->refcount == 1) {
        switch(o->type) {
        case REDIS_STRING: freeStringObject(o); break;
        case REDIS_LIST: freeListObject(o); break;
        case REDIS_SET: freeSetObject(o); break;
        // case REDIS_ZSET: freeZsetObject(o); break;
        // case REDIS_HASH: freeHashObject(o); break;
        default: /*redisPanic("Unknown object type");*/ break;
        }
        zfree(o);
    } else {
        o->refcount--;
    }
}

/**
 * 特定数据结构的释放函数包装
 */
void decrRefCountVoid(void *o) {
    decrRefCount(o);
}

/**
 * 释放字符串对象
 */
void freeStringObject(robj *o) {
    if (o->encoding == REDIS_ENCODING_RAW) {
        sdsfree(o->ptr);
    }
}

/**
 * 释放列表对象
 */
void freeListObject(robj *o) {

    switch(o->encoding) {
    case REDIS_ENCODING_LINKEDLIST:
        listRelease((list*) o->ptr);
        break;

    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    default:
        // redisPanic("Unknown list encoding type");
        break;
    }
}

void freeSetObject(robj *o) {

    switch(o->encoding) {
    case REDIS_ENCODING_HT: 
        dictRelease((dict*) o->ptr); 
        break;

    case REDIS_ENCODING_INTSET: 
        zfree(o->ptr);
        break;

    default:
        // redisPanic("Unknown set encoding type");
        break;
    }
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

#include <assert.h>

// 字符串对象：gcc -g zmalloc.c sds.c object.c
// 字符串对象、列表对象：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c object.c
int main () {

    robj *o,*dup;

    // 创建 raw 编码的字符串对象
    printf("create raw string object: ");
    {
        o = createRawStringObject("raw string", 10);
        assert(o->type == REDIS_STRING);
        assert(o->encoding == REDIS_ENCODING_RAW);
        assert(!sdscmp(o->ptr,sdsnew("raw string")));
        printf("OK\n");
    }

    // 创建 embstr 编码的字符串对象
    printf("create embstr string object: ");
    {
        freeStringObject(o);
        o = createEmbeddeStringObject("embstr string", 13);
        assert(o->type == REDIS_STRING);
        assert(o->encoding == REDIS_ENCODING_EMBSTR);
        assert(!sdscmp(o->ptr,sdsnew("embstr string")));
        printf("OK\n");
    }

    // 根据字符串长度, 选择 embstr 或 raw 编码的字符串对象
    printf("create 41 bytes raw string object: ");
    {
        o = createStringObject("long long long long long long long string",41);
        assert(o->type == REDIS_STRING);
        assert(o->encoding == REDIS_ENCODING_RAW);
        assert(!sdscmp(o->ptr,sdsnew("long long long long long long long string")));
        printf("OK\n");
    }

    // 创建一个 int 编码的字符串对象
    printf("create int string object: ");
    {
        freeStringObject(o);
        o = createStringObjectFromLongLong(123456789);
        assert(o->type == REDIS_STRING);
        assert(o->encoding == REDIS_ENCODING_INT);
        assert((long long)o->ptr == 123456789);
        printf("OK\n");
    }

    // 创建一个浮点型的字符串对象(warn 有精度问题)
    // printf("create double string object:");
    // {
    //     o = createStringObjectFromLongDouble(3.14000000);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_EMBSTR);
    //     assert(!sdscmp(o->ptr,sdsnew("3.14")));
    //     printf("OK\n");
    // }

    // 复制字符串对象
    printf("duplicate int string object: ");
    {
        dup = dupStringObject(o);
        assert(dup->type == REDIS_STRING);
        assert(dup->encoding == REDIS_ENCODING_INT);
        assert((long long)dup->ptr == 123456789);
        printf("OK\n");
    }

    // 创建一个 list 编码的空列表对象
    printf("create and free list list object: ");
    {
        o = createListObject();
        assert(o->type == REDIS_LIST);
        assert(o->encoding == REDIS_ENCODING_LINKEDLIST);
        freeListObject(o);
        printf("OK\n");
    }
    

    // 创建一个 ziplist 编码的空列表对象
    printf("create and free ziplist list object: ");
    {
        o = createZiplistObject();
        assert(o->type == REDIS_LIST);
        assert(o->encoding == REDIS_ENCODING_ZIPLIST);
        freeListObject(o);
        printf("OK\n");
    }

    // 创建一个 intset 的空集合对象
    printf("create and free intset set object: ");
    {
        o = createIntsetObject();
        assert(o->type == REDIS_SET);
        assert(o->encoding == REDIS_ENCODING_INTSET);
        freeSetObject(o);
        printf("OK\n");
    }


}