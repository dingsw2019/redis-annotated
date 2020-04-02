#ifndef __INTSET_H
#define __INTSET_H

#include <stdint.h>

typedef struct intset {

    uint32_t encoding;
    uint32_t length;
    int8_t contents[];
} intset;

#endif