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
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/*--------------------- private --------------------*/

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
 * 根据给定的编码方式 enc, 返回集合的底层数组在 pos 索引上的元素
 * 
 * T = O(1)
 */
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc){
    int64_t v64;
    int32_t v32;
    int16_t v16;

    // 每种编码方式单个元素的占位长度不同, 计算跳过的字节数
    // memrevEncifbe(&vEnc) 会对拷贝出的字节进行大小端转换
    if (enc == INTSET_ENC_INT64) {
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    } else if (enc == INTSET_ENC_INT32) {
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    } else {
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}

/**
 * 根据集合的编码方式, 返回底层数组在 pos 索引上的值
 * 
 * T = O(1)
 */
static int64_t _intsetGet(intset *is, int pos){
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

/**
 * 根据集合的编码方式, 将底层数组在 pos 位置上的值设为 value
 * 
 * T = O(1)
 */
static void _intsetSet(intset *is, int pos, int64_t value){

    // 获取集合的编码方式
    uint32_t encoding = intrev32ifbe(is->encoding);

    // 不同编码方式单元素大小不同,索引根据编码方式
    // 跳转到索引 pos 指定位置,添加 value
    // 如果有需要的话,memrevEncifbe 进行大小端转换
    if (encoding == INTSET_ENC_INT64) {
        ((uint64_t*)is->contents)[pos] = value;
        memrev64ifbe(((uint64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((uint32_t*)is->contents)[pos] = value;
        memrev32ifbe(((uint64_t*)is->contents)+pos);
    } else {
        ((uint16_t*)is->contents)[pos] = value;
        memrev16ifbe(((uint16_t*)is->contents)+pos);
    }
}

/**
 * 在集合 is 的底层数组中查找值 value 所在的索引
 * 
 * 找到 value 时, 返回 1, 并将 *pos 的值设为 value 所在的索引
 * 
 * 未找到 value 时, 返回 0, 并将 *pos 的值设为 value 可以插入到数组的位置
 * 
 * T = O(long N)
 */
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos)
{
    int min = 0, max = intrev32ifbe(is->length)-1, mid = -1;
    int64_t cur = -1;

    // 整数集合为空
    if (intrev32ifbe(is->length) == 0) {
       if (pos) *pos = 0;
        return 0;
    }
    // 整数集合不为空,优先判断边界值
    else {
        // value 大于最大边界
        if (value > _intsetGet(is,intrev32ifbe(is->length)-1)) {
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        }
        // value 小于最小边界
        else if (value < _intsetGet(is,0)) {
            if (pos) *pos = 0;
            return 0;
        }
    }

    // 在有序数组中进行二分查找
    // T = O(log N)
    while (max >= min) {
        mid = (max + min) / 2;
        cur = _intsetGet(is, mid);
        if (value < cur) {
            max = mid + 1;
        } else if (value > cur) {
            min = mid - 1;
        } else {
            break;
        }
    }

    // 检查是否找到 value
    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}

/*--------------------- API --------------------*/

/**
 * 创建并返回一个空整数结合
 * T = O(1)
 */
intset *intsetNew(void){

    // 为整数集合分配内存空间
    intset *is = zmalloc(sizeof(*is));

    // intrev32ifbe,对于x86架构采用小端序
    // 设置初始编码
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);

    // 初始化元素数量
    is->length = 0;

    return is;
}

/**
 * 尝试将元素 value 添加到整数集合中
 * 
 * *success 的值表示添加是否成功
 * 值为 1, 表示添加成功
 * 值为 0, 表示元素已存在造成的添加失败
 * 
 * T = O(N)
 */
intset *intsetAdd(intset *is, int64_t value, uint8_t *success){

    // 获取 value 的编码凡是
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    // value 编码方式大于当前编码方式
    if (valenc > intrev32ifbe(is->encoding)) {
        
    }
    // 当前编码方式适合 value
    else {
        // 整数集合中查找 value 是否存在
        // 如果存在, 不改动集合元素, success 返回 0
        // 如果不存在, 那么 value 插入到集合的索引保存在 pos 指针中
        if (intsetSearch(is,value,&pos)) {
            if (success) *success = 0;
            return is;
        }
    }

    // 添加 value 到集合
    _intsetSet(is,pos,value);

    // 更新集合的元素数量
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

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
