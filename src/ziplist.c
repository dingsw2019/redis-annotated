/**
 * 
 * 
 * 为什么用 unsigned char, 详见以下链接
 * @link https://www.runoob.com/cprogramming/c-function-printf.html
 */

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


/*--------------------- private --------------------*/



/**
 * ziplist 标识符
 */
// 末端标识符, 尾节点之后的 1 字节长度的内容
#define ZIP_END 255
// 5 字节长 长度标识符
#define ZIP_BIGLEN 254

/**
 * 字符串编码和整数编码的掩码
 */
// ‭1100 0000‬
#define ZIP_STR_MASK 0xc0
// ‭0011 0000‬
#define ZIP_INT_MASK 0x30

/**
 * 节点编码类型为字符串
 * 字符串编码的类型
 */
#define ZIP_STR_06B (0 << 6)
// ‭0100 0000‬
#define ZIP_STR_14B (1 << 6)
// ‭1000 0000‬
#define ZIP_STR_32B (2 << 6)

/**
 * 整数编码类型
 */
// (1100 0000) | (0000 0000) => 1100 0000 => 192
#define ZIP_INT_16B (0xc0 | 0<<4)
// (1100 0000) | (0001 0000) => 1101 0000 => 208
#define ZIP_INT_32B (0xc0 | 1<<4)
// (1100 0000) | (0010 0000) => 1110 0000 => 224
#define ZIP_INT_64B (0xc0 | 2<<4)
// (1100 0000) | (0011 0000) => 1111 0000 => 240
#define ZIP_INT_24B (0xc0 | 3<<4)
// 0xfe => ‭11111110‬ => 254
#define ZIP_INT_8B 0xfe

/**
 * 4 位整数编码的掩码和类型
 */
#define ZIP_INT_IMM_MASK 0x0f
#define ZIP_INT_IMM_MIN 0xf1 /* 1111 0001 */
#define ZIP_INT_IMM_MAX 0xfd /* 1111 1101 */
#define ZIP_INT_IMM_VAL(v) (v & ZIP_INT_IMM_MASK)

/**
 * 24 位整数的最大值和最小值
 */
#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)

/**
 * 查看给定编码 enc 是否字符串编码
 */
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

/**
 * ziplist 属性宏
 */
// ziplist 的 bytes 属性, 该属性记录了整个 ziplist 所占用的内存字节数
// 用于取出 bytes 属性的值, 或者为 bytes 赋新值
#define ZIPLIST_BYTES(zl) (*((uint32_t*)(zl)))
// ziplist 的 tail 属性, 该属性记录了从列表起始地址到表尾节点的距离
// 用于取出 offset 属性的值, 或者为 offset 赋新值
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// ziplist 的 length 属性, 该属性记录了列表的节点数量
// 用于取出 length 属性的值, 或者为 length 赋新值
#define ZIPLIST_LENGTH(zl) (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
// ziplist 表头的大小
#define ZIPLIST_HEADER_SIZE (sizeof(uint32_t)*2+sizeof(uint16_t))
// 返回指定列表的第一个节点(起始位置)的指针
#define ZIPLIST_ENTRY_HEAD(zl) ((zl)+ZIPLIST_HEADER_SIZE)
// 返回指定列表的最后一个节点(起始位置)的指针
#define ZIPLIST_ENTRY_TAIL(zl) ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
// 返回指定列表的 ZIP_END(起始位置)的指针
#define ZIPLIST_ENTRY_END(zl) ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

// 增加 ziplist 的节点数
#define ZIPLIST_INCR_LENGTH(zl, incr) { \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX) \
        ZIPLIST_LENGTH(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_LENGTH(zl))+incr); \
}

// ziplist 的节点结构
typedef struct zlentry {

    // prevrawlen : 前置节点的长度
    // prevrawlensize : 编码 prevrawlen 所需的字节大小
    unsigned int prevrawlen, prevrawlensize;

    // len : 当前节点值的长度
    // lensize : 编码 len 所需的字节大小
    unsigned int len, lensize;

    // 当前节点 header 的大小
    // 等于 prevrawlensize + lensize
    unsigned int headersize;

    // 当前节点值使用的编码类型
    unsigned char encoding;

    // 指向当前节点值的指针
    unsigned char *p;

} zlentry;

/**
 * 从 ptr 中取出节点值的编码类型, 并保存到 encoding 中
 * T = O(1)
 */
#define ZIP_ENTRY_ENCODING(ptr, encoding) do {  \
    (encoding) = (ptr[0]);  \
    if (encoding < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK;    \
}while(0)

/**
 * 返回保存 encoding 编码的值所需的字节数量
 * 
 * T = O(1)
 */
static unsigned int zipIntSize(unsigned char encoding) {
    
    switch (encoding) {
    case ZIP_INT_8B: return 1;
    case ZIP_INT_16B: return 2;
    case ZIP_INT_24B: return 3;
    case ZIP_INT_32B: return 4;
    case ZIP_INT_64B: return 8;
    default: return 0; /* 4 bit*/
    }

    // assert(NULL);
    return 0;
}

/**
 * 编码节点长度值, 并将它写入到 p 中
 * 返回编码所需的字节数量
 * 如果 p 为 NULL, 仅返回编码所需的字节数量, 不写入
 * 
 * T = O(1)
 */
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {

    // 编码长度
    unsigned char len = 1, buf[5];

    // 字符串编码
    if (ZIP_IS_STR(encoding)) {
        if (rawlen <= 0x3f) {
            if (!p) return len;
            buf[0] = ZIP_STR_06B | rawlen;
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >>  8) & 0xff;
            buf[4] = rawlen & 0xff;
        }
    // 整数编码
    } else {
        if (!p) return len;
        buf[0] = encoding;
    }

    // 编码后的长度写入 p
    memcpy(p,buf,len);

    // 返回编码所需字节数
    return len;

}

/**
 * 解码 ptr 指针, 取出列表节点的属性, 并保存在以下变量中
 * 
 * - encoding 保存节点值的编码类型
 * - lensize 保存编码节点长度所需的字节数
 * - len 保存节点的长度
 * 
 * T = O(1)
 */
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do {         \
                                                                    \
    /* 取出节点值的编码类型 */                                        \
    ZIP_ENTRY_ENCODING((ptr), (encoding));                          \
                                                                    \
    /* 字符串编码 */                                                 \
    if ((encoding) < ZIP_STR_MASK) {                                \
        if ((encoding) == ZIP_STR_06B) {                            \
            (lensize) = 1;                                          \
            (len) = (ptr)[0] & 0x3f;                                \
        } else if ((encoding) == ZIP_STR_14B) {                     \
            (lensize) = 2;                                          \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];            \
        } else if ((encoding) == ZIP_STR_32B) {                     \
            (lensize) = 5;                                          \
            (len) = ((ptr)[1] << 24) |                              \
                    ((ptr)[2] << 16) |                              \
                    ((ptr)[3] <<  8) |                              \
                    ((ptr)[4]);                                     \
        } else {                                                    \
            /*assert(NULL);*/                                           \
        }                                                           \
    /* 整数编码 */                                                   \
    } else {                                                        \
        (lensize) = 1;                                              \
        (len) = zipIntSize(encoding);                               \
    }                                                               \
                                                                    \
}while(0);

/**
 * 对前置节点的长度 len 进行编码, 并将它写入 p 中
 * 返回编码 len 所需的字节数量
 * 
 * 如果 p 为 NULL , 那么不进行写入, 仅返回 len 所需字节数量
 *  
 * T = O(1)
 */
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {

    // 仅返回编码 len 所需的字节数量
    if (p == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len)+1;
    
    // 写入并返回编码 len 所需的字节数量
    }else {

        // 1 字节
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;

        // 5 字节
        } else {
            // 添加 5 字节长度标识
            p[0] = ZIP_BIGLEN;
            // 长度写入编码
            memcpy(p+1,&len,sizeof(len));
            // 如果有必要的话, 进行大小端转换
            memrev32ifbe(p+1);
            // 返回编码长度
            return 1+sizeof(len);
        }
    }
}

/**
 * 将一个不需要 5 字节存储的长度, 放到 5 字节中
 */
static void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len) {

    if (p == NULL) return ;

    // 5 字节标识符
    p[0] = ZIP_BIGLEN;

    // 写入长度
    memcpy(p+1,&len,sizeof(len));
    // 大小端转换
    memrev32ifbe(p+1);
}

/**
 * 解码 ptr 指针
 * 取出编码前置节点长度所需的字节数, 并将它保存到 prevlensize 变量中
 * 
 * T = O(1)
 */
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do {   \
    if ((ptr)[0] < ZIP_BIGLEN) {                        \
        (prevlensize) = 1;                              \
    } else {                                            \
        (prevlensize) = 5;                              \
    }                                                   \
} while(0);

/**
 * 解码 ptr 指针
 * 取出"编码前置节点长度所需的字节数", 保存到 prevlensize 中
 * 取出"前置节点长度", 保存到 prevlen 中
 *
 * T = O(1)
 */
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do{                       \
                                                                                \
    /* 计算编码前置节点的字节数 */                                                \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);                                   \
                                                                                \
    /* 如果是 1 字节, 取 1 字节的内容就是前置节点的长度 */                          \
    if ((prevlensize) == 1) {                                                   \
        (prevlen) = (ptr)[0];                                                   \
    }                                                                           \
    /* 如果是 5 字节, 需跳过 1 字节(第一个字节被设置为 0xFE,十进制值254) */         \
    /* 后 4 字节才是前置节点的长度 */                                             \
    else if ((prevlensize) == 5){                                               \
        /*assert(sizeof((prevlensize)) == 4);*/                                     \
        memcpy(&(prevlen), ((char*)(ptr))+1, 4);                                \
        memrev32ifbe(&prevlen);                                                 \
    }                                                                           \
}while(0);

/**
 * 计算新旧前置节点长度的差值
 * 
 * T = O(1)
 */
static int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {

    unsigned int prevlensize;
    
    // 取出编码原前置节点长度所需的字节数
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    // 计算编码 len 所需的字节数, 然后计算新旧差值
    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

/**
 * 返回指针 p 指向的节点占用的字节数总和
 * 
 * T = O(1)
 */
static unsigned int zipRawEntryLength(unsigned char *p) {

    unsigned int prevlensize, prevlen, encoding, lensize, len;

    // 获取存储"前置节点长度"所需的字节数
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    // 获取存储"当前节点长度"所需的字节数,
    // 当前字节长度
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);

    // 计算节点占用的字节数总和
    return prevlensize + lensize + len;
}

/**
 * 检查 entry 中的 content 的字符串能否被编码成整数(比如:十六进制数已字符串存储)
 * 
 * 如果可以转化,
 * 将编码后的整数存储到指针 v , 编码方式存储到指针 encoding 中
 * 
 * 转换成功返回 1 , 否则返回 0
 * T = O(N)
 */
static int zipTryEncoding(unsigned char *entry, unsigned int entrylen, long long *v, unsigned char *encoding) {

    long long value;

    // 忽略太长或太短的字符串
    if (entrylen >= 32 || entrylen == 0)
        return 0;

    // 尝试将字符串转换成的整数
    if (string2ll((char*)entry,entrylen,&value)) {

        // 转换成功,匹配 value 的编码方式
        if (value >= 0 && value <= 12) {
            *encoding = ZIP_INT_IMM_MIN + value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT_8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT_16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT_24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT_32B;
        } else {
            *encoding = ZIP_INT_64B;
        }

        // 转换的整数赋值给指针
        *v = value;

        // 转换成功
        return 1;
    }

    // 转换失败
    return 0;
}

/**
 * 已 encoding 指定的编码方式, 将整数值 value 写入 p
 *
 * T = O(1)
 */
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {

    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT_16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev16ifbe(p);
    } else if (encoding == ZIP_INT_24B) {
        i32 = value<<8;
        memrev32ifbe(&i32);
        // ?? ((uint8_t*)&i32)+1 不懂, 不应该是 ((uint8_t*)&i32) 取前 3位
        // 加 1 不就变成 第2字节开始取值了吗, 第4字节是空的
        memcpy(p,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT_64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX){
        // 不需要做, 值存储在编码中
    } else {
        // assert(NULL);
    }
}

/**
 * 以 encoding 指定的编码方式, 读取并返回指针 p 中的整数值
 * 
 * T = O(1)
 */
static int64_t zipLoadInteger(unsigned char *p, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64, ret = 0;

    // 读取整数值
    if (encoding == ZIP_INT_8B) {
        ret = ((int8_t*)p)[0];
    } else if (encoding == ZIP_INT_16B) {
        memcpy(&i16,p,sizeof(i16));
        memrev16ifbe(&i16);
        ret = i16;
    } else if (encoding == ZIP_INT_24B) {
        i32 = 0;
        memcpy(((uint8_t*)&i32)+1,p,sizeof(i32)-sizeof(uint8_t));
        memrev32ifbe(&i32);
        ret = i32>>8;
    } else if (encoding == ZIP_INT_32B) {
        memcpy(&i32,p,sizeof(i32));
        memrev32ifbe(&i32);
        ret = i32;
    } else if (encoding == ZIP_INT_64B) {
        memcpy(&i64,p,sizeof(i64));
        memrev64ifbe(&i64);
        ret = i64;
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        ret = (encoding & ZIP_INT_IMM_MASK) - 1;
    } else {
        // assert(NULL); 
    }

    return ret;
}

/**
 * 将 p 所指向的列表节点的信息全部保存到 zlentry 中, 并返回该 zlentry
 * 
 * T = O(1)
 */
static zlentry zipEntry(unsigned char *p) {
    zlentry e;

    // e.prevrawlensize 保存编码前一个节点的长度所需的字节数
    // e.prevrawlen 保存着前一个节点的长度
    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    // p + e.prevrawlensize, 当前节点跳到编码起始地址
    // e.encoding 保存节点值的编码类型
    // e.lensize 保存着编码节点值长度所需的节点数
    // e.len 保存着节点值的长度
    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    // 计算头节点的字节数
    e.headersize = e.prevrawlensize + e.lensize;

    // 记录指针
    e.p = p;

    return e;
}



/**
 * 检查并执行连锁更新
 * 添加新节点的长度超出后置节点的prevlensize, 就需要扩容后置节点的 prevlensize
 * 反之,删除节点 prevlensize 也会更新但不会缩小内存空间
 * 
 * 返回处理完的压缩列表
 */
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char *np;
    zlentry cur, next; 

    while (p[0] != ZIP_END) {

        // 获取当前节点的字节数
        cur = zipEntry(p);
        rawlen = cur.headersize + cur.len;

        // 计算编码当前节点的长度所需的字节数
        rawlensize = zipPrevEncodeLength(NULL, rawlen);

        // 下一个节点是否存在
        if (p[rawlen] == ZIP_END) break;

        // 获取下一个节点的数据
        next = zipEntry(p+rawlen);

        // 后置节点的prev长度 与 当前节点长度完全相同
        // 那就不用改呀
        if (next.prevrawlen == rawlen) break;

        // 触发更新
        if (next.prevrawlensize < rawlen) {
            
            offset = p - zl;

            // 计算字节数差值
            extra = rawlensize - next.prevrawlensize;

            // 扩容
            zl = ziplistResize(zl, curlen+extra);
            p = zl + offset;

            // 下一个节点的偏移量
            np = p + rawlen;
            noffset = np - zl;

            // next 不是最后一个节点的话, 更新尾节点偏移量
            if ((zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+extra);
            }

            // 移动原节点数据, 给 next.prev腾出空间
            memmove(np+rawlensize, np+next.prevrawlensize, curlen-noffset-1-next.prevrawlensize);

            // 写入前置节点长度
            zipPrevEncodeLength(np,rawlensize);

            // 处理下一个节点
            p += rawlen;
            curlen += extra;

        // 未触发更新
        } else {

            // 后置节点的prev字节数大于当前字节数
            if (next.prevrawlensize > rawlensize) {

                zipPrevEncodeLengthForceLarge(p+rawlen, rawlen);
            // 后置节点的prev字节数等于当前字节数
            } else {
                zipPrevEncodeLength(p+rawlen,rawlen);
            }

            break;
        }
    }

    return zl;
}

/*--------------------- API --------------------*/
/**
 * 创建并返回一个空的压缩列表
 */
unsigned char *ziplistNew(void) {

    // 空列表的总字节数
    // ZIPLIST_HEADER_SIZE 是表头的大小
    // +1 是 ZIP_END 的大小
    unsigned int bytes = ZIPLIST_HEADER_SIZE+1;

    // 为表头和ZIP_END 申请内存空间
    unsigned char *zl = zmalloc(bytes);

    // 初始化属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
    ZIPLIST_LENGTH(zl) = 0;

    // 设置表末端标识符
    zl[bytes-1] = ZIP_END;

    // 返回
    return zl;
}

/**
 * 调整压缩列表的大小为 len 字节
 * 
 * 当 ziplist 原长度小于 len 时, 扩展后的现有元素不会改变
 * 
 * T = O(N)
 */
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    // realloc, 扩容不改变现有元素
    zl = zrealloc(zl, len);

    // 更新列表总字节数
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);

    // 更新列表末端标识符
    zl[len-1] = ZIP_END;

    return zl;
}

/**
 * 根据指针 p 所指定的位置, 将长度为 slen 的字符串 s 添加到压缩列表中
 * 
 * 返回添加完节点的压缩列表
 * 
 * T = O(N^2)
 */
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen, prevlen = 0;
    size_t offset;
    unsigned char encoding = 0;
    int nextdiff = 0;;
    /* 初始化为避免警告 , 写 123456789 是为了好认*/
    long long value = 123456789;

    zlentry entry, tail;

    // 获取前置节点长度
    if (p[0] != ZIP_END) {
        // 走到这里说明,列表节点数非空, p 正指向某个节点
        // 那么取出 p 的节点信息, 并提取前一个节点的长度
        // (当插入新节点之后, p 是新节点的前置节点)
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        // 走到这里,列表就可能无节点了,需要检查
        // 1.如果 ptail 也指向 ZIP_END, 那么空列表实锤
        // 2.如果列表不为空, 那么用 ptail 指向最后一个节点
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {
            // 表尾节点为新节点的前置节点
            // 取出表尾节点的长度
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 尝试将数值型字符串转换为整数, 如果转换成功：
    // 1. value 保存转换后的整数值
    // 2. encoding 保存适用于 value 的编码方式
    // 无论使用什么编码, reqlen 都保存"当前节点值"的长度
    if (zipTryEncoding(s,slen,&value,&encoding)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算编码前置节点的长度所需的大小
    reqlen += zipPrevEncodeLength(NULL, prevlen);

    // 计算编码当前节点值所需的大小
    reqlen += zipEncodeLength(NULL, encoding, slen);

    // 只要新节点不是添加到列表末端
    // 那么程序就需要检查 p 所指向的节点(的 header) 能否编码新节点的长度
    // nextdiff 保存新旧编码之间的字节差值, 如果值大于 0 
    // 说明需要对 p 指向的节点(的 header) 进行扩展
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p,reqlen) : 0;

    // 因为重分配空间可能会改变 zl 的地址
    // 所以在分配之前, 记住 zl 到 p 的偏移量, 在重分配后依靠偏移量还原 p
    offset = p - zl;
    
    // 增加节点,扩容列表的内存空间
    // curlen 是 ziplist 扩容前的长度
    // reqlen 是新节点的长度
    // nextdiff 是新节点的后置节点扩容 header 的长度 (要么 0 字节, 要么 4 字节)
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl + offset;

    if (p[0] != ZIP_END) {
        // 新节点有后置节点,因为新节点加入,要对之后的所有节点做调整

        // 移动新节点之后的节点位置,腾出空间添加新节点
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        // 将新节点的长度, 编码到其后置节点
        // p + reqlen 定位后置节点
        // reqlen 是新节点的长度
        zipPrevEncodeLength(p+reqlen, reqlen);

        // 更新到达列表尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        // 如果新节点的后面有多于一个节点
        // 那么程序需要将 nextdiff 也计算到表尾偏移量中
        // 这样才能让表尾偏移量正确对齐表尾节点
        tail = zipEntry(p+reqlen);    
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }

    } else {
        // 新元素是尾节点,更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    // 当 nextdiff != 0 时, 新节点的后置节点的header长度可能发生改变
    // 所以需要检查是否触发 "连锁更新"
    if (nextdiff != 0) {
        offset = p - zl;
        zl = __ziplistCascadeUpdate(zl, p+reqlen);
        p = zl + offset;
    }

    // 前置节点的长度写入新节点的 header
    p += zipPrevEncodeLength(p,prevlen);

    // 当前节点的长度写入新节点的 header
    p += zipEncodeLength(p, encoding, slen);

    // 写入节点值
    if (ZIP_IS_STR(encoding)) {
        memcpy(p, s, slen);
    } else {
        zipSaveInteger(p, value, encoding);
    }

    // 更新列表的节点计数器
    ZIPLIST_INCR_LENGTH(zl, 1);

    return zl;
}

/**
 * 将长度为 slen 的字符串 s 添加到压缩列表中
 * 
 * where 表示添加方向
 * 如果等于 ZIPLIST_HEAD , 从表头插入新节点
 * 如果等于 ZIPLIST_TAIL , 从表尾插入新节点
 * 
 * 返回值为添加新值后的压缩列表
 * 
 * T = O(N^2)
 */
unsigned char *ziplistPush(unsigned char *zl,unsigned char *s,unsigned int slen,int where) {

    // 根据添加方向计算添加的位置的起始地址
    unsigned char *p;
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);

    // 添加新值
    return __ziplistInsert(zl,p,s,slen);
}



/*--------------------- debug --------------------*/
#include <sys/time.h>
#include "adlist.h"
#include "sds.h"

unsigned char *createIntList() {
    unsigned char *zl = ziplistNew();
    char buf[32];

    sprintf(buf, "100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "128000");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "-100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    // sprintf(buf, "4294967296");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    // sprintf(buf, "non integer");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    // sprintf(buf, "much much longer non integer");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    return zl;
}

void ziplistRepr(unsigned char *zl) {
    unsigned char *p;
    int index = 0;
    zlentry entry;

    printf(
        "{total bytes %d} "
        "{length %u}\n"
        "{tail offset %u}\n",
        intrev32ifbe(ZIPLIST_BYTES(zl)),
        intrev16ifbe(ZIPLIST_LENGTH(zl)),
        intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
    p = ZIPLIST_ENTRY_HEAD(zl);
    while(*p != ZIP_END) {
        entry = zipEntry(p);
        printf(
            "{"
                "addr 0x%08lx, "
                "index %2d, "
                "offset %5ld, "
                "rl: %5u, "
                "hs %2u, "
                "pl: %5u, "
                "pls: %2u, "
                "payload %5u"
            "} ",
            (long unsigned)p,
            index,
            (unsigned long) (p-zl),
            entry.headersize+entry.len,
            entry.headersize,
            entry.prevrawlen,
            entry.prevrawlensize,
            entry.len);
        p += entry.headersize;
        if (ZIP_IS_STR(entry.encoding)) {
            if (entry.len > 40) {
                if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
                printf("...");
            } else {
                if (entry.len &&
                    fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
            }
        } else {
            printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
        }
        printf("\n");
        p += entry.len;
        index++;
    }
    printf("{end}\n\n");
}

// gcc -g zmalloc.c sds.c util.c ziplist.c
int main(void) {
    unsigned char *zl, *p;
    unsigned char *entry;
    unsigned int elen;
    long long value;

    /* If an argument is given, use it as the random seed. */
    // if (argc == 2)
        // srand(atoi(argv[1]));
    srand(time(NULL));

    zl = createIntList();
    ziplistRepr(zl);

    // zl = createList();
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_HEAD);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // printf("Get element at index 3:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 3);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index 3\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index 4 (out of range):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 4);
    //     if (p == NULL) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
    //         return 1;
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -1 (last element):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index -1\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -4 (first element):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -4);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index -4\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -5 (reverse out of range):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -5);
    //     if (p == NULL) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
    //         return 1;
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 0 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 0);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 1 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 2 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 2);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate starting out of range:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 4);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate from back to front:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistPrev(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate from back to front, deleting all items:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         zl = ziplistDelete(zl,&p);
    //         p = ziplistPrev(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Delete inclusive range 0,0:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 0, 1);
    //     ziplistRepr(zl);
    // }

    // printf("Delete inclusive range 0,1:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 0, 2);
    //     ziplistRepr(zl);
    // }

    // printf("Delete inclusive range 1,2:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 1, 2);
    //     ziplistRepr(zl);
    // }

    // printf("Delete with start index out of range:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 5, 1);
    //     ziplistRepr(zl);
    // }

    // printf("Delete with num overflow:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 1, 5);
    //     ziplistRepr(zl);
    // }

    // printf("Delete foo while iterating:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl,0);
    //     while (ziplistGet(p,&entry,&elen,&value)) {
    //         if (entry && strncmp("foo",(char*)entry,elen) == 0) {
    //             printf("Delete foo\n");
    //             zl = ziplistDelete(zl,&p);
    //         } else {
    //             printf("Entry: ");
    //             if (entry) {
    //                 if (elen && fwrite(entry,elen,1,stdout) == 0)
    //                     perror("fwrite");
    //             } else {
    //                 printf("%lld",value);
    //             }
    //             p = ziplistNext(zl,p);
    //             printf("\n");
    //         }
    //     }
    //     printf("\n");
    //     ziplistRepr(zl);
    // }

    // printf("Regression test for >255 byte strings:\n");
    // {
    //     char v1[257],v2[257];
    //     memset(v1,'x',256);
    //     memset(v2,'y',256);
    //     zl = ziplistNew();
    //     zl = ziplistPush(zl,(unsigned char*)v1,strlen(v1),ZIPLIST_TAIL);
    //     zl = ziplistPush(zl,(unsigned char*)v2,strlen(v2),ZIPLIST_TAIL);

    //     /* Pop values again and compare their value. */
    //     p = ziplistIndex(zl,0);
    //     assert(ziplistGet(p,&entry,&elen,&value));
    //     assert(strncmp(v1,(char*)entry,elen) == 0);
    //     p = ziplistIndex(zl,1);
    //     assert(ziplistGet(p,&entry,&elen,&value));
    //     assert(strncmp(v2,(char*)entry,elen) == 0);
    //     printf("SUCCESS\n\n");
    // }

    // printf("Regression test deleting next to last entries:\n");
    // {
    //     char v[3][257];
    //     zlentry e[3];
    //     int i;

    //     for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
    //         memset(v[i], 'a' + i, sizeof(v[0]));
    //     }

    //     v[0][256] = '\0';
    //     v[1][  1] = '\0';
    //     v[2][256] = '\0';

    //     zl = ziplistNew();
    //     for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
    //         zl = ziplistPush(zl, (unsigned char *) v[i], strlen(v[i]), ZIPLIST_TAIL);
    //     }

    //     verify(zl, e);

    //     assert(e[0].prevrawlensize == 1);
    //     assert(e[1].prevrawlensize == 5);
    //     assert(e[2].prevrawlensize == 1);

    //     /* Deleting entry 1 will increase `prevrawlensize` for entry 2 */
    //     unsigned char *p = e[1].p;
    //     zl = ziplistDelete(zl, &p);

    //     verify(zl, e);

    //     assert(e[0].prevrawlensize == 1);
    //     assert(e[1].prevrawlensize == 5);

    //     printf("SUCCESS\n\n");
    // }

    // printf("Create long list and check indices:\n");
    // {
    //     zl = ziplistNew();
    //     char buf[32];
    //     int i,len;
    //     for (i = 0; i < 1000; i++) {
    //         len = sprintf(buf,"%d",i);
    //         zl = ziplistPush(zl,(unsigned char*)buf,len,ZIPLIST_TAIL);
    //     }
    //     for (i = 0; i < 1000; i++) {
    //         p = ziplistIndex(zl,i);
    //         assert(ziplistGet(p,NULL,NULL,&value));
    //         assert(i == value);

    //         p = ziplistIndex(zl,-i-1);
    //         assert(ziplistGet(p,NULL,NULL,&value));
    //         assert(999-i == value);
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Compare strings with ziplist entries:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl,0);
    //     if (!ziplistCompare(p,(unsigned char*)"hello",5)) {
    //         printf("ERROR: not \"hello\"\n");
    //         return 1;
    //     }
    //     if (ziplistCompare(p,(unsigned char*)"hella",5)) {
    //         printf("ERROR: \"hella\"\n");
    //         return 1;
    //     }

    //     p = ziplistIndex(zl,3);
    //     if (!ziplistCompare(p,(unsigned char*)"1024",4)) {
    //         printf("ERROR: not \"1024\"\n");
    //         return 1;
    //     }
    //     if (ziplistCompare(p,(unsigned char*)"1025",4)) {
    //         printf("ERROR: \"1025\"\n");
    //         return 1;
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Stress with random payloads of different encoding:\n");
    // {
    //     int i,j,len,where;
    //     unsigned char *p;
    //     char buf[1024];
    //     int buflen;
    //     list *ref;
    //     listNode *refnode;

    //     /* Hold temp vars from ziplist */
    //     unsigned char *sstr;
    //     unsigned int slen;
    //     long long sval;

    //     for (i = 0; i < 20000; i++) {
    //         zl = ziplistNew();
    //         ref = listCreate();
    //         listSetFreeMethod(ref,sdsfree);
    //         len = rand() % 256;

    //         /* Create lists */
    //         for (j = 0; j < len; j++) {
    //             where = (rand() & 1) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
    //             if (rand() % 2) {
    //                 buflen = randstring(buf,1,sizeof(buf)-1);
    //             } else {
    //                 switch(rand() % 3) {
    //                 case 0:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()) >> 20);
    //                     break;
    //                 case 1:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()));
    //                     break;
    //                 case 2:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()) << 20);
    //                     break;
    //                 default:
    //                     assert(NULL);
    //                 }
    //             }

    //             /* Add to ziplist */
    //             zl = ziplistPush(zl, (unsigned char*)buf, buflen, where);

    //             /* Add to reference list */
    //             if (where == ZIPLIST_HEAD) {
    //                 listAddNodeHead(ref,sdsnewlen(buf, buflen));
    //             } else if (where == ZIPLIST_TAIL) {
    //                 listAddNodeTail(ref,sdsnewlen(buf, buflen));
    //             } else {
    //                 assert(NULL);
    //             }
    //         }

    //         assert(listLength(ref) == ziplistLen(zl));
    //         for (j = 0; j < len; j++) {
    //             /* Naive way to get elements, but similar to the stresser
    //              * executed from the Tcl test suite. */
    //             p = ziplistIndex(zl,j);
    //             refnode = listIndex(ref,j);

    //             assert(ziplistGet(p,&sstr,&slen,&sval));
    //             if (sstr == NULL) {
    //                 buflen = sprintf(buf,"%lld",sval);
    //             } else {
    //                 buflen = slen;
    //                 memcpy(buf,sstr,buflen);
    //                 buf[buflen] = '\0';
    //             }
    //             assert(memcmp(buf,listNodeValue(refnode),buflen) == 0);
    //         }
    //         zfree(zl);
    //         listRelease(ref);
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Stress with variable ziplist size:\n");
    // {
    //     stress(ZIPLIST_HEAD,100000,16384,256);
    //     stress(ZIPLIST_TAIL,100000,16384,256);
    // }

    return 0;
}