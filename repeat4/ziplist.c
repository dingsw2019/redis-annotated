#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"

// #define ZIP_DECODE_PREVLENSIZE(p, len) do{ myerr
//     if (len < ZIP_BIGLEN) {
//         p[0] = len;
//     } else {
//         p[0] = ZIP_BIGLEN;
//     }
// }while(0)

#define ZIP_DECODE_PREVLENSIZE(p, prevlensize) do{ \
    if (p[0] < ZIP_BIGLEN) {                       \
        (prevlensize) = 1;                         \
    } else {                                       \
        (prevlensize) = 5;                         \
    }                                              \
}while(0)

// 解码获取前置节点长度并保存
// #define ZIP_DECODE_PREVLEN(p, prevlensize, prevlen) do{ myerr

//     /* 获取编码长度所需节点 */
//     ZIP_DECODE_PREVLENSIZE(p, prevlensize);

//     if ((prevlensize) == 1) {
//         prevlen = p[1];
//     } else if ((prevlensize) == 5) {
//         memcpy(&(prevlen),p+1,4);
//     }
// }while(0);
#define ZIP_DECODE_PREVLEN(p, prevlensize, prevlen) do{ \
    /* 获取编码长度所需节点 */                            \
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);             \
                                                        \
    if ((prevlensize) == 1) {                           \
        (prevlen) = (p)[1];                             \
    } else if ((prevlensize) == 5) {                    \
        memcpy(&(prevlen),((char*)(p))+1,4);            \
        memrev32ifbe(&prevlen);                         \
    }                                                   \
}while(0);

// 整数编码所需长度
static unsigned int zipIntSize(unsigned char encoding) {

    switch (encoding)
    {
    case ZIP_INT_8B: return 1;
    case ZIP_INT_16B: return 2;
    case ZIP_INT_24B: return 3;
    case ZIP_INT_32B: return 4;
    case ZIP_INT_64B: return 8;
    default: return 0;
    }

    // assert(NULL);
    return 0;
}

// 解码当前节点的编码方式,占字节数,节点值长度
// #define ZIP_DECODE_LENGTH(p, encoding, lensize, len) do{
    
//     (encoding) = (p[0] & ZIP_STR_MASK);
//     // 字符串
//     if (encoding < ZIP_STR_MASK) {
//         if (encoding == ZIP_STR_06B) {
//             (lensize) = 1;
//             (len) = p[0] & 0x3f;
//         } else if (encoding == ZIP_STR_14B) {
//             (lensize) = 2;
//             (len) = (p[0] & 0x3f) | p[1];
//         } else if (encoding == ZIP_STR_32B) {
//             (lensize) = 5;
//             memcpy((len),p+1,4);
//         }
//     // 整数
//     } else {
//         (lensize) = 1;
//         (len) = zipIntSize(encoding);
//     }
// }while(0)
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do{  \
                                                            \
    ZIP_ENTRY_ENCODING((ptr), (encoding));                  \
                                                            \
    /* 字符串 */                                             \
    if ((encoding) < ZIP_STR_MASK) {                        \
        if ((encoding) == ZIP_STR_06B) {                    \
            (lensize) = 1;                                  \
            (len) = (ptr)[0] & 0x3f;                        \
        } else if ((encoding) == ZIP_STR_14B) {             \
            (lensize) = 2;                                  \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];    \
        } else if ((encoding) == ZIP_STR_32B) {             \
            (lensize) = 5;                                  \
            (len) = ((ptr)[1] << 24) |                      \
                    ((ptr)[2] << 16) |                      \
                    ((ptr)[3] <<  8) |                      \
                    ((ptr)[4]);                             \
        } else {                                            \
            /*assert(NULL);*/                               \
        }                                                   \
    /* 整数 */                                              \
    } else {                                                \
        (lensize) = 1;                                      \
        (len) = zipIntSize(encoding);                       \
    }                                                       \
}while(0)

// 从 ptr 中获取节点信息
static zlentry zipEntry(unsigned char *ptr) {

    zlentry e;

    // 获取前置节点相关属性
    ZIP_DECODE_PREVLEN(ptr,e.prevlensize, e.prevlen);

    // 获取当前节点相关属性,编码类型
    ZIP_DECODE_LENGTH(ptr + e.prevlensize, e.encoding, e.lensize, e.len);

    // 设置头部字节数
    e.headersize = e.prevlensize + e.lensize;

    e.p = ptr;

    // 返回
    return e;
}

// 获取节点长度
static unsigned int zipRawEncodeLength(unsigned char *ptr) {

    unsigned int prevlensize, encoding, lensize, len;

    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);

    ZIP_DECODE_LENGTH(ptr, encoding, lensize, len);

    return prevlensize + lensize + len;
}

// 数值型字符串转换成整型
// 成功返回 1, 否则返回 0
// 并将编码,转换的整型值写入指针
// static int zipTryEncoding(unsigned char *s, unsigned int slen, int64_t *v, unsigned char *encoding) { myerr
static int zipTryEncoding(unsigned char *s, unsigned int slen, long long *v, unsigned char *encoding) {

    long long value;
    // 太长或太短,不处理
    if (slen >= 32 || slen == 0) return 0;
    // 转换成功
    // if (string2ll(s,slen,&value)) { myerr
    if (string2ll((char*)s,slen,&value)) {

        // 获取编码类型
        if (value >= 0 && value <= 12) {
            // encoding = 0; myerr
            encoding = ZIP_INT_IMM_MIN + value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            encoding = ZIP_INT_8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            encoding = ZIP_INT_16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            encoding = ZIP_INT_24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            encoding = ZIP_INT_32B;
        } else {
            encoding = ZIP_INT_64B;
        }

        *v = value;

        return 1;
    }

    // 转换失败
    return 0;
}

// 计算前置节点的长度所需字节数
static unsigned int zipPrevEncodeLength(unsigned char *ptr, unsigned int len) {

    // 只返回字节数
    if (ptr == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len)+1;
    // 设置字节数
    } else {

        if (len < ZIP_BIGLEN) {
            ptr[0] = len;
            return 1;
        } else {
            ptr[0] = ZIP_BIGLEN;
            // memcpy(((uint8_t*)ptr)+1,&len,4); myerr
            memcpy(ptr+1,&len,sizeof(len));
            memrev32ifbe(ptr+1);
            return sizeof(len)+1;
        }
    }
}

// 计算并设置当前节点的编码长度
static unsigned int zipEncodeLength(unsigned char *ptr, unsigned char encoding, unsigned int rawlen) {

    unsigned char len=1, buf[5];
    // 字符串编码
    if (ZIP_IS_STR(encoding)) {

        // 1 字节
        // if (encoding == ZIP_STR_06B) { myerr
        //     if (!ptr) return len;
        //     buf[0] = ZIP_STR_06B | rawlen;
        // // 2 字节
        // } else if (encoding == ZIP_STR_14B) {
        //     len += 1;
        //     if (!ptr) return len;
        //     buf[0] = ZIP_STR_14B | (rawlen >> 8);
        //     buf[1] = rawlen & 0xff;
        // // 5 字节
        // } else {
        //     len += 4;
        //     if (!ptr) return len;
        //     buf[0] = ZIP_STR_32B;
        //     buf[1] = (rawlen >> 24) & 0xff;
        //     buf[2] = (rawlen >> 16) & 0xff;
        //     buf[3] = (rawlen >>  8) & 0xff;
        //     buf[4] = rawlen & 0xff;
        // }

        if (rawlen <= 0x3f) {
            if (!ptr) return len;
            buf[0] = ZIP_STR_06B | rawlen;
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!ptr) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        } else {
            len += 4;
            if (!ptr) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >>  8) & 0xff;
            buf[4] = rawlen & 0xff;
        }
    
    // 整数编码
    } else {
        if (!ptr) return len;
        // buf[0] = zipIntSize(encoding); myerr
        buf[0] = encoding;
    }

    // memcpy(ptr,buf,sizeof(value));
    memcpy(ptr,buf,len);

    return len;
}

// len 与 p的节点长度的差值 myerr:缺少
static unsigned int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {

    unsigned int prevlensize;
    // 获取 p 的节点长度
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    return zipPrevEncodeLength(NULL,len) - prevlensize;
}

// 扩容或缩容压缩列表
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    // 重分配内存空间
    zl = realloc(zl, len);

    // 总字节数
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);

    // 末端标识符
    zl[len-1] = ZIP_END;

    return zl;
}

// 将值写入节点
static void zipSaveInteger(unsigned char *p, long long value, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {
        // myerr:缺少
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT_16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev16ifbe(p);
    } else if (encoding == ZIP_INT_24B) {
        i32 = value << 8;
        memrev32ifbe(&i32);
        // memcpy(p+1,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t)); myerr
        memcpy(p,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT_64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        
    } else {
        // assert(NULL);
    }
}

static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen, prevlen = 0;
    size_t offset;
    unsigned char encoding = 0;
    long long value = 123456789;
    int nextdiff = 0;

    zlentry entry, tail;

    // 获取前置节点长度
    if (p[0] != ZIP_END) {
        entry = zipEntry(p);
        prevlen = entry.prevlen;
    } else {
        // zlentry *ptail = ZIPLIST_ENTRY_TAIL(zl); myerr
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail != ZIP_END) {

            prevlen = zipRawEncodeLength(ptail);
        }
    }

    // 尝试将字符串类数值转换成整型
    // 获取当前节点长度
    if (zipTryEncoding(s,slen,&value,&encoding)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算编码前置节点长度所需字节
    reqlen += zipPrevEncodeLength(NULL, prevlen);

    // 计算编码当前节点长度所需字节
    reqlen += zipEncodeLength(NULL, encoding, slen);

    // 后置节点的"前置节点长度"是否足够
    nextdiff = p[0] != ZIP_END ? zipPrevLenByteDiff(p, reqlen) : 0;

    // 扩容,记住偏移量
    offset = p - zl;
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl + offset;

    // 非尾节点添加
    if (p[0] != ZIP_END) {

        // 移动 p 之后的节点
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        // 更新后置节点的"前置节点长度值"
        zipPrevEncodeLength(p+reqlen,reqlen);

        // 更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)+reqlen);

        // 如果新节点之后不止一个节点,nextdiff也要添加到尾节点偏移量中
        tail = zipEntry(p+reqlen);
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)+nextdiff);   
        }

    // 尾节点添加
    } else {
        // 更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    // 写入前置节点长度编码
    p += zipPrevEncodeLength(p,prevlen);

    // 写入当前节点长度编码
    // p += zipEncodeLength(p,encoding,reqlen); myerr
    p += zipEncodeLength(p,encoding,slen);

    // 写入节点值
    if (ZIP_IS_STR(encoding)) {
        memcpy(p,s,slen);
    } else {
        zipSaveInteger(p,value,encoding);
    }

    // 更新节点计数器
    ZIPLIST_LENGTH(zl) = intrev32ifbe(ZIPLIST_LENGTH(zl)+1);

    // 返回
    return zl;
}