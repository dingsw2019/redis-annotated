#ifndef __ZIPLIST_H
#define __ZIPLIST_H

#include <stdint.h>

#define ZIP_END 255
#define ZIP_BIGLEN 254

// 字符串编码的掩码
#define ZIP_STR_MASK 0xc0

// 字符串编码类型
// #define ZIP_STR_06B (0x3f | 0<<4) myerr
// #define ZIP_STR_14B (0x3f | 1<<4)
// #define ZIP_STR_32B (0x3f | 2<<4)
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)

// 整数编码类型
#define ZIP_INT_8B 0xfe
#define ZIP_INT_16B (0xc0 | 0<<4)
#define ZIP_INT_32B (0xc0 | 1<<4)
#define ZIP_INT_64B (0xc0 | 2<<4)
#define ZIP_INT_24B (0xc0 | 3<<4)

// 4 位整数编码和掩码的类型
#define ZIP_INT_IMM_MIN 0xf1
#define ZIP_INT_IMM_MAX 0xfd

// 24位整数的值域
#define INT24_MAX 0x7fffff
#define INT24_MIN (-ZIP_INT_IMM_MAX - 1)

// 是否字符串类型编码
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

// 列表总字节数
// #define ZIPLIST_BYTES(zl) (zl) myerr
#define ZIPLIST_BYTES(zl) (*((uint32_t*)(zl)))
// 获取或设置尾节点偏移量 myerr:缺少
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// 获取或设置节点计数器
#define ZIPLIST_LENGTH(zl) (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
// 获取或设置尾节点
#define ZIPLIST_ENTRY_TAIL(zl) ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))

// 设置节点编码方式 myerr:缺少
#define ZIP_ENTRY_ENCODING(ptr, encoding) do{                  \
    (encoding) = (ptr)[0];                                     \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK; \
}while(0)

// 压缩列表节点结构
typedef struct zlentry {

    // prevlensize : 编码"前置节点长度"
    // prevlen : 前置节点长度
    unsigned int prevlensize, prevlen;

    // lensize : 编码"当前节点长度"所需字节数
    // len : 当前节点长度
    unsigned int lensize, len;

    // 节点值的编码类型
    unsigned char encoding;

    // 当前节点头部长度(字节数)
    unsigned int headersize;

    // 指向节点值的指针
    unsigned char *p;

} zlentry;

#endif