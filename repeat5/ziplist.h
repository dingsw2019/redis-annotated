#ifndef __ZIPLIST_H
#define __ZIPLIST_H

#include <stdint.h>

#define ZIP_END 255
#define ZIP_BIGLEN 254

// 字符串类型掩码
#define ZIP_STR_MASK 0xc0

// 字符串类型
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)

// 整数类型
#define ZIP_INT_16B (0xc0 | 0 << 4)
#define ZIP_INT_32B (0xc0 | 1 << 4)
#define ZIP_INT_64B (0xc0 | 2 << 4)
#define ZIP_INT_24B (0xc0 | 3 << 4)
#define ZIP_INT_8B 0xfe

// 24 位整数的值域
#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24MAX - 1)

// 4 位整数的值域
#define ZIP_INT_IMM_MIN 0xf1
#define ZIP_INT_IMM_MAX 0xfd

// 列表总长度
#define ZIPLIST_BYTES(zl) (*((uint32_t)(zl)))
// 尾节点偏移量
#define ZIPLIST_TAIL_OFFSET (*((uint32_t)((zl)+sizeof(uint32_t))))
// 节点总数
#define ZIPLIST_LENGTH (*((uint32_t)((zl)+sizeof(uint32_t)*2)))
// 尾节点
#define ZIPLIST_ENTRY_TAIL ((zl)+ZIPLIST_TAIL_OFFSET(zl))

// 是否字符型
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

typedef struct zlentry {

    // 前置节点长度以及存储长度(统称前置节点编码长度)
    unsigned int prevrawlensize, prevrawlen;

    // 当前节点长度及其存储长度(统称编码长度)
    unsigned int lensize, len;

    // 编码类型
    unsigned char encoding;

    // 头部字节数
    unsigned int headersize;

    // 值的指针
    unsigned char *p;
} zlentry;

#endif