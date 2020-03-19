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

// 在 p 指向的位置上添加节点
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), offset, reqlen, prevlen;
    int nextdiff,encoding = 0, value;
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
            // prevlen = 
        }
    }

    // 尝试将 s 转换成整数,如果它是字符串数字的话
    // 获取节点值的长度
    if (zipTry(s,slen,&encoding,&value)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算编码前置节点长度的大小
    reqlen += zipPrevEncodingLength(NULL, prevlen);
    // 计算编码当前节点长度的大小
    reqlen += zipEncodingLength(NULL, encoding, value);

    // 新节点的后置节点的prev 是否足够
    nextdiff = reqlen < ZIP_BIGLEN ? 0 : zipPrevLenByteDiff(p, reqlen);

    // 扩容
    offset = p - zl;
    zl = zipResize(zl, curlen+reqlen+nextdiff);
    p = zl + offset;

    // 如果新节点不是添加到尾节点
    if (p[0] != ZIP_END) {

        // 移动原有节点, 给新节点腾出空间
        memmove(p+reqlen,p-nextdiff,offset-1+nextdiff);
        // 更新到达尾节点的偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);
        // 如果新节点之后有多个节点
        // 尾节点偏移量还要加上 nextdiff
        tail = p+reqlen;
        if (p[reqlen+tail.prevrawlensize+tail.lensize] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }

    // 新节点是尾节点
    } else {   
        // 更新总长度
        ZIPLIST_BYTES(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_BYTES(zl))+reqlen);
    }

    // 前置节点长度写入
    zipPrevEncodingLength(p, prevlen);

    // 当前节点长度写入
    zipEncodingLength(p, encoding, value);

    // 节点值写入
    if (ZIP_IS_STR(encoding)) {
        memcopy(p,s,reqlen);
    } else {
        zipSaveInterge(p,value);
    }

    // 更新节点计数器
    ZIPLIST_LENGTH(zl) = 
        intrev32ifbe(intrev32ifbe(ZIPLIST_LENGTH(zl))+1);

    return zl;
}