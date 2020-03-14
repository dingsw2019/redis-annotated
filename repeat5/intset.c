#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"
#include <assert.h>

// 编码方式
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/*----------------- private -----------------*/
// 计算编码方式所需空间大小
static uint8_t _intsetValueEncoding(int64_t v) {
    
    // 不同类型,不同长度
    if (v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if (v < INT16_MIN || v > INT16_MAX)
        return INTSET_ENC_INT32;
    else 
        return INTSET_ENC_INT16;
}

// 向集合指定索引位添加值
static void _intsetSet(intset *is,int pos, int64_t value) {

    // int8_t enc = intrev32ifbe(is->encoding); myerr
    uint32_t enc = intrev32ifbe(is->encoding);

    if (enc == INTSET_ENC_INT64) {
        ((int64_t*)is->contents)[pos] = value;
        // memrev64ifbe(&((int64_t*)is->contents)+pos); myerr
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (enc == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        // memrev64ifbe(&((int32_t*)is->contents)+pos);
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        // memrev64ifbe(&((int16_t*)is->contents)+pos);
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

// 指定编码的方式根据索引获取值
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc){

    int64_t v64;
    int32_t v32;
    int16_t v16;

    if (enc == INTSET_ENC_INT64) {
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    } else if (enc == INTSET_ENC_INT32) {
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev64ifbe(&v32);
        return v32;
    } else {
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev64ifbe(&v16);
        return v16;
    }
}

// 返回集合中指定索引的值
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

// 查找 value 是否存在于集合数组中
// 存在, 返回 1, pos 给 value 所在索引位置
// 不存在, 返回 0 , pos 给适合插入的索引位置
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {

    int min = 0, max = intrev32ifbe(is->length)-1, mid = -1;
    int64_t cur = -1;

    // 空集合, 直接返回
    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;
    }
    // 非空
    else {

        // 边界检查
        if (value > _intsetGet(is,intrev32ifbe(is->length)-1)) {
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        } else if (value < _intsetGet(is,0)) {
            if (pos) *pos = 0;
            return 0;
        }
    }


    // 二分查找
    while (max >= min) {
        mid = (max+min)/2;
        cur = _intsetGet(is,mid);
        if (value < cur) {
            max = mid - 1;
        } else if (value > cur) {
            min = mid + 1;
        } else break;
    }

    // 是否存在
    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}


/*----------------- API -----------------*/

// 创建并返回一个空集合
intset *intsetNew(void) {

    // 申请内存空间
    intset *is = zmalloc(sizeof(intset));
    // 初始化属性
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;

    return is;
}

// 将 value 添加到集合数组
// 如果 value 已存在于集合, success=0, 不更改整数集合
// 如果 value 不存在, success=1, 并将 value 添加集合数组中
// intset *intsetAdd(intset *is, int64_t value, uint32_t *success) {
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {

    // 计算 value 的编码方式所需空间
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    if (success) *success = 1;

    // value 编码所需空间大于当前空间,进行升级
    if (valenc > intrev32ifbe(is->encoding)) {

    } else {

        // 走到这里,说明编码空间满足 value

        // 查找 value 是否存在于数组中
        // 如果存在 返回
        if (intsetSearch(is,value,&pos)) {
            if (success) *success = 0;
            return is;
        }

        // 扩容

    }

    // 添加到 value 到集合
    _intsetSet(is,pos,value);

    // 更新元素数量
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

/*----------------- debug -----------------*/
void ok(void){
    printf("OK\n");
}

// gcc -g zmalloc.c intset.c -D INTSET_TEST_MAIN
int main(void)
{
    uint8_t success;
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
        is = intsetAdd(is,6,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(!success);
        ok();
    }

    return 0;
}

// #endif
