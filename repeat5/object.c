#include "redis.h"
#include <math.h>
#include <ctype.h>

/*--------------------------------------- Redis对象创建及释放 API -----------------------------------------*/
// 创建一个指定类型的 robj
robj *createObject(int type, void *ptr) {

    // 申请内存空间
    robj *o = zmalloc(sizeof(*o));

    // 初始化属性
    o->type = type;
    o->encoding = REDIS_ENCODING_RAW;
    o->lru = LRU_CLOCK();
    o->refcount = 1;
    o->ptr = ptr;

    return o;
}

// 创建一个 raw 编码的字符串对象
robj *createRawStringObject(char *ptr, size_t len) {
    return createObject(REDIS_STRING, sdsnewlen(ptr, len));
}

// 创建一个 embstr 编码的字符串的对象
// robj + sdshdr 结构, ptr 字节存在 sdshdr中, 更快存取
robj *createEmbeddedStringObject(char *ptr, size_t len) {

    // 创建内存空间
    robj *o = zmalloc(sizeof(robj)+sizeof(struct sdshdr)+len+1);
    struct sdshdr *sh = (void*)(o+1);

    // 设置属性
    o->type = REDIS_STRING;
    o->encoding = REDIS_ENCODING_EMBSTR;
    o->lru = LRU_CLOCK();
    o->refcount = 1;
    o->ptr = sh+1;

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

// 创建 embstr 或 raw 编码的字符串对象
robj *createStringObject(char *ptr, size_t len) {

    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) {
        return createEmbeddedStringObject(ptr, len);
    } else {
        return createRawStringObject(ptr, len);
    }
}

// 整数存入 robj
robj *createStringObjectFromLongLong(long long value) {
    robj *o;

    // < 10000 使用共享整数
    if (value >= 0 && value < REDIS_SHARED_INTEGERS) {
        incrRefCount(shared.integers[value]);
        o = shared.integers[value];

    // LONG 整数范围, ptr 中存入整数
    } else {
        if (value >= LONG_MIN && value <= LONG_MAX) {
            o = createObject(REDIS_STRING, NULL);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*)((long)value);

        // 超出 LONG 范围, 转换成字符串后存入
        } else {
            o = createObject(REDIS_STRING, sdsfromlonglong(value));
        }
    }

    return o;
}

// 小数已字符串格式存入字符串对象
robj *createStringObjectFromLongDouble(long double value) {
    size_t len;
    char buf[256];

    // value 转成字符串存入 buf, len 是转换的字符串的长度
    len = snprintf(buf,sizeof(buf),"%.17Lf", value);

    // 清除无用的 0, 比如 3.14000000 => 3.14
    // 3.000 => 3
    if (strchr(buf,'.') != NULL) {
        char *p = buf+len-1;
        while (*p == '0') {
            p--;
            len--;
        }

        if (*p == '.') len--;
    }

    return createStringObject(buf, len);
}

// 复制字符串对象, 三种编码
robj *dupStringObject(robj *o) {
    robj *d;
    switch(o->encoding) {
    case REDIS_ENCODING_RAW: return createRawStringObject(o->ptr, sdslen(o->ptr));
    case REDIS_ENCODING_EMBSTR: return createEmbeddedStringObject(o->ptr, sdslen(o->ptr));
    case REDIS_ENCODING_INT:     
        d = createObject(REDIS_STRING, NULL);
        d->encoding = REDIS_ENCODING_INT;
        d->ptr = o->ptr;
        return d;
    default:
        redisPanic("Wrong encoding");
        break;
    }
}

// 创建 LINKEDLIST 编码的列表
robj *createListObject(void) {

    list *l = listCreate();

    robj *o = createObject(REDIS_LIST, l);

    listSetFreeMethod(l, decrRefCountVoid);

    o->encoding = REDIS_ENCODING_LINKEDLIST;

    return o;
}

// 创建 ZIPLIST 编码的列表
robj *createZiplistObject(void) {
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_LIST, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

// HT 编码的集合
robj *createSetObject(void) {
    dict *d = dictCreate(&setDictType,NULL);
    robj *o = createObject(REDIS_SET, d);
    o->encoding = REDIS_ENCODING_HT;
    
    return o;
}

// INTSET 编码的集合
robj *createIntsetObject(void) {
    intset *is = intsetNew();
    robj *o = createObject(REDIS_SET, is);
    o->encoding = REDIS_ENCODING_INTSET;
    return o;
}

// ZIPLIST 编码的哈希表
robj *createHashObject(void) {
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_HASH, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

// 跳跃表编码的有序集合
robj *createZsetObject(void) {
    zset *zs = zmalloc(sizeof(*zs));
    zs->dict = dictCreate(&zsetDictType,NULL);
    zs->zsl = zslCreate();

    robj *o = createObject(REDIS_ZSET, zs);
    o->encoding = REDIS_ENCODING_SKIPLIST;
    return o;
}

// ZIPLIST 编码的有序集合
robj *createZsetZiplistObject(void) {
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_ZSET, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

// 释放字符串对象
void freeStringObject(robj *o) {
    if (o->encoding == REDIS_ENCODING_RAW) {
        sdsfree(o->ptr);
    }
}

// 释放列表对象
void freeListObject(robj *o) {
    switch(o->encoding) {
    case REDIS_ENCODING_LINKEDLIST: 
        listRelease((list*)o->ptr);
        break;

    case REDIS_ENCODING_ZIPLIST: 
        zfree(o->ptr);
        break;

    default:
        redisPanic("Unknown list encoding type");
        break;
    }
}

// 释放集合对象
void freeSetObject(robj *o) {
    switch(o->encoding) {
    case REDIS_ENCODING_HT:
        dictRelease((dict*) o->ptr);
        break;

    case REDIS_ENCODING_INTSET:
        zfree(o->ptr);
        break;
    default:
        redisPanic("Unknown set encoding type");
        break;
    }
}

// 释放有序集合对象
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
        redisPanic("Unknown zset encoding type");
        break;
    }
}

// 释放哈希对象
void freeHashObject(robj *o) {
    switch(o->encoding) {
    case REDIS_ENCODING_HT:
        dictRelease((dict*) o->ptr);
        break;

    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    default:
        redisPanic("Unknown list encoding type");
        break;
    }
}

/*--------------------------------------- Redis对象引用计数 API -----------------------------------------*/
void incrRefCount(robj *o) {
    o->refcount++;
}

void decrRefCount(robj *o) {

    if (o->refcount <= 0) 
        redisPanic("decrRefCount against refcount <= 0");

    if (o->refcount == 1) {
        switch(o->type) {
        case REDIS_STRING: freeStringObject(o); break;
        case REDIS_LIST: freeListObject(o); break;
        case REDIS_SET: freeSetObject(o); break;
        case REDIS_ZSET: freeZsetObject(o); break;
        case REDIS_HASH: freeHashObject(o); break;
        default:redisPanic("Unknown object type"); break;
        }

        zfree(o);
    } else {
        o->refcount--;
    }
}

void decrRefCountVoid(void *o) {
    decrRefCount(o);
}

robj *resetRefCount(robj *obj) {
    obj->refcount = 0;
    return obj;
}

/*--------------------------------------- Redis字符串对象值的相关函数 -----------------------------------------*/

// 读取 o 的值,存入 llval 中
// 如果是字符串, 尝试转成整数
int isObjectRepresentableAsLongLong(robj *o, long long *llval) {
    
    if (o->encoding == REDIS_ENCODING_INT) {
        if (llval) *llval = (long) o->ptr;
        return REDIS_OK;
    } else {
        return string2ll(o->ptr, sdslen(o->ptr), llval) ? REDIS_OK : REDIS_ERR;
    }
}

// 压缩字符串对象的内存
// 21位整数, 转换成 int 编码
// 39字节以下字符串, 转换成 embstr 编码
// raw 编码的空闲长度如果大于已用长度的1/10, 回收空闲长度
robj *tryObjectEncoding(robj *o) {
    long value;
    sds s = o->ptr;
    size_t len;

    // 只处理 raw, embstr 编码
    if (!sdsEncodedObject(o)) return o;

    // 计算值长度
    len = sdslen(s);

    // 值为整数, 转换成 int 编码
    if (len <= 21 && string2l(s, len, &value)) {

        if (value >=0 && value < REDIS_SHARED_INTEGERS) {
            decrRefCount(o);
            incrRefCount(shared.integers[value]);
            return shared.integers[value];

        } else {
            if (o->encoding == REDIS_ENCODING_RAW) sdsfree(o->ptr);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*) value;
            return o;
        }
    }

    // raw 编码但长度小于 39, 转换成 embstr 编码
    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) {
        robj *emb;
        if (o->encoding == REDIS_ENCODING_EMBSTR) return o;
        emb = createEmbeddedStringObject(s, sdslen(s));
        decrRefCount(o);
        return emb;
    }

    // raw 编码, 回收空闲长度
    if (o->encoding == REDIS_ENCODING_RAW && sdsavail(s) > len/10) {
        o->ptr = sdsRemoveFreeSpace(o->ptr);
    }

    return o;
}

// 将 int 编码的值转换成 embstr或raw编码
robj *getDecodedObject(robj *o) {
    robj *dec;

    if (sdsEncodedObject(o)) {
        incrRefCount(o);
        return o;
    }

    if (o->type == REDIS_STRING && o->encoding == REDIS_ENCODING_INT) {
        char buf[32];

        ll2string(buf,32,(long) o->ptr);
        dec = createStringObject(buf, strlen(buf));
        return dec;

    } else {
        redisPanic("Unknown encoding type");
    }
}

#define REDIS_COMPARE_BINARY (1<<0)
#define REDIS_COMPARE_COLL (1<<1)

// 比对两个字符串对象的值, flag 是比对方式
// 值为整数, 将其转换成字符串后比对
int compareStringObjectWithFlags(robj *a, robj *b, int flags) {
    char bufa[128], bufb[128], *astr, *bstr;
    size_t alen, blen, minlen;

    // 值和长度
    if (sdsEncodedObject(a)) {
        astr = a->ptr;
        alen = sdslen(a->ptr);
    } else {
        alen = ll2string(bufa,sizeof(bufa),(long) a->ptr);
        astr = bufa;
    }

    if (sdsEncodedObject(b)) {
        bstr = b->ptr;
        blen = sdslen(b->ptr);
    } else {
        blen = ll2string(bufb, sizeof(bufb), (long)b->ptr);
        bstr = bufb;
    }

    if (flags & REDIS_COMPARE_COLL) {
        return strcoll(astr, bstr);
    } else {
        int cmp;
        minlen = (alen < blen) ? alen : blen;
        cmp = memcmp(astr, bstr, minlen);
        if (cmp == 0) return alen-blen;
        return cmp;
    }
}

int compareStringObjects(robj *a, robj *b) {
    return compareStringObjectWithFlags(a, b, REDIS_COMPARE_BINARY);
}

// 相同返回 1, 否则返回 0
int equalStringObjects(robj *a, robj *b) {

    if (a->encoding == REDIS_ENCODING_INT &&
        b->encoding == REDIS_ENCODING_INT) {
        
        return a->ptr == b->ptr;
    
    } else {
        return compareStringObjects(a,b) == 0;
    }
}

// 字符串对象的长度
size_t stringObjectLen(robj *o) {

    if (sdsEncodedObject(o)) {
        return sdslen(o->ptr);
    } else {
        char buf[32];
        return ll2string(buf,32,(long)o->ptr);
    }
}

// 从字符串对象中提取 double类型的数字
int getDoubleFromObject(robj *o, double *target) {
    double value;
    char *eptr;

    if (o == NULL) {
        value = 0;
    } else {

        redisAssertWithInfo(NULL, o, o->type == REDIS_STRING);

        if (sdsEncodedObject(o)) {
            errno = 0;
            value = strtod(o->ptr, &eptr);
            
            if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' ||
                (errno == ERANGE && (value == HUGE_VAL || value == -HUGE_VAL || value == 0)) ||
                errno == EINVAL || isnan(value))
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

// 从字符串对象中提取 double 类型的数字
// 如果提取失败, 回复客户端 msg 里面的信息
int getDoubleFromObjectOrReply(redisClient *c, robj *o, double *target, const char *msg) {
    double value;
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

// 从字符串对象中提取 long double 类型的数字
int getLongDoubleFromObject(robj *o, long double *target) {
    long double value;
    char *eptr;

    if (o == NULL) {
        value = 0;
    } else {
        redisAssertWithInfo(NULL, o, o->type == REDIS_STRING);
        if (sdsEncodedObject(o)) {
            errno = 0;
            value = strtold(o->ptr, &eptr);
            if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' || 
                errno == ERANGE || isnan(value)) {
                return REDIS_ERR;
            }

        } else if (o->encoding == REDIS_ENCODING_INT) {
            value = (long)o->ptr;
        } else {
            redisPanic("Unknown string encoding");
        }
    }

    *target = value;
    return REDIS_OK;
}

// 从字符串对象中提取 long double 类型的数字
// 如果提取失败, 回复客户端 msg 里的信息
int getLongDoubleFromObjectOrReply(redisClient *c, robj *o, long double *target, const char *msg) {
    long double value;

    if (getLongDoubleFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c, (char*)msg);
        } else {
            addReplyError(c,"value is not a valid float");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

// 从字符串对象中提取 long long 类型的数字
int getLongLongFromObject(robj *o, long long *target) {
    long long value;
    char *eptr;

    if (o == NULL) {
        value = 0;
    } else {
        redisAssertWithInfo(NULL, o, o->type == REDIS_STRING);
        if (sdsEncodedObject(o)) {
            errno = 0;
            value = strtoll(o->ptr, &eptr,10);
            if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' || 
                errno == ERANGE) {
                return REDIS_ERR;
            }

        } else if (o->encoding == REDIS_ENCODING_INT) {
            value = (long)o->ptr;
        } else {
            redisPanic("Unknown string encoding");
        }
    }

    if (target) *target = value;
    return REDIS_OK;
}

int getLongLongFromObjectOrReply(redisClient *c, robj *o, long long *target, const char *msg) {
    long double value;

    if (getLongLongFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c, (char*)msg);
        } else {
            addReplyError(c,"value is not an integer or out of range");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

int getLongFromObjectOrReply(redisClient *c, robj *o, long *target, const char *msg) {
    long value;
    if (getLongLongFromObjectOrReply(c, o, &value, msg) != REDIS_OK) return REDIS_ERR;

    if (value < LONG_MIN || value > LONG_MAX) {
        if (msg != NULL) {
            addReplyError(c, (char*)msg);
        } else {
            addReplyError(c, "value is out of range");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

/*--------------------------------------- Redis对象类型 API -----------------------------------------*/
int checkType(redisClient *c, robj *o, int type) {
    if (o->type != type) {
        addReply(c, shared.wrongtypeerr);
        return 1;
    }

    return 0;
}

char *strEncoding(int encoding) {
    switch(encoding) {
    case REDIS_ENCODING_RAW: return "raw";
    case REDIS_ENCODING_INT: return "int";
    case REDIS_ENCODING_HT: return "hashtable";
    case REDIS_ENCODING_LINKEDLIST: return "linkedlist";
    case REDIS_ENCODING_ZIPLIST: return "ziplist";
    case REDIS_ENCODING_INTSET: return "intset";
    case REDIS_ENCODING_SKIPLIST: return "skiplist";
    case REDIS_ENCODING_EMBSTR: return "embstr";
    default: return "unknown";
    }
}

/*--------------------------------------- OBJECT 命令函数 -----------------------------------------*/

// 计算键的剩余时间
unsigned long long estimateObjectIdleTime(robj *o) {
    unsigned long long lruclock = LRU_CLOCK();
    if (lruclock >= o->lru) {
        return (lruclock - o->lru) * REDIS_LRU_CLOCK_RESOLUTION;
    } else {
        return (lruclock + (REDIS_LRU_CLOCK_MAX - o->lru)) * REDIS_LRU_CLOCK_RESOLUTION;
    }
}

// 查找指定 key 的值
robj *objectCommandLookup(redisClient *c, robj *key) {

    dictEntry *de;
    if ((de = dictFind(c->db->dict, key->ptr)) == NULL) return NULL;
    return (robj*)dictGetVal(de);
}

// 查找指定 key 的值, 未找到回复客户端
robj *objectCommandLookupOrReply(redisClient *c, robj *key, robj *reply) {
    robj *o;

    o = objectCommandLookup(c, key);
    if (!o) addReply(c, reply);
    return o;
}

// OBJECT 命令
void objectCommand(redisClient *c) {
    robj *o;

    if (strcasecmp(c->argv[1]->ptr,"refcount") && c->argc == 3){
        if ((o = objectCommandLookupOrReply(c, c->argv[2],shared.nullbulk)) == NULL)
            return;
        addReplyLongLong(c, o->refcount);
        
    } else if (strcasecmp(c->argv[1]->ptr,"encoding") && c->argc == 3){
        if ((o = objectCommandLookupOrReply(c, c->argv[2],shared.nullbulk)) == NULL)
            return;
        addReplyBulkCString(c,strEncoding(o->encoding));

    } else if (strcasecmp(c->argv[1]->ptr,"idletime") && c->argc == 3){
        if ((o = objectCommandLookupOrReply(c, c->argv[2],shared.nullbulk)) == NULL)
            return;
        addReplyLongLong(c, estimateObjectIdleTime(o->lru)/1000);
    
    } else {
        addReplyError(c,"Syntax error. Try OBJECT (refcount|encoding|idletime)");
    }
}


//#ifdef OBJECT_TEST_MAIN

#include <assert.h>

// 字符串：gcc -g zmalloc.c sds.c redis.c object.c
// 字符串、列表：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c object.c
// 字符串、列表、集合、哈希：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c object.c
// 字符串、列表、集合、哈希、有序集合：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c t_zset.c object.c
// gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c t_zset.c redis.c networking.c object.c
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

    // 从 robj 中提取整数
    printf("get double from object: ");
    {
        o = createRawStringObject("10086", 5);
        assert(o->type == REDIS_STRING);
        assert(o->encoding == REDIS_ENCODING_RAW);
        assert(!sdscmp(o->ptr,sdsnew("10086")));
        double target;
        getDoubleFromObject(o, &target);
        assert(target==10086);
        printf("OK\n");
    }

    printf("get long double from object: ");
    {
        o = createRawStringObject("1008610010", 10);
        assert(o->type == REDIS_STRING);
        assert(o->encoding == REDIS_ENCODING_RAW);
        assert(!sdscmp(o->ptr,sdsnew("1008610010")));
        long double target;
        int a;
        a = getLongDoubleFromObject(o, &target);
        assert(target==1008610010);
        printf("OK\n");
    }

    printf("get long long from object: ");
    {
        o = createRawStringObject("10086100101234", 14);
        assert(o->type == REDIS_STRING);
        assert(o->encoding == REDIS_ENCODING_RAW);
        assert(!sdscmp(o->ptr,sdsnew("10086100101234")));
        long long target;
        getLongLongFromObject(o, &target);
        assert(target==10086100101234);
        printf("OK\n");
    }


    // 压缩 robj
    printf("try object encoding: \n");
    {   
        robj *intObj, *embObj, *rawObj;
        o = createRawStringObject("10086", 5);
        intObj = tryObjectEncoding(o);
        assert(intObj->encoding == REDIS_ENCODING_INT);
        printf("to int encoding, OK\n");

        o = createRawStringObject("test string", 11);
        embObj = tryObjectEncoding(o);
        assert(embObj->encoding == REDIS_ENCODING_EMBSTR);
        printf("to int embstr, OK\n");

        sds s = sdsempty();
        s = sdscpy(s,"this is a long long long long long string");
        o = createObject(REDIS_STRING, s);
        rawObj = tryObjectEncoding(o);
        assert(sdsavail(rawObj->ptr)==0);
        printf("remove free space OK\n");
    }

    // 将 robj 整数值转为字符串
    printf("get decode object: ");
    {
        robj *dec;
        o = createStringObjectFromLongLong(10086);
        assert(o->encoding == REDIS_ENCODING_INT);
        dec = getDecodedObject(o);
        assert(dec->encoding == REDIS_ENCODING_EMBSTR);
        printf("OK\n");
    }

    // 从 robj 中提取 long long 整数
    printf("representable long long: ");
    {   
        long long val;
        o = createStringObjectFromLongLong(10086);
        isObjectRepresentableAsLongLong(o, &val);
        assert(val == 10086);

        o = createStringObject("10010",5);
        isObjectRepresentableAsLongLong(o, &val);
        assert(val == 10010);
        printf("OK\n");
    }
}

//#endif