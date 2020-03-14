#ifndef __INTSET_H
#define __INTSET_H

#include <stdint.h>

typedef struct intset
{
    // // 编码方式 myerr
    // uint64_t encoding;

    // // 元素数量
    // uint64_t length;

    // // 元素数组
    // uint64_t contents[];

    uint32_t encoding;
    uint32_t length;
    int8_t contents[];
} intset;

#endif