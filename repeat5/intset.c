#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"
#include <sys/time.h>

// 编码方式
#define INTSET_ENC_INT64 (sizeof(int64_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT16 (sizeof(int16_t))

/*--------------------- private --------------------*/
// 数值转换成编码方式
static uint8_t _intsetValueEncoding(int64_t v) {
    if (v < INT32_MIN || v > INT32_MAX) {
        return INTSET_ENC_INT64;
    } else if (v < INT16_MIN || v > INT16_MAX) {
        return INTSET_ENC_INT32;
    } else {
        return INTSET_ENC_INT16;
    }
}

// 通过指定编码方式获取指定索引上的值
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc) {
    int64_t v64;
    int32_t v32;
    int16_t v16;

    if (enc == INTSET_ENC_INT64) {
        // memcpy(&v64,intrev32ifbe(is->contents)+pos,sizeof(v64)); myerr
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

// 获取指定索引的值
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

// 给指定索引设置值
static void _intsetSet(intset *is, int pos, int64_t value) {

    // 获取编码方式
    // uint8_t enc = intrev32ifbe(is->encoding); myerr
    uint32_t enc = intrev32ifbe(is->encoding);
    // 添加值
    if (enc == INTSET_ENC_INT64) {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (enc == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

// 按指定元素数量重新分配内存
static intset *intsetResize(intset *is, uint32_t len) {

    // 元素内存大小计算
    uint32_t size = len * intrev32ifbe(is->encoding);

    is = zrealloc(is, sizeof(intset)+size);

    return is;
}

// 查找 value 
// 找到返回 1, 并将索引位写入 pos 指针中
// 未找到返回 0, 并将适合插入的索引位写入 pos 指针中
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {

    int min = 0, max = intrev32ifbe(is->length)-1, mid = -1;
    int64_t cur = 1;
    // 底层数组为空, 不继续查找
    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;
    }
    // 数组非空
    else {

        // 边界查找
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
        mid = (max + min) / 2;
        cur = _intsetGet(is,mid);

        if (value > cur) {
            min = mid+1;
        } else if (value < cur){
            max = mid-1;
        } else {
            break;
        }
    }

    // 验证是否找到
    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}

// 升级编码方式并添加元素
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) {

    // 新元素的编码方式
    uint8_t valenc = _intsetValueEncoding(value);

    // 当前的编码方式
    uint8_t curenc = intrev32ifbe(is->encoding);

    // 向前还是向后添加新元素
    int prepend = (value < 0) ? 1 : 0;

    int length = intrev32ifbe(is->length);

    // 按新编码重新分配内存空间
    is->encoding = valenc;
    is = intsetResize(is,intrev32ifbe(is->length)+1);

    // 移动原有元素
    while (length--)
        _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));

    // 添加新元素
    if (prepend) {
        _intsetSet(is,0,value);
    } else {
        _intsetSet(is,intrev32ifbe(is->length),value);
    }

    // 更新元素计数器
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;

}


// 向前或先后移动元素
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) {

    void *src, *dst;

    // 要移动的元素数量
    uint32_t byte = intrev32ifbe(is->length) - from;

    // 编码方式
    uint8_t enc = intrev32ifbe(is->encoding);

    if (enc == INTSET_ENC_INT64) {
        src = ((int64_t*)is->contents)+from;
        dst = ((int64_t*)is->contents)+to;
        byte *= sizeof(int64_t);
    } else if (enc == INTSET_ENC_INT32) {
        src = ((int32_t*)is->contents)+from;
        dst = ((int32_t*)is->contents)+to;
        byte *= sizeof(int32_t);
    } else {
        src = ((int16_t*)is->contents)+from;
        dst = ((int16_t*)is->contents)+to;
        byte *= sizeof(int16_t);
    }

    memmove(dst,src,byte);
}




/*--------------------- API --------------------*/
// 创建并返回一个空的整数集合
intset *intsetNew(void) {
    // 申请内存空间
    intset *is = zmalloc(sizeof(intset));
    // 初始化属性
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;

    return is;
}

// 添加一个新元素
// 添加成功, 返回 1, success 为 1
// 添加失败或 value 已存在, 返回 0, success 为 0
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {

    uint32_t pos;
    // 计算 value 的编码方式
    int8_t valenc = _intsetValueEncoding(value);

    // 默认成功
    if (success) *success = 1;

    // 当前编码方式不满足 value 的情况
    if (valenc > intrev32ifbe(is->encoding)) {
        // 升级编码方式,并添加 value
        return intsetUpgradeAndAdd(is,value);
    }
    // 当前编码方式满足
    else {

        // 查找 value是否存在于数组中
        if(intsetSearch(is,value,&pos)) {
            if (success) *success = 0;
            return is;
        }

        // 底层数组扩容
        is = intsetResize(is,intrev32ifbe(is->length)+1);

        // 移动原有元素
        // if (pos < intrev32ifbe(is->length)-1) intsetMoveTail(is,pos,pos+1); myerr
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);
    }

    // 添加 value
    _intsetSet(is,pos,value);

    // 更新元素计数器
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

// 查找 value, 找到返回 1, 否则返回 0
uint8_t intsetFind(intset *is, int64_t value) {

    // 计算 value 的编码方式
    uint8_t valenc = _intsetValueEncoding(value);
    // 编码符合后再查找
    return (valenc <= intrev32ifbe(is->encoding)) && intsetSearch(is,value,NULL);
}

// 删除元素值为 value 的元素
// 删除成功 , 返回 1, success 为 1
// 删除失败或未找到 , 返回 0, success 为 0
intset *intsetRemove(intset *is, int64_t value, int *success) {

    // 计算 value 的编码方式
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    // myerr : 缺少
    if (success) *success = 0;

    // 当前编码方式满足 value 再进行查找
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,&pos)) {

        uint32_t len = intrev32ifbe(is->length);
        
        // myerr : 缺少
        if (success) *success = 1;
        // 移动元素, 覆盖删除的元素,如果不是最后一个元素
        if (pos < len-1) intsetMoveTail(is,pos+1,pos);

        // 缩小数组内存空间
        // intsetResize(is,len-1); myerr
        is = intsetResize(is,len-1);

        // 更新元素计数器
        is->length = intrev32ifbe(len-1);
    }

    return is;
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

    // 添加相同编码长度的值
    printf("Basic adding: "); {
        is = intsetNew();
        is = intsetAdd(is,5,&success); assert(success);
        is = intsetAdd(is,6,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(!success);
        ok();
    }

    // 16编码方式下,随机添加 1024 次
    // 检查元素值是否按顺序排列
    printf("Large number of random adds: "); {
        int inserts = 0;
        is = intsetNew();
        for (i = 0; i < 1024; i++) {
            is = intsetAdd(is,rand()%0x800,&success);
            if (success) inserts++;
        }
        assert(intrev32ifbe(is->length) == inserts);
        checkConsistency(is);
        ok();
    }

    // 升级编码方式(16 升级 32), 测试添加,查找是否正常
    printf("Upgrade from int16 to int32: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,65535));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-65535));
        checkConsistency(is);
        ok();
    }

    // 升级编码方式(16 升级 64), 测试添加,查找是否正常
    printf("Upgrade from int16 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    // 升级编码方式(32 升级 64), 测试添加,查找是否正常
    printf("Upgrade from int32 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    // 
    printf("Stress lookups: "); {
        long num = 100000, size = 10000;
        int i, bits = 20;
        long long start;
        is = createSet(bits,size);
        checkConsistency(is);

        start = usec();
        for (i = 0; i < num; i++) intsetSearch(is,rand() % ((1<<bits)-1),NULL);
        printf("%ld lookups, %ld element set, %lldusec\n",num,size,usec()-start);
    }

    // 添加和删除的压力测试
    printf("Stress add+delete: "); {
        int i, v1, v2;
        is = intsetNew();
        for (i = 0; i < 0xffff; i++) {
            v1 = rand() % 0xfff;
            is = intsetAdd(is,v1,NULL);
            assert(intsetFind(is,v1));

            v2 = rand() % 0xfff;
            is = intsetRemove(is,v2,NULL);
            assert(!intsetFind(is,v2));
        }
        checkConsistency(is);
        ok();
    }


    return 0;
}

// #endif