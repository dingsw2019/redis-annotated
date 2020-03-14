#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"
#include <assert.h>

// #define INTSET_ENC_INT16 sizeof(uint16_t);
// #define INTSET_ENC_INT32 sizeof(uint32_t);
// #define INTSET_ENC_INT64 sizeof(uint64_t);
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/*--------------------- private --------------------*/
// static uint8_t _intsetValueEncoding(uint64_t value){//myerr
static uint8_t _intsetValueEncoding(int64_t value){
    
    if (value < INT32_MIN || value > INT32_MAX) {
        return INTSET_ENC_INT64;
    } else if (value < INT16_MIN || value > INT16_MAX) {
        return INTSET_ENC_INT32;
    } else {
        return INTSET_ENC_INT16;
    }
}

static int64_t _intsetGetEncoded(intset *is,int pos, uint8_t enc){
    // uint64_t v64;
    // uint32_t v32;
    // uint16_t v16; myerr
    int64_t v64;
    int32_t v32;
    int16_t v16;

    if (enc == INTSET_ENC_INT64) {
        // v64 = ((uint64_t*)is->contents)[pos]; myerr
        // memrev64ifbe(v64);
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    } else if (enc == INTSET_ENC_INT32) {
        // v32 = ((uint32_t*)is->contents)[pos];
        // memrev32ifbe(v32);
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev64ifbe(&v32);
        return v32;
    } else {
        // v16 = ((uint16_t*)is->contents)[pos];
        // memrev16ifbe(v16);
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev64ifbe(&v16);
        return v16;
    }
}

static int64_t _intsetGet(intset *is, int pos){
    // return _intsetGetEncoded(is,pos,_intsetValueEncoding(is->encoding)); myerr
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

static void _intsetSet(intset *is, int pos, int64_t value){

    // uint8_t valenc = _intsetValueEncoding(value); myerr
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) {
        // ((int64_t*)is->contents)[pos] = intrev64ifbe(value);
        // memrev64(&value);
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

// 找到返回 1, pos 返回索引值
// 未找到到返回 0, pos 返回适合插入的索引值
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos){

    int min = 0,max = intrev32ifbe(is->length)-1, mid = -1;
    // int64_t cur; myerr
    int64_t cur = -1;
    // 如果集合为空, 返回 0
    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;

    } else {
        // 优先查找边界
        // if (value > is->contents[intrev32ifbe(is->length)-1]) { myerr
        if (value > _intsetGet(is,intrev32ifbe(is->length)-1)) {
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        // } else if (value < is->contents[0]) {
        } else if (value < _intsetGet(is,0)) {
            if (pos) *pos = 0;
            return 0;
        }
    }

    // 二分查找
    while (max >= min) {

        mid = (max+min)/2;
        // cur = intrev32ifbe(is->contents[mid]); myerr
        cur = _intsetGet(is,mid);
        if (value < cur) {
            max = mid - 1;
        } else if (value > cur) {
            min = mid + 1;
        } else break;
    }

    // 确定是否找到 value
    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}

/*--------------------- API --------------------*/

intset *intsetNew(void){

    intset *is = zmalloc(sizeof(*is));

    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;

    return is;
}

// 添加成功返回, succes 为 1
// 添加失败或者 value 已存在, successs 为 0
// intset *intsetAdd(intset *is, int64_t value, uint8_t success){
intset *intsetAdd(intset *is, int64_t value, uint8_t *success){

    // int64_t valenc; myerr
    // int pos;
    uint8_t valenc;
    uint32_t pos;

    // 计算 value 的编码方式
    valenc = _intsetValueEncoding(value);

    // if (success) success = 1;
    if (success) *success = 1;

    // 整数集合为空, 返回 0     myerr:删除
    // if (intrev32ifbe(is->length) == 0) {
    //     if (success) success = 0;
    //     return is;
    // }

    // 如果 value 的编码方式大于当前集合的编码方式, 升级集合
    if (valenc > intrev32ifbe(is->encoding)){

    } 
    // 整数集合不为空
    else {

        // 查找 value 是否存在
        if (intsetSearch(is,value,&pos)) {
            // if (success) success = 0;
            if (success) *success = 0;
            return is;
        }
    }

    // 添加 value 到集合
    _intsetSet(is,pos,value);

    // 更新整数集合的元素计数器
    // intrev32ifbe(is->length)++;
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    // 返回
    return is;
}

/*--------------------- debug --------------------*/
void ok(void){
    printf("OK\n");
}

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