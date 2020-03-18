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