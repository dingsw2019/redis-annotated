#ifndef __ZIPLIST_H
#define __ZIPLIST_H

// 迭代方向
#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

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

#endif