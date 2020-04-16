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
 * 
 * 函数主要分为
 * 1. 创建不同编码的对象
 * 
 * 2. 大量的函数处理"字符串对象的值"
 *    - 整数值提取, 错误回复客户端
 *    - 字符串内存压缩
 * 
 * 3. object 命令, 返回对象属性
 *    - 返回 refcount
 *    - 返回 encoding
 *    - 返回 idletime
 */

#include "redis.h"
#include <math.h>
#include <ctype.h>

/*---------------------------------------  -----------------------------------------*/

/*--------------------------------------- Redis对象创建及释放 API -----------------------------------------*/
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
 * 
 * --------------------------------------------------
 * |         redisObject         |      sdshdr      |
 * --------------------------------------------------
 * | type | encoding | ptr | ... | free | len | buf |
 * --------------------------------------------------
 */
robj *createEmbeddedStringObject(char *ptr, size_t len) {

    robj *o = zmalloc(sizeof(robj)+sizeof(struct sdshdr)+len+1);
    // 结合图看, o+1指向 sdshdr 的首地址
    struct sdshdr *sh = (void*)(o+1);

    o->type = REDIS_STRING;
    o->encoding = REDIS_ENCODING_EMBSTR;
    // 结合图看, sh+1 指向 sdshdr 的 buf 的首地址
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

    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) 
        return createEmbeddedStringObject(ptr, len);
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
        while (*p == '0') {
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
        return createEmbeddedStringObject(o->ptr, sdslen(o->ptr));
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
robj *createSetObject(void) {
    dict *d = dictCreate(&setDictType, NULL);

    robj *o = createObject(REDIS_SET, d);

    o->encoding = REDIS_ENCODING_HT;

    return o;
}

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
 * 释放哈希对象
 */
void freeZsetObject(robj *o) {

    zset *zs;

    switch(o->encoding) {
    case REDIS_ENCODING_SKIPLIST:
        zs = o->ptr;
        dictRelease(zs->dict);
        zslFree(zs->zsl);
        zfree(zs);
        break;

    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;

    default:
        // redisPanic("Unknown sorted set encoding");
        break;
    }

}

/**
 * 释放有序集合对象
 */
void freeHashObject(robj *o) {
    
    switch(o->encoding) {
    case REDIS_ENCODING_HT:
        dictRelease((dict*)o->ptr);
        break;

    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    default:
        // redisPanic("Unknown hash encoding type");
        break;
    }
}

/*--------------------------------------- Redis对象引用计数 API -----------------------------------------*/
/**
 * 对象的引用计数加 1
 */
void incrRefCount(robj *o) {
    o->refcount++;
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
        case REDIS_ZSET: freeZsetObject(o); break;
        case REDIS_HASH: freeHashObject(o); break;
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
 * 将对象的引用计数置为 0, 但不释放对象
 * 返回对象是方便其他函数调用, 注入变量达到链式调用的形式, 如下
 * 
 * functionThatWillIncrementRefCount(resetRefCount(CreateObject(...)));
 */
robj *resetRefCount(robj *obj) {
    obj->refcount = 0;
    return obj;
}



/*--------------------------------------- Redis字符串对象值的相关函数 -----------------------------------------*/

/**
 * 检查对象 o 中的值能否转换为 long long 类型：
 * - 可以, 返回 REDIS_OK, 并将整数保存到 *llval 中
 * - 不可以, 返回 REDIS_ERR
 */
int isObjectRepresentableAsLongLong(robj *o, long long *llval) {

    if (o->encoding == REDIS_ENCODING_INT) {
        if (llval) *llval = (long) o->ptr;
        return REDIS_OK;
    } else {
        return string2ll(o->ptr, sdslen(o->ptr), llval) ? REDIS_OK : REDIS_ERR;
    }
}

/**
 * 尝试将字符串对象进行编码转换或压缩
 * 以达到节约内存的目的
 */
robj *tryObjectEncoding(robj *o) {

    long value;
    sds s = o->ptr;
    size_t len;

    // 确保是字符串对象
    // redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

    // embstr 和 raw 才有压缩的空间
    if (!sdsEncodedObject(o)) return o;

    len = sdslen(s);

    // 将 long 型整数的字符串转换为整数
    if (len <= 21 && string2l(s, len, &value)) {

        // todo 还没看到 server部分
        // if (server.maxmemory == 0 && 
        // 共享内存的整数
        if (value >= 0 && value < REDIS_SHARED_INTEGERS) {
            decrRefCount(o);
            incrRefCount(shared.integers[value]);
            return shared.integers[value];
        // long 型整数
        } else {
            
            if (o->encoding == REDIS_ENCODING_RAW) sdsfree(o->ptr);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*) value;
            return o;
        }
    }

    // 尝试将 raw 编码转为 embstr 编码
    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) {
        robj *emb;

        if (o->encoding == REDIS_ENCODING_EMBSTR) return o;
        emb = createEmbeddedStringObject(s, sdslen(s));
        decrRefCount(o);
        return emb;
    }

    // 尝试减少 raw 编码的剩余空间
    if (o->type == REDIS_ENCODING_RAW &&
        sdsavail(s) > len/10)
    {
        o->ptr = sdsRemoveFreeSpace(o->ptr);
    }

    return o;
}

/**
 * 将数字转换成字符串
 * raw 和 embstr 编码, 引用计数加 1 后返回redis对象
 * int 编码, 将整数转换成字符串存入新字符串对象, 并返回新对象
 */
robj *getDecodedObject(robj *o) {
    robj *dec;

    // raw 和 embstr 编码
    if (sdsEncodedObject(o)) {
        incrRefCount(o);
        return o;
    }

    // int 编码, 将其转化为字符串后存入新对象
    if (o->type == REDIS_STRING && o->encoding == REDIS_ENCODING_INT) {
        char buf[32];
        
        ll2string(buf,32,(long)o->ptr);
        dec = createStringObject(buf, strlen(buf));
        return dec;
    } else {
        redisPanic("Unknown encoding type");
    }
}

/* 字符串对象的值比对函数的可选值 strcmp() 或 strcoll 
 * 通过 flags 传入以下值
 */
#define REDIS_COMPARE_BINARY (1<<0)
#define REDIS_COMPARE_COLL (1<<1)

/**
 * 字符串对象的值比对, 返回值通 strcmp
 */
int compareStringObjectsWithFlags(robj *a, robj *b, int flags) {

    char bufa[128], bufb[128], *astr, *bstr;
    size_t alen, blen, minlen;

    // 提取 a 的字符串值
    if (sdsEncodedObject(a)) {
        astr = a->ptr;
        alen = sdslen(astr);
    } else {
        alen = ll2string(bufa,sizeof(bufa),(long) a->ptr);
        astr = bufa;
    }

    // 提取 b 的字符串值
    if (sdsEncodedObject(b)) {
        bstr = b->ptr;
        blen = sdslen(bstr);
    } else {
        blen = ll2string(bufb,sizeof(bufb),(long) b->ptr);
        bstr = bufb;
    }

    // 字符串对比
    if (flags & REDIS_COMPARE_COLL) {
        return strcoll(astr,bstr);
    } else {
        int cmp;

        minlen = (alen < blen) ? alen : blen;
        cmp = memcmp(astr,bstr,minlen);
        if (cmp == 0) return alen-blen;
        return cmp;
    }
}

/* 二进制方式比对字符串对象的值 */
int compareStringObjects(robj *a, robj *b) {
    return compareStringObjectsWithFlags(a, b, REDIS_COMPARE_BINARY);
}

/**
 * 字符串对象值比对
 * 整型编码, 进行数值比对
 * raw, embstr 编码, 进行二进制比对。相同返回 1, 否则返回 0
 */
int equalStringObjects(robj *a, robj *b) {

    // 数值型字符串对象
    // 避免将整数值转换成字符串后再比对, 效率更高
    if (a->encoding == REDIS_ENCODING_INT &&
        b->encoding == REDIS_ENCODING_INT) {
        
        return a->ptr == b->ptr;
    
    // 字符串比对
    } else {
        return compareStringObjects(a,b) == 0;
    }
}

/**
 * 字符串对象的值长度
 */
size_t stringObjectLen(robj *o) {

    // redisAssertWithInfo(NULL, o, o->type == REDIS_STRING);
    if (sdsEncodedObject(o)) {
        return sdslen(o->ptr);
    } else {
        char buf[32];
        return ll2string(buf,32,(long) o->ptr);
    }
}

/**
 * 尝试从字符串对象的值转换成 double 型整数
 * - 转换成功, 将值保存到 *target 中, 返回 REDIS_OK
 * - 否则, 返回 REDIS_ERR
 */
int getDoubleFromObject(robj *o, double *target) {

    double value;
    char *eptr;

    // 对象不存在, value给 0
    if (o == NULL) {
        value = 0;

    // 对象存在
    } else {
        // 确定是字符串对象
        // redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

        // raw 或 embstr 编码, 将字符串转换成整数
        if (sdsEncodedObject(o)) {
            errno = 0;
            value = strtod(o->ptr, &eptr);
            // todo 暂时不知道这是啥, 过后看
            if (isspace(((char*)o->ptr)[0]) ||
                eptr[0] != '\0' ||
                (errno == ERANGE &&
                    (value == HUGE_VAL || value == -HUGE_VAL || value == 0)) ||
                errno == EINVAL ||
                isnan(value))
                return REDIS_ERR;

        // int 编码, 提取
        } else if(o->encoding == REDIS_ENCODING_INT) {
            value = (long)o->ptr;

        // 不存在的编码
        } else {
            // redisPanic("Unknown string encoding");
        }
    }

    *target = value;
    return REDIS_OK;
}

/**
 * todo redisClient 还没看, 此函数先屏蔽
 * 尝试从字符串对象 o 中提取 double 值
 * - 提取失败, 返回 msg 给客户端, 函数返回 REDIS_ERR
 * - 提取成功, 将提取值存储在 *target 中, 函数返回 REDIS_OK
 */
int getDoubleFromObjectOrReply(redisClient *c, robj *o, double *target, const char *msg) {
    double value;

    // 提取失败
    if (getDoubleFromObject(o, &value) != REDIS_OK) {

        if (msg != NULL) {
            addReplyError(c, (char*)msg);
        } else {
            addReplyError(c, "value is not a valid float");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

/**
 * 尝试从字符串对象的值转换成 long double 型整数
 * - 转换成功, 将值保存到 *target 中, 返回 REDIS_OK
 * - 否则, 返回 REDIS_ERR
 */
int getLongDoubleFromObject(robj *o, long double *target) {

    long double value;
    char *eptr;

    if (o == NULL) {
        value = 0;
    } else {
        // redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

        if (sdsEncodedObject(o)) {
            errno = 0;
            value = strtold(o->ptr, &eptr);
            // isspace(((char*)o->ptr)[0]) => ptr已空白符开始, 
            // eptr[0] != '\0'             => ptr取出整数之后还有别的字符
            // errno == ERANGE             => value 超出程序所能表达的数值范围
            // isnan(value)                => value非数值
            if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' ||
                errno == ERANGE || isnan(value))
                return REDIS_ERR;
        } else if (o->encoding == REDIS_ENCODING_INT) {
            value = (long)o->ptr;
        } else {
            redisPanic("Unknown string encoding");
        }
    }

    *target = value;
    return REDIS_OK;
}

/**
 * todo redisClient 还没看, 此函数先屏蔽
 * 尝试从字符串对象 o 中提取 long double 值
 * - 提取失败, 返回 msg 给客户端, 函数返回 REDIS_ERR
 * - 提取成功, 将提取值存储在 *target 中, 函数返回 REDIS_OK
 */
int getLongDoubleFromObjectOrReply(redisClient *c, robj *o, long double *target, const char *msg) {
    long double value;

    if (getLongDoubleFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c, (char*)msg);
        } else {
            addReplyError(c, "value is not a valid float");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}


/**
 * 尝试从字符串对象的值转换成 long long 型整数
 * - 转换成功, 将值保存到 *target 中, 返回 REDIS_OK
 * - 否则, 返回 REDIS_ERR
 */
int getLongLongFromObject(robj *o, long long *target) {
    long long value;
    char *eptr;

    if (o == NULL) {
        // o 为 NULL 时，将值设为 0 。
        value = 0;
    } else {

        // 确保对象为 REDIS_STRING 类型
        // redisAssertWithInfo(NULL,o,o->type == REDIS_STRING);

        if (sdsEncodedObject(o)) {
            errno = 0;
            // T = O(N)
            value = strtoll(o->ptr, &eptr, 10);
            if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' ||
                errno == ERANGE)
                return REDIS_ERR;
        } else if (o->encoding == REDIS_ENCODING_INT) {
            // 对于 REDIS_ENCODING_INT 编码的整数值
            // 直接将它的值保存到 value 中
            value = (long)o->ptr;
        } else {
            // redisPanic("Unknown string encoding");
        }
    }

    // 保存值到指针
    if (target) *target = value;

    // 返回结果标识符
    return REDIS_OK;
}

/*
 * 尝试从对象 o 中取出整数值，
 * 或者尝试将对象 o 中的值转换为整数值，
 * 并将这个得出的整数值保存到 *target 。
 *
 * 如果取出/转换成功的话，返回 REDIS_OK 。
 * 否则，返回 REDIS_ERR ，并向客户端发送一条出错回复。
 *
 * T = O(N)
 */
int getLongLongFromObjectOrReply(redisClient *c, robj *o, long long *target, const char *msg) {

    long long value;

    // T = O(N)
    if (getLongLongFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c,(char*)msg);
        } else {
            addReplyError(c,"value is not an integer or out of range");
        }
        return REDIS_ERR;
    }

    *target = value;

    return REDIS_OK;
}

/*
 * 尝试从对象 o 中取出 long 类型值，
 * 或者尝试将对象 o 中的值转换为 long 类型值，
 * 并将这个得出的整数值保存到 *target 。
 *
 * 如果取出/转换成功的话，返回 REDIS_OK 。
 * 否则，返回 REDIS_ERR ，并向客户端发送一条 msg 出错回复。
 */
int getLongFromObjectOrReply(redisClient *c, robj *o, long *target, const char *msg) {
    long long value;

    // 先尝试以 long long 类型取出值
    if (getLongLongFromObjectOrReply(c, o, &value, msg) != REDIS_OK) return REDIS_ERR;

    // 然后检查值是否在 long 类型的范围之内
    if (value < LONG_MIN || value > LONG_MAX) {
        if (msg != NULL) {
            addReplyError(c,(char*)msg);
        } else {
            addReplyError(c,"value is out of range");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

/*--------------------------------------- Redis对象类型 API -----------------------------------------*/
/**
 * todo redisClient 还没看, 暂不开放
 * 检查对象 o 的类型是否和 type 相同
 * 相同返回 0
 * 不同返回 1, 并先客户端回复一个错误
 */
int checkType(redisClient *c, robj *o, int type) {
    
    if (o->type != type) {
        addReply(c,shared.wrongtypeerr);
        return 1;
    }

    return 0;
}

/*--------------------------------------- OBJECT 命令函数 -----------------------------------------*/

/**
 * 返回编码类型的英文名称
 */
char *strEncoding(int encoding) {

    switch(encoding){
    case REDIS_ENCODING_RAW: return "raw";
    case REDIS_ENCODING_INT: return "int";
    case REDIS_ENCODING_HT: return "hashtable";
    case REDIS_ENCODING_LINKEDLIST: return "linkedlist";
    case REDIS_ENCODING_ZIPLIST: return "ziplist";
    case REDIS_ENCODING_INTSET: return "intset";
    case REDIS_ENCODING_SKIPLIST: return "skiplist";
    case REDIS_ENCODING_EMBSTR: return "emstr";
    default: return "unknown";
    }
}

/**
 * 使用近似 LRU 算法, 计算出对象的闲置时长
 */
unsigned long long estimateObjectIdleTime(robj *o) {
    unsigned long long lruclock = LRU_CLOCK();
    if (lruclock >= o->lru) {
        return (lruclock - o->lru) * REDIS_LRU_CLOCK_RESOLUTION;
    } else {
        return (lruclock + (REDIS_LRU_CLOCK_MAX - o->lru)) * 
                    REDIS_LRU_CLOCK_RESOLUTION;
    }
}

/**
 * OBJECT 命令的辅助函数, 在不修改 LRU 时间的情况下, 尝试获取 key 对象
 */
robj *objectCommandLookup(redisClient *c, robj *key) {
    dictEntry *de;

    if ((de = dictFind(c->db->dict, key->ptr)) == NULL) return NULL;

    return (robj*)dictGetVal(de);
}

/**
 * 在不修改 LRU 时间的情况下, 尝试获取 key 对象
 * 如果对象不存在, 先客户端发送 reply
 */
robj *objectCommandLookupOrReply(redisClient *c, robj *key, robj *reply) {

    robj *o = objectCommandLookup(c, key);
    if (!o) addReply(c, reply);
    return o;
}

/**
 * 处理 OBJECT 命令
 */
void objectCommand(redisClient *c) {

    robj *o;

    // 返回指定 key 的引用计数的值
    if (strcasecmp(c->argv[1]->ptr, "refcount") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c, c->argv[2], shared.nullbulk)) == NULL)
            return;
        addReplyLongLong(c, o->refcount);

    // 返回指定 key 的编码
    } else if (strcasecmp(c->argv[1]->ptr, "encoding") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c, c->argv[2], shared.nullbulk)) == NULL)
            return;
        addReplyBulkCString(c, strEncoding(o->encoding));

    // 返回指定 key 的空间时间
    } else if (strcasecmp(c->argv[1]->ptr, "idletime") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c, c->argv[2], shared.nullbulk)) == NULL)
            return;
        addReplyLongLong(c, estimateObjectIdleTime(o)/1000);
    } else {
        addReplyError(c,"Syntax error. Try OBJECT (refcount|encoding|idletime)");
    }

}

#include <assert.h>

// 字符串：gcc -g zmalloc.c sds.c object.c
// 字符串、列表：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c object.c
// 字符串、列表、集合、哈希：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c object.c
// 字符串、列表、集合、哈希、有序集合：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c t_zset.c object.c
// gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c t_zset.c redis.c object.c
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
        o = createEmbeddedStringObject("embstr string", 13);
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

    // 创建并释放 intset 的空集合对象
    printf("create and free intset set object: ");
    {
        o = createIntsetObject();
        assert(o->type == REDIS_SET);
        assert(o->encoding == REDIS_ENCODING_INTSET);
        freeSetObject(o);
        printf("OK\n");
    }

    // 创建并释放一个 哈希对象
    printf("create and free hash object: ");
    {
        o = createHashObject();
        assert(o->type == REDIS_HASH);
        assert(o->encoding == REDIS_ENCODING_ZIPLIST);
        freeHashObject(o);
        printf("OK\n");
    }

    // 创建并释放 SKIPLIST 编码的有序集合对象
    printf("create and free skiplist zset object: ");
    {
        o = createZsetObject();
        assert(o->type == REDIS_ZSET);
        assert(o->encoding == REDIS_ENCODING_SKIPLIST);
        freeZsetObject(o);
        printf("OK\n");
    }
    
    // 创建并释放 ZIPLIST 编码的有序集合对象
    printf("create and free ziplist zset object: ");
    {
        o = createZsetZiplistObject();
        assert(o->type == REDIS_ZSET);
        assert(o->encoding == REDIS_ENCODING_ZIPLIST);
        freeZsetObject(o);
        printf("OK\n");
    }
}