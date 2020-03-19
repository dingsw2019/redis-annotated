#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"
#include <sys/time.h>


/*--------------------- private --------------------*/
#define ZIP_BIGLEN 254

// 字符编码和整数编码的掩码
#define ZIP_STR_MASK 0xc0
#define ZIP_INT_MASK 0x30

// 字符编码类型
#define ZIP_STR06B (0 << 6)
#define ZIP_STR14B (1 << 6)
#define ZIP_STR32B (2 << 6)

// 整数编码类型
#define ZIP_INT16B (0xc0 | 0 << 4)
#define ZIP_INT32B (0xc0 | 1 << 4)
#define ZIP_INT64B (0xc0 | 2 << 4)
#define ZIP_INT24B (0xc0 | 3 << 4)
// #define ZIP_INT8B 0xfc myerr
#define ZIP_INT8B 0xfe

// 从 p 中获取存储"前置节点长度"所需字节大小
#define ZIP_DECODE_PREVLENSIZE(p, prevlensize) do{      \
    if ((p)[0] < ZIP_BIGLEN) {                          \
        (prevlensize) = 1;                              \
    } else {                                            \
        (prevlensize) = 5;                              \
    }                                                   \
}while(0);

// 从 p 中获取前置节点信息
#define ZIP_DECODE_PREVLEN(p,prevlensize, prevlen) do {         \
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);                     \
                                                                \
    /* 按字节获取前置节点长度 */                                  \
    if ((prevlensize) == 1) {                                   \
        (prevlen) = p[0];                                       \
    } else if ((prevlensize) == 5){                             \
        /*assert(sizeof(prevlensize) == 4);*/                   \
        memcpy(&(prevlen), ((char*)(p))+1, 4);                  \
        memrev32ifbe(&(prevlen));                               \
    }                                                           \
} while(0);

// 获取编码方式
#define ZIP_ENTRY_ENCODING(p, encoding) do{                    \
    (encoding) = (p)[0];                                        \
    /* if (encoding < ZIP_STR_MASK) encoding &= ZIP_STR_MASK myerr*/      \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK;       \
}while(0)

// static unsigned int zipIntSize(unsigned int encoding) { myerr
static unsigned int zipIntSize(unsigned char encoding) {
    switch (encoding)
    {
    case ZIP_INT8B: return 1;
    case ZIP_INT16B: return 2;
    case ZIP_INT24B: return 3;
    case ZIP_INT32B: return 4;
    case ZIP_INT64B: return 8;
    default:return 0;
    }

    // assert(NULL);
    return 0;
}

#define ZIP_DECODE_LENGTH(p, encoding, lensize, len) do{        \
                                                                \
    /*ZIP_DECODE_ENCODING(p, encoding); myerr */                          \
    ZIP_ENTRY_ENCODING((p), (encoding));                           \
                                                                \
    /* 字符数组 */                                               \
    if ((encoding) < ZIP_STR_MASK) {                            \
                                                                \
        if ((encoding) == ZIP_STR06B) {                         \
            (lensize) = 1;                                      \
            (len) = (p)[0] & 0x3f;                              \
        } else if ((encoding) == ZIP_STR14B) {                  \
            (lensize) = 2;                                      \
            (len) = (((p)[0] & 0x3f) << 8) | (p)[1];            \
        } else if ((encoding) == ZIP_STR32B) {                  \
            (lensize) = 5;                                      \
            (len) = ((p)[1] << 24) |                            \
                    ((p)[2] << 16) |                            \
                    ((p)[3] <<  8) |                            \
                    ((p)[4]);                                   \
        } else {                                                \
            /*assert(NULL);*/                                   \
        }                                                       \
                                                                \
    /* 整数 */                                                  \
    } else {                                                    \
        (lensize) = 1;                                          \
        (len) = zipIntSize(encoding);                           \
    }                                                           \
}while(0);


// 从 p 指针中按读取 zlentry 结构的信息, 并返回 zlentry
// static zlentry zipEntry(unsigned char p) { myerr
static zlentry zipEntry(unsigned char *p) {

    zlentry e;

    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    e.headersize = e.prevrawlensize + e.lensize;

    e.p = p;

    return e;
}

// 获取节点长度(字节)
static unsigned int zipRawEntryLength(unsigned char *p) {

    unsigned int prevlensize,encoding,lensize,len;
    // 获取存储前置节点的长度
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    // 获取存储当前节点的长度
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
    // 返回
    return prevlensize+lensize+len;
}

// 将字符串型数值转换成整数
// 转换成功返回 1, 否则返回 0
static int zipTryEncoding(unsigned char *s, unsigned int slen, long long *v, unsigned char *encoding) {

    // myerr 缺少
    long long value;
    // 值太大或太小, 不处理
    if (slen >= 32 || slen == 0)
        return 0;

    // 转换
    // if (string2ll(s,slen,value)) { myerr
    if (string2ll((char*)s,slen,&value)) {

        // 根据值的大小判断 编码类型
        if (value >= 0 && value <= 12) {
            *encoding = ZIP_INT_IMM_MIN+value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT32B;
        } else {
            *encoding = ZIP_INT64B;
        }

        *v = value;

        return 1;
    }

    // 转换失败
    return 0;
}

// 返回前置节点长度编码
// 如果 p 存在, 写入长度编码
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {

    // 仅返回编码长度,不写入
    if (p == NULL) {
        return len < ZIP_BIGLEN ? 1 : sizeof(len)+1;
    } else {

        // 1 字节
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;
        // 5 字节
        } else {
            // 1 字节标识符
            p[0] = ZIP_BIGLEN;
            // 存长度
            memcpy(p+1,&len,sizeof(len));
            memrev32ifbe(p+1);
            return sizeof(len)+1;
        }
    }
}

// 返回当前节点长度编码
// 如果 p 存在, 写入长度编码
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {

    unsigned char len = 1, buf[5];

    // 字符串
    if (ZIP_IS_STR(encoding)) {

        // 1 字节
        if (rawlen <= 0x3f) {
            if (!p) return len;
            buf[0] = ZIP_STR06B | rawlen;
        // 2 字节
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        // 5 字节
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >>  8) & 0xff;
            buf[4] = rawlen & 0xff;
        }
    // 整数
    } else {
        // 保存节点值长度
        if (!p) return len;
        buf[0] = encoding;
    }

    memcpy(p,buf,len);

    return len;
}

// 新节点长度 len 与其后置节点 p 的前置节点长度的差值
static unsigned int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {

    unsigned prevlensize;

    ZIP_DECODE_PREVLENSIZE(p,prevlensize);

    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

// 扩容或缩容, 重分配列表内容空间
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    zl = zrealloc(zl,len);

    // 更新总长度
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);
    // 更新末端标识
    zl[len-1] = ZIP_END;

    return zl;
}

// 整数型节点值写入节点
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {

    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT24B) {
        i32 = value << 8;
        memrev32ifbe(&i32);
        memcpy(p,((uint8_t)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        
    } else {
        // assert(NULL);
    }
}

// 在 p 指向的位置上添加节点
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), offset, reqlen, prevlen = 0;
    // int nextdiff,encoding = 0, value; myerr
    int nextdiff = 0;
    unsigned char encoding = 0;
    long long value = 123456789;

    zlentry entry, tail;

    // 计算前置节点的长度
    if (p[0] != ZIP_END) {
        // 走到这里说明 p 之后有节点
        // 这个节点就是新节点的前置节点,获取它的长度
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        // 判断列表是否无节点
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {

            // 新节点的前置节点是尾节点
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 尝试将 s 转换成整数,如果它是字符串数字的话
    // 获取存储节点值的内存大小
    if (zipTryEncoding(s,slen,&encoding,&value)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算编码前置节点长度的大小
    reqlen += zipPrevEncodeLength(NULL, prevlen);
    // 计算编码当前节点长度的大小
    reqlen += zipEncodeLength(NULL, encoding, value);

    // 新节点的后置节点的prev 是否足够
    // nextdiff = reqlen < ZIP_BIGLEN ? 0 : zipPrevLenByteDiff(p, reqlen); myerr
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

    // 扩容
    offset = p - zl;
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl + offset;

    // 如果新节点不是添加到尾节点
    if (p[0] != ZIP_END) {

        // 移动原有节点, 给新节点腾出空间
        // memmove(p+reqlen,p-nextdiff,offset-1+nextdiff); myerr
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        // 更新后置节点的前置长度编码 myerr:缺少
        zipPrevEncodeLength(p+reqlen,reqlen);

        // 更新到达尾节点的偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);
        // 如果新节点之后有多个节点
        // 尾节点偏移量还要加上 nextdiff
        // tail = p+reqlen; myerr
        tail = zipEntry(p+reqlen);
        // if (p[reqlen+tail.prevrawlensize+tail.lensize] != ZIP_END) { myerr
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }

    // 新节点是尾节点
    } else {   
        // 更新总长度
        // ZIPLIST_BYTES(zl) = 
            // intrev32ifbe(intrev32ifbe(ZIPLIST_BYTES(zl))+reqlen);

        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    // 前置节点长度写入
    zipPrevEncodingLength(p, prevlen);

    // 当前节点长度写入
    zipEncodingLength(p, encoding, value);

    // 节点值写入
    if (ZIP_IS_STR(encoding)) {
        memcopy(p,s,reqlen);
    } else {
        // zipSaveInteger(p,value); myerr
        zipSaveInteger(p,value,encoding);
    }

    // 更新节点计数器
    ZIPLIST_LENGTH(zl) = 
        intrev32ifbe(intrev32ifbe(ZIPLIST_LENGTH(zl))+1);

    return zl;
}