#ifndef __INTSET_H
#define __INTSET_H

#include <stdint.h>

typedef struct intset 
{
    // 编码方式
    uint32_t encoding;

    // 集合包含的元素数量
    uint32_t length;

    // 保存元素的数组
    int8_t contents[];
} intset;

#endif