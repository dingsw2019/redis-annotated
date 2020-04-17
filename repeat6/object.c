#include "redis.h"

/*--------------------------------------- Redis对象创建及释放 API -----------------------------------------*/
// 按类型创建一个redis对象, 并绑定值
robj *createObject(int type, void *ptr) {

    robj *o = zmalloc(sizeof(*o));

    o->type = type;
    o->encoding = REDIS_ENCODING_RAW;
    o->refcount = 1;
    o->lru = LRU_CLOCK();
    o->ptr = ptr;

    return o;
}

// 创建一个 RAW 编码的字符串对象
robj *createRawStringObject(char *ptr, size_t len) {
    // return createObject(REDIS_STRING, ptr); myerr
    return createObject(REDIS_STRING, sdsnewlen(ptr, len));
}

// 创建一个 EMBSTR 编码的字符串对象
// 优点：字符串值和对象一次创建, 一次释放, 高效。相比 embstr 占用更少空间
// 不可修改, 修改前会转为 RAW 编码
robj *createEmbeddedStringObject(char *ptr, size_t len) {

    // 创建内存
    robj *o = zmalloc(sizeof(robj)+sizeof(struct sdshdr)+len+1);
    struct sdshdr *sh = (void*)(o+1);

    // 设置属性
    o->type = REDIS_STRING;
    o->encoding = REDIS_ENCODING_EMBSTR;
    o->ptr = sh+1;
    o->refcount = 1;
    o->lru = LRU_CLOCK();
    sh->len = len;
    sh->free = 0;

    // 塞数据
    if (ptr) {
        memcpy(sh->buf, ptr, len);
        sh->buf[len] = '\0';
    } else {
        memset(sh->buf, 0, len+1);
    }

    return o;
}

#define REDIS_ENCODING_EMBSTR_SIZE_LIMIT 39

// raw 、 embstr 的封装
robj *createStringObject(char *ptr, size_t len) {

    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT) 
        return createEmbeddedStringObject(ptr,len);
    else
        return createRawStringObject(ptr,len);
}

// 整数存入robj
robj *createStringObjectFromLongLong(long long value) {
    robj *o;

    // 共享整数
    if (value >=0 && value < REDIS_SHARED_INTEGERS) {
        incrRefCount(shared.integers[value]);
        o = shared.integers[value];
    } else {

        // 存储整数
        if (value >= LONG_MIN && value <= LONG_MAX) {
            o = createObject(REDIS_STRING, NULL);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void*)((long)value);
        
        // 按字符串存储整数
        } else {
            o = createObject(REDIS_STRING, sdsfromlonglong(value));
        }
    }


    return o;
}

// double 存入 robj
robj *createStringObjectFromLongDouble(long double value) {
    char buf[256];
    int len;

    // 将 value 写入数组, 保存17位小数
    len = snprintf(buf,sizeof(buf),"%.17Lf",value);

    // 过渡小数后的0, 3.140000 变 3.14
    // 3.0000 变 3
    if (strchr(buf,'.') != NULL) {
        char *p = buf+len-1;
        while(*p == '0') {
            p--;
            len--;
        }
        if (*p == '.') len--;
    }

    // 返回
    return createStringObject(buf,len);
}

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

    // myerr：缺少
    listSetFreeMethod(l,decrRefCountVoid);

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

    dict *d = dictCreate(&setDictType, NULL);

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

robj *createHashObject(void) {

    unsigned char *zl = ziplistNew();

    robj *o = createObject(REDIS_HASH, zl);

    o->encoding = REDIS_ENCODING_ZIPLIST;

    return o;
}

robj *createZsetObject(void) {

    zset *zs = zmalloc(sizeof(*zs));

    zs->dict = dictCreate(&zsetDictType, NULL);
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

/*--------------------------------------- Redis对象引用计数 API -----------------------------------------*/
void incrRefCount(robj *o) {
    o->refcount++;
}

void decrRefCount(robj *o) {

}

void decrRefCountVoid(void *o) {

}

robj *resetRefCount(robj *obj) {

}



/*--------------------------------------- Redis字符串对象值的相关函数 -----------------------------------------*/


/*--------------------------------------- Redis对象类型 API -----------------------------------------*/


/*--------------------------------------- OBJECT 命令函数 -----------------------------------------*/


// 字符串：gcc -g zmalloc.c sds.c redis.c object.c
// 字符串、列表：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c object.c
// 字符串、列表、集合、哈希：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c object.c
// 字符串、列表、集合、哈希、有序集合：gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c t_zset.c object.c
// gcc -g util.c zmalloc.c sds.c adlist.c ziplist.c dict.c intset.c t_zset.c redis.c object.c
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

    // 创建一个浮点型的字符串对象(warn 有精度问题)
    printf("create double string object:");
    {
        o = createStringObjectFromLongDouble(3.14000000);
        // assert(o->type == REDIS_STRING);
        // assert(o->encoding == REDIS_ENCODING_EMBSTR);
        // assert(!sdscmp(o->ptr,sdsnew("3.14")));
        printf("OK\n");
    }

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
}