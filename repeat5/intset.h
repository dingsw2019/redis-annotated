#ifndef __INTSET_H
#define __INTSET_H

#include <stdint.h>

typedef struct intset
{
    // 编码方式
    uint32_t encoding;
    // 元素数量
    uint32_t length;
    // 存放元素数组
    int8_t contents[];
} intset;


#endif