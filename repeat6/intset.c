#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"

// 编码方式
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/*--------------------- private --------------------*/

// 将 v 转换成编码方式
static uint8_t _intsetValueEncoding(int64_t v) {

    if (v < INT32_MIN || v > INT32_MAX) {
        return INTSET_ENC_INT64;
    } else if (v < INT16_MIN || v > INT16_MAX) {
        return INTSET_ENC_INT32;
    } else {
        return INTSET_ENC_INT16;
    }
}

// 按指定编码方式获取指定索引的元素值
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc) {

    int16_t v16;
    int32_t v32;
    int64_t v64;
    // 获取并返回元素值
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

// 获取指定索引的元素值
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is, pos, intrev32ifbe(is->encoding));
}

// 将 value 添加到指定索引位置
static void _intsetSet(intset *is, int pos, int64_t value) {

    // 获取当前编码方式
    uint32_t enc = intrev32ifbe(is->encoding);

    // 设置值
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

// 按编码方式重新分配指定元素数量大小的内存空间
static intset *intsetResize(intset *is, uint32_t len) {

    uint32_t size = len * intrev32ifbe(is->encoding);

    is = zrealloc(is,sizeof(intset)+size);

    return is;
}

// 查找 value 是否存在
// 存在返回 1 , 并且填充 pos 为 value 的索引位置
// 不存在返回 0 , pos 为适合添加的索引位置
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {

    // uint32_t min = 0, max = intrev32ifbe(is->length)-1, mid = -1; myerr
    // uint32_t cur = -1;
    int min = 0, max = intrev32ifbe(is->length)-1, mid = -1;
    int64_t cur = -1;

    // 底层数组为空, 不查找
    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;
    } 
    // 底层数组非空
    else {
        // 边界查找
        if (value < _intsetGet(is,0)) {
            if (pos) *pos = 0;
            return 0;
        } else if (value > _intsetGet(is,intrev32ifbe(is->length)-1)) {
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        }
    }

    // 二分查找
    while (max >= min) {
        mid = (max+min)/2;
        cur = _intsetGet(is,mid);
        if (value > cur) {
            min = mid + 1;
        } else if (value < cur){
            max = mid - 1;
        } else break;
    }

    // 确认找没找到值
    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}

// 按 value 的编码方式升级底层数组并添加 value
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) {

    // 获取当前编码方式
    // uint32_t curenc = intrev32ifbe(is->encoding); myerr
    uint8_t curenc = intrev32ifbe(is->encoding);

    // 计算 value 的编码方式
    // uint32_t valenc = _intsetValueEncoding(value);
    uint8_t valenc = _intsetValueEncoding(value);

    // 获取元素数量
    int length = intrev32ifbe(is->length);

    // 计算 value 是添加到头还是尾,因为 value 超范围, 不是头就是尾
    int prepend = value < 0 ? 1 : 0;

    // 按新编码方式申请内存空间
    is->encoding = valenc;
    // is = intsetResize(is,length); myerr
    is = intsetResize(is,intrev32ifbe(is->length)+1);

    // 移动底层数组的元素
    while(length--)
        _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));


    // 添加 value
    if (prepend) {
        _intsetSet(is,0,value);
    } else {
        _intsetSet(is,intrev32ifbe(is->length),value);
    }

    // 更新元素计数器
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    // 返回
    return is;
}

// 向前或先后移动 n 个元素
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) {

    void *dst, *src;
    // 移动的元素数量
    uint32_t byte = intrev32ifbe(is->length) - from;

    uint8_t encoding = intrev32ifbe(is->encoding);

    // 计算移动前的位置,移动后的位置,移动的字节数
    if (encoding == INTSET_ENC_INT64) {
        dst = ((int64_t*)is->contents)+to;
        src = ((int64_t*)is->contents)+from;
        byte *= sizeof(int64_t);
    } else if (encoding == INTSET_ENC_INT32) {
        dst = ((int32_t*)is->contents)+to;
        src = ((int32_t*)is->contents)+from;
        byte *= sizeof(int32_t);
    } else {
        dst = ((int16_t*)is->contents)+to;
        src = ((int16_t*)is->contents)+from;
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

// 添加新元素
// 成功 succss 为 1, 否则 success 为 0
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {

    uint32_t pos;
    // 计算 value 的编码方式
    uint32_t valenc = _intsetValueEncoding(value);

    if (success) *success = 1;

    // value 的编码方式大于当前编码方式
    if (valenc > intrev32ifbe(is->encoding)) {

        // 按新编码方式升级底层数组并添加 value
        return intsetUpgradeAndAdd(is,value);
    }
    // 按当前编码方式添加 value
    else {

        // value 存在于数组中, 不添加并返回
        if (intsetSearch(is,value,&pos)) {
            if (success) *success = 0;
            return is;
        }

        // 扩容数组空间
        // intsetResize(is,intrev32ifbe(is->length)+1);
        is = intsetResize(is,intrev32ifbe(is->length)+1);

        // 如果 value 添加的位置不是最后一个
        // 那么 value 所在索引之后的所有元素后移一位
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);

    }

    // 添加 value 到数组中
    _intsetSet(is,pos,value);

    // 更新数组的元素计数器
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    // 返回
    return is;
}

// 在底层数组中查找 value
// 找到返回 1, 否则返回 0
uint8_t intsetFind(intset *is, int64_t value) {
    
    // 计算 value 的编码方式
    uint8_t valenc = _intsetValueEncoding(value);
    // value 编码方式大于当前编码方式, 一定不存在
    return (valenc <= intrev32ifbe(is->encoding)) && intsetSearch(is,value,NULL);
}

// 从数组中删除 value
// 删除成功, success 为 1
// 删除失败或未找到, success 为 0
// intset *intsetRemove(intset *is, int64_t value, uint8_t *success) { myerr
intset *intsetRemove(intset *is, int64_t value, int *success) {

    uint32_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    if (success) *success = 0;

    // 如果 value 的编码方式大于当前编码方式, 无法删除
    // if (valenc <= intrev32ifbe(is->encoding)) { myerr
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, &pos)) {

        if (success) *success = 1;

        // 查找 value, 找不到返回
        if (!intsetSearch(is,value,&pos)) {
            if (success) *success = 0;
            return is;
        }

        // 走到这里找到 value, 移动元素, 覆盖 value所在位置的值
        if(pos < intrev32ifbe(is->length)-1) 
            intsetMoveTail(is,pos+1,pos);

        // 缩小元素空间
        is = intsetResize(is,intrev32ifbe(is->length)-1);

        // 更新元素计数器 myerr:缺少
        is->length = intrev32ifbe(intrev32ifbe(is->length)-1);
    }

    // 返回
    return is;
}

uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value) {


    if (pos < intrev32ifbe(is->length)) {
        *value = _intsetGet(is,pos);
        return 1;
    }

    return 0;
}

int64_t intsetRandom(intset *is) {
    return _intsetGet(is,rand()%intrev32ifbe(is->length));
}

uint32_t intsetLen(intset *is) {
    return intrev32ifbe(is->length);
}

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