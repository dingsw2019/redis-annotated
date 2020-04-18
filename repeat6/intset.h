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

intset *intsetNew(void);
intset *intsetAdd(intset *is, int64_t value, uint8_t *success);
intset *intsetRemove(intset *is, int64_t value, int *success);
uint8_t intsetFind(intset *is, int64_t value);
int64_t intsetRandom(intset *is);
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value);
uint32_t intsetLen(intset *is);
size_t intsetBlobLen(intset *is);

#endif