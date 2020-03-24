#ifndef __ZIPLIST_H
#define __ZIPLIST_H

// 末端标识符和前置节点1字节最大值
#define ZIP_END 255
#define ZIP_BIGLEN 254

// 字符串编码
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)

// 整数编码
#define ZIP_INT_16B (0xc0 | 0 << 4)
#define ZIP_INT_32B (0xc0 | 1 << 4)
#define ZIP_INT_64B (0xc0 | 2 << 4)
#define ZIP_INT_24B (0xc0 | 3 << 4)
#define ZIP_INT_8B 0xfe

// 24位的取值范围和掩码
#define ZIP_INT_IMM_MAX 0x7fffff
#define ZIP_INT_IMM_MIN (-ZIP_INT_IMM_MAX - 1)

// 字符串/整数编码的掩码
#define ZIP_STR_MASK 0xc0

// 节点
typedef struct zlentry {

    // 编码前置节点长度所需字节数
    // 前置节点长度
    unsigned int prevrawlensize, prevrawlen;

    // 编码当前节点长度所需字节数
    // 当前节点长度
    unsigned int lensize, len;

    // 编码
    unsigned char encoding;

    // 头部字节数
    unsigned int headersize;

    // 内容指针
    unsigned char *p;
} zlentry;

// 是否字符串编码
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

// 压缩列表总字节数
#define ZIPLIST_BYTES(zl) (*((uint32_t*)(zl)))
// 设置或获取尾节点偏移量
#define ZIPLIST_OFFSET_LENGTH (*((uint32_t*)((zl)+sizeof(uint32_t))))
// 列表头部字节数
#define ZIPLIST_HEADSIZE ((uint32_t)*2+(uint16_t))
// 设置或获取节点数量
#define ZIPLIST_LENGTH(zl) (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
// 第一个节点
#define ZIPLIST_ENTRY_HEAD ((zl)+ZIPLIST_HEADSIZE)
// 最后一个节点
#define ZIPLIST_ENTRY_TAIL ((zl)+ZIPLIST_OFFSET_LENGTH)

// 获取编码
#define ZIP_ENTRY_ENCODING(p) do { \
    (encoding) = p[0];          \
    if (encoding < ZIP_STR_MASK) encoding &= ZIP_STR_MASK;  \
}while(0)

#endif