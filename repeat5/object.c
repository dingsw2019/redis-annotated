#include "redis.h"
#include <math.h>
#include <ctype.h>

/*--------------------------------------- Redis对象创建及释放 API -----------------------------------------*/
robj *createObject(int type, void *ptr) {

    robj *o = zmalloc(sizeof(robj));

    o->type = type;
    o->encoding = REDIS_ENCODING_RAW;
    o->lru = LRU_CLOCK();
    o->ptr = ptr;
    o->refcount = 1;

    return o;
}

robj *createRawStringObject(char *ptr, size_t len) {
    return createObject(REDIS_STRING, sdsnewlen(ptr, len));
}

robj *createEmbeddedStringObject(char *ptr, size_t len) {
    robj *o = zmalloc(sizeof(robj)+sizeof(struct sdshdr)+len+1);
    struct sdshdr *sh = (void*)(o+1);

    o->type = REDIS_STRING;
    o->encoding = REDIS_ENCODING_EMBSTR;
    o->ptr = sh+1;
    o->lru = LRU_CLOCK();
    o->refcount = 1;

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

robj *createStringObject(char *ptr, size_t len) {
    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) 
        return createEmbeddedStringObject(ptr, len);
    else 
        return createRawStringObject(ptr, len);
}

robj *createStringObjectFromLongLong(long long value) {
    robj *o;

    // 共享整数 10000
    if (value >= 0 && value < REDIS_SHARED_INTEGERS) {
        incrRefCount(shared.integers[value]);
        o = shared.integers[value];

    } else {
        // LONG 范围内, 以整数编码存储
        if (value >= LONG_MIN && value <= LONG_MAX) {
            o = createObject(REDIS_STRING, NULL);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*)((long)value);

        // 大于 LONG 范围, 以字符串形式存储
        } else {
            o = createObject(REDIS_STRING,sdsfromlonglong(value));
        }
    }
    
    return o;
}

robj *createStringObjectFromLongDouble(long double value) {
    char buf[256];
    size_t len;

    // 转成字符串
    len = snprintf(buf,sizeof(buf), "%.17Lf", value);

    // 去掉无效小数位
    if (strchr(buf, '.') != NULL) {
        char *p = buf+len-1;
        while(*p == '0') {
            p--;
            len--;
        }
        if (*p == '.') len--;
    }

    // 存储
    return createStringObject(buf,len);
}

robj *dupStringObject(robj *o) {

    robj *d;
    switch(o->encoding) {
    case REDIS_ENCODING_RAW: return createRawStringObject(o->ptr, sdslen(o->ptr));
    case REDIS_ENCODING_EMBSTR: return createEmbeddedStringObject(o->ptr, sdslen(o->ptr));
    case REDIS_ENCODING_INT:
        d = createObject(REDIS_STRING,NULL);
        d->encoding = REDIS_ENCODING_INT;
        d->ptr = o->ptr;
        return d;
    default:
        redisPanic("Wrong encoding");
        break;
    }
}

robj *createListObject(void) {

    list *l = listCreate();
    listSetFreeMethod(l,decrRefCountVoid);
    robj *o = createObject(REDIS_LIST,l);
    o->encoding = REDIS_ENCODING_LINKEDLIST;
    return o;
}

robj *createZiplistObject(void) {
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_LIST, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

robj *createSetObject(void) {
    dict *d = dictCreate(&setDictType,NULL);
    robj *o = createObject(REDIS_SET, d);
    o->encoding = REDIS_ENCODING_HT;
    return o;
}

robj *createIntsetObject(void) {
    intset *is = intsetNew();
    robj *o = createObject(REDIS_SET, is);
    o->encoding = REDIS_ENCODING_INTSET;
    return o;
}

robj *createHashObject(void) {
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_HASH, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

robj *createZsetObject(void) {
    zset *zs;
    zs->dict = dictCreate(&zsetDictType,NULL);
    zs->zsl = zslCreate();
    robj *o = createObject(REDIS_ZSET, zs);
    o->encoding = REDIS_ENCODING_SKIPLIST;
    return o;
}

robj *createZsetZiplistObject(void) {
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_ZSET, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

void freeStringObject(robj *o) {

    if (o->encoding == REDIS_ENCODING_RAW) {
        sdsfree(o->ptr);
    }
}

void freeListObject(robj *o) {

    switch(o->encoding) {
    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    case REDIS_ENCODING_LINKEDLIST:
        dictRelease((list*)o->ptr);
        break;
    default:
        redisPanic("Unknown list encoding type");
        break;
    }
}

void freeSetObject(robj *o) {
    
    switch(o->encoding) {
    case REDIS_ENCODING_HT:
        dictRelease((dict*)o->ptr);
        break;
    case REDIS_ENCODING_INTSET:
        zfree(o->ptr);
        break;
    default:
        redisPanic("Unknown set encoding type");
        break;
    }
    
}

void freeZsetObject(robj *o) {
    zset *zs;

    switch(o->encoding) {
    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    case REDIS_ENCODING_SKIPLIST:
        zs = o->ptr;
        dictRelease(zs->dict);
        zslFree(zs->zsl);
        zfree(zs);
        break;
    default:
        redisPanic("Unknown zset encoding type");
        break;
    }
    
}

void freeHashObject(robj *o) {
    
    switch(o->encoding) {
    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    case REDIS_ENCODING_HT:
        dictRelease((dict*)o->ptr);
        break;
    default:
        redisPanic("Unknown hash encoding type");
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
        switch(o->type){
        case REDIS_STRING: freeStringObject(o);break;
        case REDIS_LIST: freeListObject(o);break;
        case REDIS_SET: freeSetObject(o);break;
        case REDIS_ZSET: freeZsetObject(o);break;
        case REDIS_HASH: freeHashObject(o);break;
        default:redisPanic("Unknown object type");break;
        }

        zfree(o);
    } else {
        o->refcount--;
    }
}

void decrRefCountVoid(void *o) {
    decrRefCount(o);
}

robj *resetRefCount(robj *o) {
    o->refcount = 0;
    return o;
}

/*--------------------------------------- Redis字符串对象值的相关函数 -----------------------------------------*/

// 从 o 中读取整数存入 llval
int isObjectRepresentableAsLongLong(robj *o, long long *llval) {

    if (o->encoding == REDIS_ENCODING_INT) {
        if (llval) *llval = (long)o->ptr;
        return REDIS_OK;
    } else {
        return string2ll(o->ptr, sdslen(o->ptr), llval) ? REDIS_OK : REDIS_ERR;
    }
}

robj *tryObjectEncoding(robj *o) {
    long value;
    sds s = o->ptr;
    size_t len;

    if (!sdsEncodedObject(o)) return o;

    len = sdslen(s);

    if (len <= 21 && string2l(s, len, &value)) {
        if (value >= 0 && value < REDIS_SHARED_INTEGERS) {
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

    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) {
        robj *emb;
        if(o->encoding == REDIS_ENCODING_EMBSTR) return o;
        emb = createEmbeddedStringObject(s, sdslen(s));
        decrRefCount(o);
        return emb;
    }

    if (o->encoding == REDIS_ENCODING_RAW && sdsavail(s)>(len/10)) {
        o->ptr = sdsRemoveFreeSpace(o->ptr);
    }

    return o;
}

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

int compareStringObjectWithFlags(robj *a, robj *b, int flags) {
    char bufa[128], bufb[128], *astr, *bstr;
    size_t alen, blen, minlen;

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
        blen = ll2string(bufb,sizeof(bufb),(long) b->ptr);
        bstr = bufb;
    }

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

int compareStringObjects(robj *a, robj *b) {
    return compareStringObjectWithFlags(a,b,REDIS_COMPARE_BINARY);
}

int equalStringObjects(robj *a, robj *b) {

    if (a->encoding == REDIS_ENCODING_INT && b->encoding == REDIS_ENCODING_INT) {
        return a->ptr == b->ptr;
    } else {
        return compareStringObjects(a,b) == 0;
    }
}

size_t stringObjectLen(robj *o) {

    if (sdsEncodedObject(o)) {
        return sdslen(o->ptr);
    } else {
        char buf[32];
        return ll2string(buf,32,(long)o->ptr);
    }
}

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
                (errno == ERANGE && (value = HUGE_VAL || value == -HUGE_VAL || value==0)) || 
                errno == EINVAL || isnan(value)) {
                
                return REDIS_ERR;
            }
        } else if (o->encoding == REDIS_ENCODING_INT) {
            value = (long)o->ptr;
        
        } else {
            redisPanic("Unknown string encoding");
        }
    }

    if (target) 
        *target = value;
    
    return REDIS_OK;
}

int getDoubleFromObjectOrReply(redisClient *c, robj *o, double *target, const char *msg) {
    double value;

    if (getDoubleFromObject(o, &value) != REDIS_OK) {
        if (msg != NULL) {
            addReplyError(c, msg);
        } else {
            addReplyError(c, "value is not a valid float");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}

/*--------------------------------------- Redis对象类型 API -----------------------------------------*/

int checkType(redisClient *c, robj *o, int type) {
    if (o->encoding != type) {
        addReply(c,shared.wrongtypeerr);
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

unsigned long long estimateObjectIdleTime(robj *o) {
    unsigned long long lruclock = LRU_CLOCK();
    if (lruclock >= o->lru) {
        return (lruclock - o->lru) * REDIS_LRU_CLOCK_RESOLUTION;
    } else {
        return (lruclock + (REDIS_LRU_CLOCK_MAX - o->lru)) * REDIS_LRU_CLOCK_RESOLUTION;
    }
}

robj *objectCommandLookup(redisClient *c, robj *key) {

    // robj *value;

    // if ((value = lookupKeyRead(c->db, key)) == NULL) {
    //     return NULL;
    // }

    // return value;

    dictEntry *de;
    if ((de = dictFind(c->db->dict, key->ptr)) == NULL) return NULL;
    return (robj*)dictGetVal(de);
}

robj *objectCommandLookupOrReply(redisClient *c, robj *key, robj *reply) {
    robj *o;
    o = objectCommandLookup(c, key);
    if (!o) addReply(c,reply);
    return o;
    
}

void objectCommand(redisClient *c) {
    robj *o;
    if (strcasecmp(c->argv[1]->ptr,"refcount") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c, c->argv[2],shared.nullbulk)) == NULL)
            return;
        addReplyLongLong(c,o->refcount);

    } else if (strcasecmp(c->argv[1]->ptr,"encoding") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c, c->argv[2],shared.nullbulk)) == NULL)
            return;
        addReplyBulkCString(c,strEncoding(o->encoding));

    } else if (strcasecmp(c->argv[1]->ptr,"idletime") && c->argc == 3) {
        if ((o = objectCommandLookupOrReply(c, c->argv[2],shared.nullbulk)) == NULL)
            return;
        addReplyLongLong(c,estimateObjectIdleTime(o->lru)/1000);
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

    // // 创建 raw 编码的字符串对象
    // printf("create raw string object: ");
    // {
    //     o = createRawStringObject("raw string", 10);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_RAW);
    //     assert(!sdscmp(o->ptr,sdsnew("raw string")));
    //     printf("OK\n");
    // }

    // // 创建 embstr 编码的字符串对象
    // printf("create embstr string object: ");
    // {
    //     freeStringObject(o);
    //     o = createEmbeddedStringObject("embstr string", 13);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_EMBSTR);
    //     assert(!sdscmp(o->ptr,sdsnew("embstr string")));
    //     printf("OK\n");
    // }

    // // 根据字符串长度, 选择 embstr 或 raw 编码的字符串对象
    // printf("create 41 bytes raw string object: ");
    // {
    //     o = createStringObject("long long long long long long long string",41);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_RAW);
    //     assert(!sdscmp(o->ptr,sdsnew("long long long long long long long string")));
    //     printf("OK\n");
    // }

    // // 创建一个 int 编码的字符串对象
    // printf("create int string object: ");
    // {
    //     freeStringObject(o);
    //     o = createStringObjectFromLongLong(123456789);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_INT);
    //     assert((long long)o->ptr == 123456789);
    //     printf("OK\n");
    // }

    // // 创建一个浮点型的字符串对象(warn 有精度问题)
    // // printf("create double string object:");
    // // {
    // //     o = createStringObjectFromLongDouble(3.14000000);
    // //     assert(o->type == REDIS_STRING);
    // //     assert(o->encoding == REDIS_ENCODING_EMBSTR);
    // //     assert(!sdscmp(o->ptr,sdsnew("3.14")));
    // //     printf("OK\n");
    // // }

    // // 复制字符串对象
    // printf("duplicate int string object: ");
    // {
    //     dup = dupStringObject(o);
    //     assert(dup->type == REDIS_STRING);
    //     assert(dup->encoding == REDIS_ENCODING_INT);
    //     assert((long long)dup->ptr == 123456789);
    //     printf("OK\n");
    // }

    // // 创建一个 list 编码的空列表对象
    // printf("create and free list list object: ");
    // {
    //     o = createListObject();
    //     assert(o->type == REDIS_LIST);
    //     assert(o->encoding == REDIS_ENCODING_LINKEDLIST);
    //     freeListObject(o);
    //     printf("OK\n");
    // }
    

    // // 创建一个 ziplist 编码的空列表对象
    // printf("create and free ziplist list object: ");
    // {
    //     o = createZiplistObject();
    //     assert(o->type == REDIS_LIST);
    //     assert(o->encoding == REDIS_ENCODING_ZIPLIST);
    //     freeListObject(o);
    //     printf("OK\n");
    // }

    // // 创建并释放 intset 的空集合对象
    // printf("create and free intset set object: ");
    // {
    //     o = createIntsetObject();
    //     assert(o->type == REDIS_SET);
    //     assert(o->encoding == REDIS_ENCODING_INTSET);
    //     freeSetObject(o);
    //     printf("OK\n");
    // }

    // // 创建并释放一个 哈希对象
    // printf("create and free hash object: ");
    // {
    //     o = createHashObject();
    //     assert(o->type == REDIS_HASH);
    //     assert(o->encoding == REDIS_ENCODING_ZIPLIST);
    //     freeHashObject(o);
    //     printf("OK\n");
    // }

    // // 创建并释放 SKIPLIST 编码的有序集合对象
    // printf("create and free skiplist zset object: ");
    // {
    //     o = createZsetObject();
    //     assert(o->type == REDIS_ZSET);
    //     assert(o->encoding == REDIS_ENCODING_SKIPLIST);
    //     freeZsetObject(o);
    //     printf("OK\n");
    // }
    
    // // 创建并释放 ZIPLIST 编码的有序集合对象
    // printf("create and free ziplist zset object: ");
    // {
    //     o = createZsetZiplistObject();
    //     assert(o->type == REDIS_ZSET);
    //     assert(o->encoding == REDIS_ENCODING_ZIPLIST);
    //     freeZsetObject(o);
    //     printf("OK\n");
    // }

    // // 从 robj 中提取整数
    // printf("get double from object: ");
    // {
    //     o = createRawStringObject("10086", 5);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_RAW);
    //     assert(!sdscmp(o->ptr,sdsnew("10086")));
    //     double target;
    //     getDoubleFromObject(o, &target);
    //     assert(target==10086);
    //     printf("OK\n");
    // }

    // printf("get long double from object: ");
    // {
    //     o = createRawStringObject("1008610010", 10);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_RAW);
    //     assert(!sdscmp(o->ptr,sdsnew("1008610010")));
    //     long double target;
    //     int a;
    //     a = getLongDoubleFromObject(o, &target);
    //     assert(target==1008610010);
    //     printf("OK\n");
    // }

    // printf("get long long from object: ");
    // {
    //     o = createRawStringObject("10086100101234", 14);
    //     assert(o->type == REDIS_STRING);
    //     assert(o->encoding == REDIS_ENCODING_RAW);
    //     assert(!sdscmp(o->ptr,sdsnew("10086100101234")));
    //     long long target;
    //     getLongLongFromObject(o, &target);
    //     assert(target==10086100101234);
    //     printf("OK\n");
    // }


    // // 压缩 robj
    // printf("try object encoding: \n");
    // {   
    //     robj *intObj, *embObj, *rawObj;
    //     o = createRawStringObject("10086", 5);
    //     intObj = tryObjectEncoding(o);
    //     assert(intObj->encoding == REDIS_ENCODING_INT);
    //     printf("to int encoding, OK\n");

    //     o = createRawStringObject("test string", 11);
    //     embObj = tryObjectEncoding(o);
    //     assert(embObj->encoding == REDIS_ENCODING_EMBSTR);
    //     printf("to int embstr, OK\n");

    //     sds s = sdsempty();
    //     s = sdscpy(s,"this is a long long long long long string");
    //     o = createObject(REDIS_STRING, s);
    //     rawObj = tryObjectEncoding(o);
    //     assert(sdsavail(rawObj->ptr)==0);
    //     printf("remove free space OK\n");
    // }

    // // 将 robj 整数值转为字符串
    // printf("get decode object: ");
    // {
    //     robj *dec;
    //     o = createStringObjectFromLongLong(10086);
    //     assert(o->encoding == REDIS_ENCODING_INT);
    //     dec = getDecodedObject(o);
    //     assert(dec->encoding == REDIS_ENCODING_EMBSTR);
    //     printf("OK\n");
    // }

    // // 从 robj 中提取 long long 整数
    // printf("representable long long: ");
    // {   
    //     long long val;
    //     o = createStringObjectFromLongLong(10086);
    //     isObjectRepresentableAsLongLong(o, &val);
    //     assert(val == 10086);

    //     o = createStringObject("10010",5);
    //     isObjectRepresentableAsLongLong(o, &val);
    //     assert(val == 10010);
    //     printf("OK\n");
    // }
}

//#endif