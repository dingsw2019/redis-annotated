#ifndef __INTSET_H
#define __INTSET_H

#include <stdint.h>

typedef struct intset {
    // 编码方式
    uint32_t encoding;

    // 元素数量
    uint32_t length;

    // 底层数组
    // uint8_t contents[]; myerr
    int8_t contents[];
} intset;

#endif