#ifndef __ZIPLIST_H
#define __ZIPLIST_H

// 迭代方向
#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

#define ZIP_END 255
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

#define ZIP_INT_IMM_MASK 0x0f
#define ZIP_INT_IMM_MIN 0xf1
#define ZIP_INT_IMM_MAX 0xfd

#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)


#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

// 定位到 ziplist 的 bytes 属性，该属性记录了整个 ziplist 所占用的内存字节数
// 用于取出 bytes 属性的现有值，或者为 bytes 属性赋予新值
#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))
// 定位到 ziplist 的 offset 属性，该属性记录了到达表尾节点的偏移量
// 用于取出 offset 属性的现有值，或者为 offset 属性赋予新值
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// 定位到 ziplist 的 length 属性，该属性记录了 ziplist 包含的节点数量
// 用于取出 length 属性的现有值，或者为 length 属性赋予新值
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
// 返回 ziplist 表头的大小
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))
// 返回指向 ziplist 第一个节点（的起始位置）的指针
#define ZIPLIST_ENTRY_HEAD(zl)  ((zl)+ZIPLIST_HEADER_SIZE)
// 返回指向 ziplist 最后一个节点（的起始位置）的指针
#define ZIPLIST_ENTRY_TAIL(zl)  ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
// 返回指向 ziplist 末端 ZIP_END （的起始位置）的指针
#define ZIPLIST_ENTRY_END(zl)   ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

// myerr
// #define ZIPLIST_INCR_LENGTH(zl,incr) intrev32ifbe(intrev32ifbe(ZIPLIST_LENGTH(zl))+incr)
#define ZIPLIST_INCR_LENGTH(zl,incr) {  \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX)    \
        ZIPLIST_LENGTH(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_LENGTH(zl))+incr);    \
}
    

typedef struct zlentry {

    // prevrawlensize : 存储"前置节点长度"所用字节数
    // prevrawlen : 前置节点长度
    unsigned int prevrawlensize, prevrawlen;

    // lensize : 存储"当前节点长度"所用字节数
    // len : 当前节点长度
    unsigned int lensize, len;

    // 节点头部长度(字节数)
    unsigned int headersize;

    // 节点值的编码方式
    unsigned char encoding;

    // 指向节点值的指针
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