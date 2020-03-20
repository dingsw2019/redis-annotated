#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"

// 获取节点的前置编码长度
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do{    \
    if ((ptr)[0] < ZIP_BIGLEN) {                        \
        (prevlensize) = 1;                              \
    } else {                                            \
        (prevlensize) = 5;                              \
    }                                                   \
}while(0)

// 获取节点的前置节点编码字节数和长度
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do{   \
                                                            \
    /* 获取前置节点编码长度*/                                 \
    ZIP_DECODE_PREVLENSIZE((ptr), (prevlensize));           \
    /* 提取前置节点的长度*/                                   \
    if ((prevlensize) == 1) {                               \
        prevlen = (ptr)[0];                                 \
    } else {                                                \
        /*myerr 缺少*/                                      \
        /*assert(sizeof((prevlensize)) == 4)*/              \
        memcpy(&(prevlen),(ptr)+1,4);                       \
        /*myerr 缺少*/                                      \
        memrev32ifbe(&prevlen);                             \
    }                                                       \
}while(0)

// 编码获取整型的存储长度
static int zipIntSize(unsigned char encoding) {
    switch(encoding){
    case ZIP_INT_8B : return 1;
    case ZIP_INT_16B : return 2;
    case ZIP_INT_24B : return 3;
    case ZIP_INT_32B : return 4;
    case ZIP_INT_64B : return 8;
    default: return 0; // myerr : 缺少
    }

    // assert(NULL);
    return 0;
}

// 获取节点的编码字节数和长度, 编码方式
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do{      \
                                                                \
    /* 获取编码类型 */                                           \
    ZIP_DECODE_ENCODING((ptr), (encoding));                     \
                                                                \
    /* 字符串类型 */                                             \
    /* if (ZIP_IS_STR(encoding)) { myerr */                     \
    if ((encoding) < ZIP_STR_MASK) {                            \
                                                                \
        if ((encoding) == ZIP_STR_06B) {                        \
            (lensize) = 1;                                      \
            (len) = (ptr)[0] & 0x3f;                            \
        } else if ((encoding) == ZIP_STR_14B) {                 \
            (lensize) = 2;                                      \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];        \
        } else if ((encoding) == ZIP_STR_32B) {                 \
            (lensize) = 5;                                      \
            (len) = ((ptr)[1] << 24) |                          \
                    ((ptr)[2] << 16) |                          \
                    ((ptr)[3] << 8) |                           \
                    (ptr)[4];                                   \
        } else {                                                \
            /*assert(NULL);*/                                   \
        }                                                       \
                                                                \
    /* 整数类型 */                                               \
    } else {                                                    \
        (lensize) = 1;                                          \
        (len) = zipIntSize(encoding);                           \
    }                                                           \
}while(0)

// 获取 p 的节点信息 并返回
static zlentry zipEntry(unsigned char *p) {
    zlentry e;
    // 获取前置节点所需字节数和长度
    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    // 获取编码类型、当前节点所需字节数和长度
    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    // 计算头部字节数
    e.headersize = e.prevrawlensize + e.lensize;

    // 设置 p 值
    e.p = p;

    return e;
}

// 获取节点的总字节数
static int zipRawEntryLength(unsigned char *p) {

    unsigned int prevlensize, encoding, lensize, len;

    // 前置节点字节数
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    // 当前节点编码字节数和值字节数
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);

    return prevlensize + lensize + len;
}

// 键数值型字符串转换成整型, 将值和编码写入指针
// 成功返回 1, 失败返回 0
// static int zipTryEncoding(unsigned char *p, unsigned char *s, unsigned int slen, int64_t *v, unsigned char *encoding) { myerr
static int zipTryEncoding(unsigned char *p, unsigned int slen, long long *v, unsigned char *encoding) {

    long long value;

    // 值超范围, 不处理
    if (slen >= 32 || slen == 0) return 0;

    // 转换成功
    if (string2ll((char*)p,slen,&value)) {

        // 根据值域换取编码类型
        if (value >= 0 && value <= 12) {
            // encoding = ZIP_INT_IMM_MASK | value; myerr
            *encoding = ZIP_INT_IMM_MASK + value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            // encoding = ZIP_INT_8B; myerr
            *encoding = ZIP_INT_8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            // encoding = ZIP_INT_16B;
            *encoding = ZIP_INT_16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            // encoding = ZIP_INT_24B;
            *encoding = ZIP_INT_24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            // encoding = ZIP_INT_32B;
            *encoding = ZIP_INT_32B;
        } else {
            // encoding = ZIP_INT_64B;
            *encoding = ZIP_INT_64B;
        }

        *v = value;

        return 1;
    }

    // 转换失败
    return 0;
}

// 计算 len 按前置编码规则所占用的字节数
// 返回字节数
// 如果设置了 p , 将字节数写入 p
static int zipPrevEncodeLength(unsigned char *p, unsigned int len) {

    // 只返回编码长度占用字节数
    if (p == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len)+1;
    // 写入前置节点编码长度
    } else {
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;
        } else {
            p[0] = ZIP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));
            // memrev32ifbe(&len); myerr
            memrev32ifbe(p+1);
            return sizeof(len)+1;
        }
    }
}

// 按当前节点的编码规则, 计算 len 所需字节数, 返回字节数
// 如果设置了 p , 编码写入 p
static int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int len) {

    unsigned char len = 1, buf[5];
    // 字符串编码
    if (ZIP_IS_STR(encoding)) {

        // if (len < 0x3f) {  myerr
        if (len <= 0x3f) {
            if (!p) return len;
            // buf[0] = ZIP_STR_06B | (len & 0x3f);
            buf[0] = ZIP_STR_06B | len;
        // } else if (len < 0x3fff) { myerr
        } else if (len <= 0x3fff) {
            len += 1;
            if (!p) return len;
            // buf[0] = ZIP_STR_14B | (len & 0x3f); myerr
            buf[0] = ZIP_STR_14B | ((len >> 8) & 0x3f);
            // buf[1] = (len >> 8) & 0xff; myerr
            buf[1] = len & 0xff;
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            // buf[1] = (len >> 24); myerr
            // buf[2] = (len >> 16);
            // buf[3] = (len >> 8);
            buf[1] = (len >> 24) & 0xff;
            buf[2] = (len >> 16) & 0xff;
            buf[3] = (len >> 8) & 0xff;
            buf[4] = (len & 0xff);
        }
    // 整数编码
    } else {
        if (!p) return len;
        buf[0] = encoding;
    }

    // memcpy(p,buf,sizeof(len)); myerr
    memcpy(p,buf,len);

    return len;
}

// 计算 len 所占字节数 与 p的前置节点长度字节数的差值
static int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {

    unsigned int prevlensize;

    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    return zipPrevEncodeLength(NULL,len) - prevlensize;
}

// 整数列表扩容或缩容
static unsigned *ziplistResize(unsigned char *zl, unsigned int len) {

    // 重分配内存
    zl = realloc(zl, len);

    // 更新列表总字节数
    ZIPLIST_BYTES(zl) = len;

    // 更新列表末端标识符
    zl[len-1] = ZIP_END;

    return zl;
}

// 将整数值写入节点
static void zipSaveInteger(unsigned char *p, unsigned char encoding, int64_t value) {

    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT_16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev16ifbe(&i16);
    } else if (encoding == ZIP_INT_24B) {
        // i32 = value; myerr
        i32 = value << 8;
        memrev32ifbe(&i32);
        // memcpy((uint8_t*)p+1,&i32,sizeof(i32)-sizeof(uint8_t)); myerr 
        memcpy(p,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(&i32);
    } else if (encoding == ZIP_INT_64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(&i64);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX){
        // 已经写入编码了
    } else {
        //assert(NULL);
    }
}

// 先压缩列表的指定节点（p）之【前】添加 节点
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen, prevlen = 0;
    size_t offset;
    unsigned char encoding = 0;
    long long value;
    int nextdiff = 0;
    zlentry entry, tail;


    // 获取指定节点的长度(新节点的前置节点)
    if (p[0] != ZIP_END) {
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail != ZIP_END) {
            prevlen = zipRawEntryLength(p);
        }
    }

    // 如果数值型字符串,那么将其转换成整型
    // 获取待添加值的长度
    if (zipTryEncoding(p,slen,&value,&encoding)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算编码前置节点所需字节数
    reqlen += zipPrevEncodeLength(p, prevlen);

    // 计算编码当前节点所需字节数
    reqlen += zipEncodeLength(p, encoding, slen);

    // 新节点的后置节点的prev编码 是否足够存储新节点长度
    // 计算新节点长度与原prev长度的差值
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

    // 扩容
    offset = p - zl;
    zl = ziplistResize(p, curlen+reqlen+nextdiff);
    p = zl + offset;

    // 非尾节点添加
    if (p[0] != ZIP_END) {

        // 移动 p 之后的内容(包含 p)
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        // 更新"新节点的后置节点"的前置编码长度
        zipPrevEncodeLength(p+reqlen,reqlen);

        // 更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        // 如果新节点之后存在多个节点,需添加 nextdiff 到尾节点偏移量
        tail = zipEntry(p+reqlen);
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }

    // 尾节点添加
    } else {
        // 更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p - zl);
    }

    // 写入新节点前置编码长度
    p += zipPrevEncodeLength(p,prevlen);

    // 写入新节点编码长度
    p += zipEncodeLength(p,reqlen);

    // 写入新节点的值
    if (ZIP_IS_STR(encoding)) {
        memcpy(p,s,slen);
    } else {
        zipSaveInteger(p,encoding,value);
    }

    // 更新节点数量
    ZIPLIST_INCR_LENGTH(zl,1); 

    return zl;
}