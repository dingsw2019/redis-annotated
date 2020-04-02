#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"
#include <sys/time.h>

#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/*--------------------- private --------------------*/
// 获取 v 的编码方式
static uint8_t _intsetValueEncoding(int64_t v) {

    if (v < INT32_MIN || v > INT32_MAX) {
        return INTSET_ENC_INT64;
    } else if (v < INT16_MIN || v > INT16_MAX) {
        return INTSET_ENC_INT32;
    } else {
        return INTSET_ENC_INT16;
    }
}

// 通过 enc 编码方式获取 pos 位置上的值
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc) {
    
    int64_t v64;
    int32_t v32;
    int16_t v16;
    
    if (enc == INTSET_ENC_INT16) {
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    } else if (enc == INTSET_ENC_INT32) {
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    } else {
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    }
}

// 获取 pos 索引位置的值
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is, pos, intrev32ifbe(is->encoding));
}

// 将 value 添加到 pos 索引位
static void _intsetSet(intset *is, int pos, int64_t value) {

    uint8_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT16) {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    }
}

// 重分配内存空间
static intset *intsetResize(intset *is, uint32_t len) {

    // 计算 content 所需字节数
    uint32_t size = len * intrev32ifbe(is->encoding);

    return zrealloc(is,sizeof(intset)+size);
}

// 在集合中搜索 value , 找到返回 1, 否则返回 0
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {

    int max = intrev32ifbe(is->length)-1, min=0, mid=-1;
    int64_t cur = -1;
    // 空集合不处理
    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;
    
    // 越界检查
    } else {

        if (value > _intsetGet(is, intrev32ifbe(is->length)-1)) {
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        } else if (value < _intsetGet(is, 0)) {
            if (pos) *pos = 0;
            return 0;
        }
    }

    // 二分查找
    while (max >= min) {
        mid = (max + min) / 2;
        cur = _intsetGet(is, mid);
        
        if (value > cur) {
            min = mid+1;
        } else if (value < cur){
            max = mid-1;
        } else {
            break;
        }
    }

    if (cur == value) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}


// 升级编码方式同时添加value到集合数组
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) {

    uint8_t curenc, valenc;
    int prepare, length = intrev32ifbe(is->length);

    // 当前编码
    curenc = intrev32ifbe(is->encoding);

    // 新编码
    valenc = _intsetValueEncoding(value);

    is->encoding = valenc;
    // 判断头部或尾部添加
    prepare = (value < 0) ? 1 : 0;

    // 按新编码执行扩容
    is = intsetResize(is, intrev32ifbe(is->length)+1);

    // 移动原数据
    while(length--)
        _intsetSet(is, length+prepare, _intsetGetEncoded(is, length, curenc));

    // 写入 value
    if (prepare) {

        _intsetSet(is, 0, value);
    } else {
        _intsetSet(is, intrev32ifbe(is->length), value);
    }

    // 更新节点数
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

// 将 from 开始的内容移动到 to 索引上
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) {

    void *src, *dst;

    // 计算移动的元素数量
    uint32_t bytes = intrev32ifbe(is->length) - from;

    // 当前编码
    uint8_t encoding = intrev32ifbe(is->encoding);

    // 起始,结束指针和移动字节数
    if (encoding == INTSET_ENC_INT16) {
        src = ((int16_t*)is->contents)+from;
        dst = ((int16_t*)is->contents)+to;
        bytes *= sizeof(int16_t);
    } else if (encoding == INTSET_ENC_INT32) {
        src = ((int32_t*)is->contents)+from;
        dst = ((int32_t*)is->contents)+to;
        bytes *= sizeof(int32_t);
    } else {
        src = ((int64_t*)is->contents)+from;
        dst = ((int64_t*)is->contents)+to;
        bytes *= sizeof(int64_t);
    }

    memmove(dst, src, bytes);
}

/*--------------------- API --------------------*/
// 创建并返回一个空集合
intset *intsetNew(void) {

    intset *is = zmalloc(sizeof(*is));

    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;

    return is;
}

// 集合添加新元素, 添加成功 success 写 1, 否则写0
// 返回集合
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {

    uint32_t pos;
    // value 的编码方式
    uint8_t valenc = _intsetValueEncoding(value);

    if (success) *success = 1;

    // 升级编码同时添加元素
    if (valenc > is->encoding) {
        return intsetUpgradeAndAdd(is, value);

    // 只添加元素
    } else {
        
        // 元素已存在, 返回
        if (intsetSearch(is, value, &pos)) {
            if (success) *success = 0;
            return is;
        }

        // 扩容
        is = intsetResize(is, intrev32ifbe(is->length)+1);

        // 移动元素
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);
    }

    // 添加元素
    _intsetSet(is, pos, value);

    // 更新节点数
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

// 查找 value 是否存在集合中
// 存在返回 1, 否则返回 0
uint8_t intsetFind(intset *is, int64_t value) {

    // 编码方式大于当前, 不查找
    uint8_t valenc = _intsetValueEncoding(value);

    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, NULL);
}

// 从集合中删除 value
// 成功 success 为 1, 否则为 0
intset *intsetRemove(intset *is, int64_t value, int *success) {

    uint32_t pos;
    uint8_t valenc = _intsetValueEncoding(value);

    if (success) *success = 0;

    // 搜索 value 的位置
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, &pos)) {

        uint32_t len = intrev32ifbe(is->length);

        if (success) *success = 1;

        // 移动数据, 覆盖掉删除的元素位置
        if (pos < len-1)
            intsetMoveTail(is,pos+1,pos);

        // 缩容
        intsetResize(is,len-1);

        // 更新元素计数器
        is->length = intrev32ifbe(len-1);
    }

    return is;
}

// 取出指定索引上的值, 并存入 value
// 取出返回 1, 否则返回 0
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value) {

    if (pos < intrev32ifbe(is->length)) {

        *value = _intsetGet(is, pos);
        return 1;
    }
    
    return 0;
}

/**
 * 从整数集合中随机返回一个元素
 */
int64_t intsetRandom(intset *is) {

    return _intsetGet(is, rand() % intrev32ifbe(is->length));
}

// 总元素数量
uint32_t intsetLen(intset *is) {
    return intrev32ifbe(is->length);
}

// 集合总字节数
uint32_t intsetBlobLen(intset *is) {

    return sizeof(intset)+intrev32ifbe(is->length)*intrev32ifbe(is->encoding);
}

/*--------------------- debug --------------------*/
void intsetRepr(intset *is) {
    int i;
    for (i=0; i<intrev32ifbe(is->length); i++) {
        printf("%lld\n",(uint64_t)_intsetGet(is,i));
    }
    printf("\n");
}

void error(char *err){
    printf("%s\n",err);
    exit(1);
}

void ok(void){
    printf("OK\n");
}

intset *createSet(int bits, int size) {
    uint64_t mask = (1<<bits)-1;
    uint64_t i, value;
    intset *is = intsetNew();

    for (i = 0; i < size; i++) {
        if (bits > 32) {
            value = (rand()*rand()) & mask;
        } else {
            value = rand() & mask;
        }
        is = intsetAdd(is,value,NULL);
    }
    return is;
}

long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

#define assert(_e) ((_e)?(void)0:(_assert(#_e,__FILE__,__LINE__),exit(1)))
void _assert(char *estr, char *file, int line) {
    printf("\n\n=== ASSERTION FAILED ===\n");
    printf("==> %s:%d '%s' is not true\n",file,line,estr);
}

/**
 * 检查元素值是否按顺序排列
 */
void checkConsistency(intset *is) {
    int i;

    for (i = 0; i < (intrev32ifbe(is->length)-1); i++) {
        uint32_t encoding = intrev32ifbe(is->encoding);

        if (encoding == INTSET_ENC_INT16) {
            int16_t *i16 = (int16_t*)is->contents;
            assert(i16[i] < i16[i+1]);
        } else if (encoding == INTSET_ENC_INT32) {
            int32_t *i32 = (int32_t*)is->contents;
            assert(i32[i] < i32[i+1]);
        } else {
            int64_t *i64 = (int64_t*)is->contents;
            assert(i64[i] < i64[i+1]);
        }
    }
}

// gcc -g zmalloc.c intset.c -D INTSET_TEST_MAIN
int main(void) {

    uint8_t success;
    int i;
    intset *is;
    srand(time(NULL));

    // 确认编码范围
    // printf("Value encodings: "); {
    //     assert(_intsetValueEncoding(-32768) == INTSET_ENC_INT16);
    //     assert(_intsetValueEncoding(+32767) == INTSET_ENC_INT16);
    //     assert(_intsetValueEncoding(-32769) == INTSET_ENC_INT32);
    //     assert(_intsetValueEncoding(+32768) == INTSET_ENC_INT32);
    //     assert(_intsetValueEncoding(-2147483648) == INTSET_ENC_INT32);
    //     assert(_intsetValueEncoding(+2147483647) == INTSET_ENC_INT32);
    //     assert(_intsetValueEncoding(-2147483649) == INTSET_ENC_INT64);
    //     assert(_intsetValueEncoding(+2147483648) == INTSET_ENC_INT64);
    //     assert(_intsetValueEncoding(-9223372036854775808ull) == INTSET_ENC_INT64);
    //     assert(_intsetValueEncoding(+9223372036854775807ull) == INTSET_ENC_INT64);
    //     ok();
    // }

    // 添加相同编码长度的值
    // printf("Basic adding: "); {
    //     is = intsetNew();
    //     is = intsetAdd(is,5,&success); assert(success);
    //     is = intsetAdd(is,6,&success); assert(success);
    //     is = intsetAdd(is,4,&success); assert(success);
    //     is = intsetAdd(is,4,&success); assert(!success);
    //     ok();
    // }

    // 16编码方式下,随机添加 1024 次
    // 检查元素值是否按顺序排列
    // printf("Large number of random adds: "); {
    //     int inserts = 0;
    //     is = intsetNew();
    //     for (i = 0; i < 1024; i++) {
    //         is = intsetAdd(is,rand()%0x800,&success);
    //         if (success) inserts++;
    //     }
    //     assert(intrev32ifbe(is->length) == inserts);
    //     checkConsistency(is);
    //     ok();
    // }

    // 升级编码方式(16 升级 32), 测试添加,查找是否正常
    // printf("Upgrade from int16 to int32: "); {
    //     is = intsetNew();
    //     is = intsetAdd(is,32,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
    //     is = intsetAdd(is,65535,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
    //     assert(intsetFind(is,32));
    //     assert(intsetFind(is,65535));
    //     checkConsistency(is);

    //     is = intsetNew();
    //     is = intsetAdd(is,32,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
    //     is = intsetAdd(is,-65535,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
    //     assert(intsetFind(is,32));
    //     assert(intsetFind(is,-65535));
    //     checkConsistency(is);
    //     ok();
    // }

    // 升级编码方式(16 升级 64), 测试添加,查找是否正常
    // printf("Upgrade from int16 to int64: "); {
    //     is = intsetNew();
    //     is = intsetAdd(is,32,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
    //     is = intsetAdd(is,4294967295,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
    //     assert(intsetFind(is,32));
    //     assert(intsetFind(is,4294967295));
    //     checkConsistency(is);

    //     is = intsetNew();
    //     is = intsetAdd(is,32,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
    //     is = intsetAdd(is,-4294967295,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
    //     assert(intsetFind(is,32));
    //     assert(intsetFind(is,-4294967295));
    //     checkConsistency(is);
    //     ok();
    // }

    // 升级编码方式(32 升级 64), 测试添加,查找是否正常
    // printf("Upgrade from int32 to int64: "); {
    //     is = intsetNew();
    //     is = intsetAdd(is,65535,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
    //     is = intsetAdd(is,4294967295,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
    //     assert(intsetFind(is,65535));
    //     assert(intsetFind(is,4294967295));
    //     checkConsistency(is);

    //     is = intsetNew();
    //     is = intsetAdd(is,65535,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
    //     is = intsetAdd(is,-4294967295,NULL);
    //     assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
    //     assert(intsetFind(is,65535));
    //     assert(intsetFind(is,-4294967295));
    //     checkConsistency(is);
    //     ok();
    // }

    // printf("Stress lookups: "); {
    //     long num = 100000, size = 10000;
    //     int i, bits = 20;
    //     long long start;
    //     is = createSet(bits,size);
    //     checkConsistency(is);

    //     start = usec();
    //     for (i = 0; i < num; i++) intsetSearch(is,rand() % ((1<<bits)-1),NULL);
    //     printf("%ld lookups, %ld element set, %lldusec\n",num,size,usec()-start);
    // }

    // 添加和删除的压力测试
    // printf("Stress add+delete: "); {
    //     int i, v1, v2;
    //     is = intsetNew();
    //     for (i = 0; i < 0xffff; i++) {
    //         v1 = rand() % 0xfff;
    //         is = intsetAdd(is,v1,NULL);
    //         assert(intsetFind(is,v1));

    //         v2 = rand() % 0xfff;
    //         is = intsetRemove(is,v2,NULL);
    //         assert(!intsetFind(is,v2));
    //     }
    //     checkConsistency(is);
    //     ok();
    // }

    return 0;
}