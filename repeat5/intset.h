#ifndef __INTSET_H
#define __INTSET_H

#include <stdint.h>

typedef struct intset {
    // 编码方式
    uint32_t encoding;
    // 元素数量
    uint32_t lenght;
    // 底层数组顺序存放元素
    int8_t contents[];
} intset;

#endif 