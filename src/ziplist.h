#ifndef __ZIPLIST_H
#define __ZIPLIST_H

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

#endif