/**
 * 
 * 
 * 为什么用 unsigned char, 详见以下链接
 * @link https://www.runoob.com/cprogramming/c-function-printf.html
 * 
 * 图解Redis压缩列表-数据结构
 * @link https://www.jianshu.com/p/30d1b5b6d62d
 * 
 * 图解Redis压缩列表-添加节点
 * @link https://www.jianshu.com/p/8205336ed227
 * 
 * 图解Redis压缩列表-连锁更新
 * @link https://www.jianshu.com/p/b5d111bee6d9
 * 
 * 图解Redis压缩列表-删除节点
 * @link https://www.jianshu.com/p/62ccf148266e
 * 
 * 图解Redis压缩列表-遍历节点
 * @link https://www.jianshu.com/p/3f5fd13d2e5c
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
// #include "redisassert.h"


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
        // 比如 value 的二进制为 1001 0101 0000 0111 0001 1011
        // i32 的二进制为        1001 0101 0000 0111 0001 1011 0000 0000
        // uint8_t之后i32索引   <--  3 --><--  2 --><--  1 --><--  0 -->
        // 复制后的p的对应关系   <-- p2 --><-- p1 --><-- p0 -->
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
        // 比如 p 的二进制为   1001 0101 0000 0111 0001 1011
        // p 的索引关系       <-- p2 --><-- p1 --><-- p0 -->
        // i32 现在的值       0000 0000 0000 0000 0000 0000 0000 0000
        // uint8_t之后i32索引 <--  3 --><--  2 --><--  1 --><--  0 -->
        // 复制之后的 i32      1001 0101 0000 0111 0001 1011 0000 0000
        // 右移后的 i32        1001 0101 0000 0111 0001 1011
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
        ret = (encoding & ZIP_INT_IMM_MASK) - 1;// 减1 是因为ZIP_INT_IMM_MIN值为1
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

        // 触发扩容
        if (next.prevrawlensize < rawlensize) {
            
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
            zipPrevEncodeLength(np,rawlen);

            // 处理下一个节点
            p += rawlen;
            curlen += extra;

        // 未触发扩容
        } else {

            // 后置节点的prev字节数大于当前字节数
            if (next.prevrawlensize > rawlensize) {
                
                // 修改前置节点的总长度
                zipPrevEncodeLengthForceLarge(p+rawlen, rawlen);
            // 后置节点的prev字节数等于当前字节数
            } else {
                zipPrevEncodeLength(p+rawlen,rawlen);
            }

            // 当前节点没有扩容, 字节数不变, 
            // 之后的节点就没有增加的条件了, 所以结束循环
            break;
        }
    }

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
 * 从位置 p 开始, 连续删除 num 个节点
 * 返回删除后的压缩列表
 * 
 * T = O(N^2)
 */
static unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num) {

    unsigned int i, totlen, deleted=0;
    size_t offset;
    int nextdiff = 0;
    zlentry first, tail;

    // 获取删除的第一个节点的信息
    first = zipEntry(p);

    // 遍历删除节点, 计算实际删除节点数,获取删除的最后一个节点
    for (i=0; p[0] != ZIP_END && i < num; i++) {
        p += zipRawEntryLength(p);
        deleted++;
    }

    // 计算删除的总字节数
    totlen = p - first.p;
    // 删除字节数 > 0 再进行后续处理
    if (totlen > 0) {

        // 删除的最后一个节点之后还有节点
        if (p[0] != ZIP_END) {

            // 计算删除的最后一个节点的下一个节点的 prevlensize
            // 能否存储删除的第一个节点的前一个节点的长度
            nextdiff = zipPrevLenByteDiff(p,first.prevrawlen);

            // 是否需要前移, 给扩容 prevlensize 留空间
            p -= nextdiff;

            // 将 first 的前置节点的长度编码至 p 中
            zipPrevEncodeLength(p, first.prevrawlen);

            // 更新尾节点偏移量
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))-totlen);

            // 如果删除的最后一个节点之后有一个以上的节点
            // nextdiff 也要加到尾节点偏移量
            tail = zipEntry(p);
            if (p[tail.headersize+tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
            }

            // 移动原数据, 覆盖删除的字节 ??
            memmove(first.p,p,
                intrev32ifbe(ZIPLIST_BYTES(zl))-(p-zl)-1);
        
        // 已经删到末端标识符了
        } else {
            // 更新尾节点偏移量
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe((first.p-zl)-first.prevrawlen);
        }

        // 释放删除的节点的内容空间
        offset = first.p - zl;
        ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl))-totlen+nextdiff);
        ZIPLIST_INCR_LENGTH(zl, -deleted);
        p = zl + offset;

        // 连锁更新
        if (nextdiff != 0)
            zl = __ziplistCascadeUpdate(zl, p);
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

/**
 * 返回给定索引对应的指针
 * 正索引, 从表头向表尾遍历, >=0 为正索引
 * 负索引, 从表尾向表头遍历, <=-1 为负索引
 * 
 * 索引值超范围或压缩列表为空, 返回 NULL
 */
unsigned char *ziplistIndex(unsigned char *zl, int index) {

    unsigned char *p;
    zlentry entry;

    // 负索引
    if (index < 0) {

        // 遍历次数
        index = (-index) - 1;

        // 从尾节点开始遍历
        p = ZIPLIST_ENTRY_TAIL(zl);

        // 列表不为空
        if (p[0] != ZIP_END) {

            entry = zipEntry(p);

            // 迭代遍历
            while (entry.prevrawlen > 0 && index--) {
                // 指针移动到前置节点
                p -= entry.prevrawlen;
                entry = zipEntry(p);
            }
        }

    // 正索引
    } else {

        // 从头节点开始遍历
        p = ZIPLIST_ENTRY_HEAD(zl);

        // 迭代遍历
        while (p[0] != ZIP_END && index--) {
            // 指针移动到下一个节点
            p += zipRawEntryLength(p);
        }
    }

    return (p[0] == ZIP_END || index > 0) ? NULL : p;
}

/**
 * 返回 p 所指向节点的后置节点
 * 
 * 如果 p 是末端标识符, 那么返回 NULL
 *
 * T = O(1)
 */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p) {
    ((void) zl);
    // p 已经指向列表末端
    if (p[0] == ZIP_END)
        return NULL;
    
    // 指向后置节点
    p += zipRawEntryLength(p);

    // p 已经是列表末端, 无后置节点
    if (p[0] == ZIP_END) 
        return NULL;

    return p;
}

/**
 * 返回 p 所指向节点的前置节点地址
 * 
 * 如果 p 所指向为空列表或者 p 已经指向表头节点, 那么返回 NULL
 * 
 * T = O(1)
 */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p) {
    
    zlentry entry;
    
    // 如果 p 指向列表末端, 尝试取出尾节点
    if (p[0] == ZIP_END) {
        p = ZIPLIST_ENTRY_TAIL(zl);
        return (p[0] == ZIP_END) ? NULL : p;
    
    // p 指向表头, 说明迭代完成
    } else if (p == ZIPLIST_ENTRY_HEAD(zl)) {
        return NULL;
    // 不是表头或表尾, 从表尾向表头移动
    } else {
        entry = zipEntry(p);
        // assert(entry.prevrawlen > 0);
        return p-entry.prevrawlen;
    }
}

/**
 * 获取指针 p 指向的节点数据
 * 如果节点保存的是字符串, 将字符串保存到 *sstr, 字符串长度保存在 *slen
 * 如果节点保存的是整数, 将整数值保存到 *sval
 *
 * 节点存在,提取成功返回 1
 * 节点为空或者不存在, 提取失败返回 0
 */
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval) {

    zlentry entry;
    // p 为空 , 或者指向末端终结符
    // 无法提取节点, 返回
    if (p == NULL || p[0] == ZIP_END) return 0;
    
    // 清空 sstr 
    if (sstr) *sstr = NULL;

    // 获取节点信息
    entry = zipEntry(p);

    // 字符串类型
    if (ZIP_IS_STR(entry.encoding)) {

        // 保存字符串内容和长度
        if (sstr) {
            *sstr = p+entry.headersize;
            *slen = entry.len;
        }
    // 整数类型
    } else {

        // 保存整数值
        if (sval) {
            *sval = zipLoadInteger(p+entry.headersize, entry.encoding); 
        }
    }

    return 1;
}

unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    return __ziplistInsert(zl,p,s,slen);
}

/**
 * 从 zl 中删除 *p 指向的节点, 返回处理后的压缩列表
 * 并且更新 *p 位置, 使得可以在迭代列表的过程中对节点进行删除
 * 
 * T = O(N^2);
 */
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p) {

    // 删除节点
    size_t offset = *p-zl;
    zl = __ziplistDelete(zl,*p,1);

    // 更新 p 的位置
    *p = zl+offset;

    return zl;
}

/**
 * 从指定索引的节点处开始，连续删除 num 个节点
 * 返回处理后的列表
 */
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num) {

    // 通过索引获取起始节点的地址
    unsigned char *p = ziplistIndex(zl, index);

    // 执行连续删除
    return (p == NULL) ? zl : __ziplistDelete(zl, p, num);
}

/**
 * 将 p 指向的节点值与 sstr 比对
 * 如果相同返回 1, 否则返回 0
 * 
 * T = O(N)
 */
unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen) {

    zlentry entry;
    unsigned char sencoding = 0;
    long long zval, sval;

    if (p[0] == ZIP_END) return 0;

    // 获取 p 的节点数据
    entry = zipEntry(p);

    // 字符串
    if (ZIP_IS_STR(entry.encoding)) {

        // 长度和内容相同,返回 1
        if (entry.len == slen) {
            return memcmp(p+entry.headersize, sstr, slen) == 0;
        } else {
            return 0;
        }
    // 整数
    } else {
        
        // 给 sstr 解码, 获取整数值和编码
        if (zipTryEncoding(sstr,slen,&sval,&sencoding)) {
            // 获取节点的整数值
            zval = zipLoadInteger(p+entry.headersize, entry.encoding);
            // 比对整数
            return zval == sval;
        }
    }

    return 0;
}

/**
 * 寻找并返回节点值与 vstr 相等的节点
 * 每次对比之间先跳过 skip 个节点
 * 如果未找到节点返回 NULL
 * 
 * T = O(N^2)
 */
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip) {

    int skipcnt = 0;
    long long vll = 0;
    unsigned char vencoding = 0;

    // 迭代节点
    while (p[0] != ZIP_END) {

        unsigned int prevlensize, encoding, lensize, len;
        unsigned char *q;

        // 获取当前节点信息
        ZIP_DECODE_PREVLENSIZE(p, prevlensize);
        ZIP_DECODE_LENGTH(p+prevlensize, encoding, lensize, len);

        // 节点值地址
        q = p + prevlensize + lensize;

        // 搜索节点
        if (skipcnt == 0) {
            // 字符串
            if (ZIP_IS_STR(encoding)) {
                // 值相同,返回
                if (len == vlen && memcmp(q,vstr,vlen) == 0) {
                    return p;
                }
            
            // 整数
            } else {

                // vstr 可能编码了, 将它转换成整数
                // 只执行一次
                if (vencoding == 0) {
                    if (!zipTryEncoding(vstr,vlen,&vll,&vencoding)) {
                        vencoding = UCHAR_MAX;
                    }
                    // accsert(vencoding);
                }

                // 值相同,返回
                if (vencoding != UCHAR_MAX) {
                    long long ll = zipLoadInteger(q, encoding);
                    if (vll == ll) {
                        return p;
                    }
                }
            }

            // 重置跳过节点数
            skipcnt = skip;

        // 跳过节点
        } else {
            skipcnt--;
        }

        // 指向下一个节点
        p = q + len;
    }

    // 未找到
    return NULL;
}

/**
 * 返回压缩列表的节点数量
 *
 * T = O(N)
 */
unsigned int ziplistLen(unsigned char *zl) {

    unsigned int len = 0;

    // 小于 UINT16_MAX 时, 调用宏获取节点数
    if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) {
        len = intrev16ifbe(ZIPLIST_LENGTH(zl));

    // 大于 UINT16_MAX, 需要迭代计算节点数
    } else {
        unsigned char *p = zl + ZIPLIST_HEADER_SIZE;
        while (*p != ZIP_END) {
            p += zipRawEntryLength(p);
            len++;
        }

        // 如果节点数小于 UINT16_MAX , 更新节计数器
        if (len < UINT16_MAX) ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
    }

    // 返回节点数
    return len;
}

/**
 * 返回压缩列表占用内存字节数
 */
size_t ziplistBlobLen(unsigned char *zl) {
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}

// #ifdef ZIPLIST_TEST_MAIN
/*--------------------- debug --------------------*/
#include <sys/time.h>
#include <assert.h>
#include "adlist.h"
#include "sds.h"

unsigned char *createList() {
    unsigned char *zl = ziplistNew();
    zl = ziplistPush(zl, (unsigned char*)"foo", 3, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"quux", 4, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"hello", 5, ZIPLIST_HEAD);
    zl = ziplistPush(zl, (unsigned char*)"1024", 4, ZIPLIST_TAIL);
    return zl;
}

unsigned char *createIntList() {
    unsigned char *zl = ziplistNew();
    char buf[32];

    sprintf(buf, "100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "128000");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "-100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "4294967296");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "much much longer non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    return zl;
}

long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

void stress(int pos, int num, int maxsize, int dnum) {
    int i,j,k;
    unsigned char *zl;
    char posstr[2][5] = { "HEAD", "TAIL" };
    long long start;
    for (i = 0; i < maxsize; i+=dnum) {
        zl = ziplistNew();
        for (j = 0; j < i; j++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,ZIPLIST_TAIL);
        }

        /* Do num times a push+pop from pos */
        start = usec();
        for (k = 0; k < num; k++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,pos);
            zl = ziplistDeleteRange(zl,0,1);
        }
        printf("List size: %8d, bytes: %8d, %dx push+pop (%s): %6lld usec\n",
            i,intrev32ifbe(ZIPLIST_BYTES(zl)),num,posstr[pos],usec()-start);
        zfree(zl);
    }
}

void pop(unsigned char *zl, int where) {
    unsigned char *p, *vstr;
    unsigned int vlen;
    long long vlong;

    p = ziplistIndex(zl,where == ZIPLIST_HEAD ? 0 : -1);
    if (ziplistGet(p,&vstr,&vlen,&vlong)) {
        if (where == ZIPLIST_HEAD)
            printf("Pop head: ");
        else
            printf("Pop tail: ");

        if (vstr)
            if (vlen && fwrite(vstr,vlen,1,stdout) == 0) perror("fwrite");
        else
            printf("%lld", vlong);

        printf("\n");
        ziplistDeleteRange(zl,-1,1);
    } else {
        printf("ERROR: Could not pop\n");
        exit(1);
    }
}

int randstring(char *target, unsigned int min, unsigned int max) {
    int p = 0;
    int len = min+rand()%(max-min+1);
    int minval, maxval;
    switch(rand() % 3) {
    case 0:
        minval = 0;
        maxval = 255;
    break;
    case 1:
        minval = 48;
        maxval = 122;
    break;
    case 2:
        minval = 48;
        maxval = 52;
    break;
    default:
        assert(NULL);
    }

    while(p < len)
        target[p++] = minval+rand()%(maxval-minval+1);
    return len;
}

void verify(unsigned char *zl, zlentry *e) {
    int i;
    int len = ziplistLen(zl);
    zlentry _e;

    for (i = 0; i < len; i++) {
        memset(&e[i], 0, sizeof(zlentry));
        e[i] = zipEntry(ziplistIndex(zl, i));

        memset(&_e, 0, sizeof(zlentry));
        _e = zipEntry(ziplistIndex(zl, -len+i));

        assert(memcmp(&e[i], &_e, sizeof(zlentry)) == 0);
    }
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

    printf("sizeof(1)=%d\n",sizeof(1));
    printf("sizeof(32760)=%d\n",sizeof(32760));
    printf("sizeof(2147483640)=%d\n",sizeof(2147483640));
    printf("int size: %lu byte\n",sizeof(int));
    printf("short size: %lu byte\n",sizeof(short int));
    printf("long size: %lu byte\n",sizeof(long int));
    printf("long long size: %lu byte\n",sizeof(long long int));
    // printf("sizeof(1)=%d\n",sizeof(1));
    exit(0);

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

    zl = createList();
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_HEAD);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    printf("Get element at index 3:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 3);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index 3\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
    }

    printf("Get element at index 4 (out of range):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 4);
        if (p == NULL) {
            printf("No entry\n");
        } else {
            printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
            return 1;
        }
        printf("\n");
    }

    printf("Get element at index -1 (last element):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index -1\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
    }

    printf("Get element at index -4 (first element):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -4);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("ERROR: Could not access index -4\n");
            return 1;
        }
        if (entry) {
            if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            printf("\n");
        } else {
            printf("%lld\n", value);
        }
        printf("\n");
    }

    printf("Get element at index -5 (reverse out of range):\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -5);
        if (p == NULL) {
            printf("No entry\n");
        } else {
            printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
            return 1;
        }
        printf("\n");
    }

    printf("Iterate list from 0 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 0);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate list from 1 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate list from 2 to end:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 2);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistNext(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate starting out of range:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, 4);
        if (!ziplistGet(p, &entry, &elen, &value)) {
            printf("No entry\n");
        } else {
            printf("ERROR\n");
        }
        printf("\n");
    }

    printf("Iterate from back to front:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            p = ziplistPrev(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Iterate from back to front, deleting all items:\n");
    {
        zl = createList();
        p = ziplistIndex(zl, -1);
        while (ziplistGet(p, &entry, &elen, &value)) {
            printf("Entry: ");
            if (entry) {
                if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
            } else {
                printf("%lld", value);
            }
            zl = ziplistDelete(zl,&p);
            p = ziplistPrev(zl,p);
            printf("\n");
        }
        printf("\n");
    }

    printf("Delete inclusive range 0,0:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 0, 1);
        ziplistRepr(zl);
    }

    printf("Delete inclusive range 0,1:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 0, 2);
        ziplistRepr(zl);
    }

    printf("Delete inclusive range 1,2:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 1, 2);
        ziplistRepr(zl);
    }

    printf("Delete with start index out of range:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 5, 1);
        ziplistRepr(zl);
    }

    printf("Delete with num overflow:\n");
    {
        zl = createList();
        zl = ziplistDeleteRange(zl, 1, 5);
        ziplistRepr(zl);
    }

    printf("Delete foo while iterating:\n");
    {
        zl = createList();
        p = ziplistIndex(zl,0);
        while (ziplistGet(p,&entry,&elen,&value)) {
            if (entry && strncmp("foo",(char*)entry,elen) == 0) {
                printf("Delete foo\n");
                zl = ziplistDelete(zl,&p);
            } else {
                printf("Entry: ");
                if (entry) {
                    if (elen && fwrite(entry,elen,1,stdout) == 0)
                        perror("fwrite");
                } else {
                    printf("%lld",value);
                }
                p = ziplistNext(zl,p);
                printf("\n");
            }
        }
        printf("\n");
        ziplistRepr(zl);
    }

    printf("Regression test for >255 byte strings:\n");
    {
        char v1[257],v2[257];
        memset(v1,'x',256);
        memset(v2,'y',256);
        zl = ziplistNew();
        zl = ziplistPush(zl,(unsigned char*)v1,strlen(v1),ZIPLIST_TAIL);
        zl = ziplistPush(zl,(unsigned char*)v2,strlen(v2),ZIPLIST_TAIL);

        /* Pop values again and compare their value. */
        p = ziplistIndex(zl,0);
        assert(ziplistGet(p,&entry,&elen,&value));
        assert(strncmp(v1,(char*)entry,elen) == 0);
        p = ziplistIndex(zl,1);
        assert(ziplistGet(p,&entry,&elen,&value));
        assert(strncmp(v2,(char*)entry,elen) == 0);
        printf("SUCCESS\n\n");
    }

    printf("Regression test deleting next to last entries:\n");
    {
        char v[3][257];
        zlentry e[3];
        int i;

        for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
            memset(v[i], 'a' + i, sizeof(v[0]));
        }

        v[0][256] = '\0';
        v[1][  1] = '\0';
        v[2][256] = '\0';

        zl = ziplistNew();
        for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
            zl = ziplistPush(zl, (unsigned char *) v[i], strlen(v[i]), ZIPLIST_TAIL);
        }

        verify(zl, e);

        assert(e[0].prevrawlensize == 1);
        assert(e[1].prevrawlensize == 5);
        assert(e[2].prevrawlensize == 1);

        /* Deleting entry 1 will increase `prevrawlensize` for entry 2 */
        unsigned char *p = e[1].p;
        zl = ziplistDelete(zl, &p);

        verify(zl, e);

        assert(e[0].prevrawlensize == 1);
        assert(e[1].prevrawlensize == 5);

        printf("SUCCESS\n\n");
    }

    printf("Create long list and check indices:\n");
    {
        zl = ziplistNew();
        char buf[32];
        int i,len;
        for (i = 0; i < 1000; i++) {
            len = sprintf(buf,"%d",i);
            zl = ziplistPush(zl,(unsigned char*)buf,len,ZIPLIST_TAIL);
        }
        for (i = 0; i < 1000; i++) {
            p = ziplistIndex(zl,i);
            assert(ziplistGet(p,NULL,NULL,&value));
            assert(i == value);
            p = ziplistIndex(zl,-i-1);
            assert(ziplistGet(p,NULL,NULL,&value));
            assert(999-i == value);
        }
        printf("SUCCESS\n\n");
    }

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
// #endif