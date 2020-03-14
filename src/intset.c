#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"
#include <assert.h>

/**
 * 编码方式
 */
#define INTSET_ENC_INT16 (sizeof(int64_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/**
 * 返回适用于 传入值(v) 的编码方式, 闭区间
 * ( ( (16 编码范围) 32 编码范围) 64 编码范围)  包含关系
 * T = O(1)
 */
static uint8_t _intsetValueEncoding(int64_t v) {

    if (v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if (v < INT16_MIN || v > INT16_MAX)
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16;
}

/**
 * 创建并返回一个空整数结合
 * T = O(1)
 */
intset *intsetNew(void){

    // 为整数集合分配内存空间
    intset *is = zmalloc(sizeof(*is));

    // 设置初始编码
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);

    // 初始化元素数量
    is->length = 0;

    return is;
}

// #ifdef INTSET_TEST_MAIN

/*---------------------  --------------------*/
/*--------------------- debug --------------------*/
void error(char *err){
    printf("%s\n",err);
    exit(1);
}

void ok(void){
    printf("OK\n");
}

// gcc -g zmalloc.c intset.c -D INTSET_TEST_MAIN
int main(void)
{
    intset *is;

    // 确认编码范围
    printf("Value encodings: "); {
        assert(_intsetValueEncoding(-32768) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(+32767) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(-32769) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+32768) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483648) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+2147483647) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483649) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+2147483648) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(-9223372036854775808ull) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+9223372036854775807ull) == INTSET_ENC_INT64);
        ok();
    }

    printf("Basic adding: "); {
        is = intsetNew();
        // is = intsetAdd(is,5,&success); assert(success);
        // is = intsetAdd(is,6,&success); assert(success);
        // is = intsetAdd(is,4,&success); assert(success);
        // is = intsetAdd(is,4,&success); assert(!success);
        ok();
    }

    return 0;
}

// #endif
