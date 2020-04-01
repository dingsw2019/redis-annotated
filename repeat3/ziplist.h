#ifndef __ZIPLIST_H
#define __ZIPLIST_H

#include <stdint.h>

// 迭代方向
#define ZIPLIST_HEAD 0 
#define ZIPLIST_TAIL 1

// 末端标识符 和 1 字节最大值
#define ZIP_END 255
#define ZIP_BIGLEN 254

// 字符串掩码
#define ZIP_STR_MASK 0xc0
#define ZIP_INT_MASK 0x30

// 字符串编码类型
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)

// 整型编码类型
#define ZIP_INT_16B (0xc0 | 0<<4)
#define ZIP_INT_32B (0xc0 | 1<<4)
#define ZIP_INT_64B (0xc0 | 2<<4)
#define ZIP_INT_24B (0xc0 | 3<<4)
#define ZIP_INT_8B 0xfe

// 4 位整数的取值范围
#define ZIP_INT_IMM_MIN 0xf1
#define ZIP_INT_IMM_MAX 0xfd
#define ZIP_INT_IMM_MASK 0x0f

// 24 的取值范围
#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX)-1

#define ZIPLIST_BYTES(zl) (*((uint32_t*)(zl)))
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
#define ZIPLIST_LENGTH(zl) (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
#define ZIPLIST_HEADER_SIZE (sizeof(uint32_t)*2+sizeof(uint16_t))
#define ZIPLIST_ENTRY_HEAD(zl) ((zl)+ZIPLIST_HEADER_SIZE)
#define ZIPLIST_ENTRY_TAIL(zl) ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
#define ZIPLIST_ENTRY_END(zl) ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

// 编码是否为字符串型
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

// 增加或减少节点数量
#define ZIPLIST_INCR_LENGTH(zl, incr) do {      \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX)    \
        ZIPLIST_LENGTH(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_LENGTH(zl))+incr); \
}while(0)

// 压缩列表节点
typedef struct zlentry {

    // 前置节点
    unsigned int prevrawlensize, prevrawlen;

    // 当前节点
    unsigned int lensize, len;

    // 编码
    unsigned char encoding;

    // 头部信息字节数
    unsigned int headersize;

    // 节点内容
    unsigned char *p;
} zlentry;


unsigned char *ziplistNew(void);
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where);
unsigned char *ziplistIndex(unsigned char *zl, int index);
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p);
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p);
unsigned int ziplistGet(unsigned char *p, unsigned char **sval, unsigned int *slen, long long *lval);
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p);
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num);
unsigned int ziplistCompare(unsigned char *p, unsigned char *s, unsigned int slen);
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip);
unsigned int ziplistLen(unsigned char *zl);
size_t ziplistBlobLen(unsigned char *zl);

#endif