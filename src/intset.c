#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"
#include <sys/time.h>

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
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int64_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

/**
 * 调整整数集合的内存空间大小
 * 
 * 如果调整后的空间大于原空间
 * 那么集合中原有元素的值不会被改变
 * 
 * 返回调整后的整数集合
 * 
 * T = O(N) 疑问:为什么是 O(N) ? 难道不是 O(1)吗 ?
 */
static intset *intsetResize(intset *is,uint32_t len) {

    // 计算数组空间长度
    uint32_t size = len * intrev32ifbe(is->encoding);

    // 重新分配数据空间
    // 如果新长度大于旧长度,旧数据依然存在
    is = zrealloc(is,sizeof(intset)+size);

    return is;
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
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {
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
            max = mid - 1;
        } else if (value > cur) {
            min = mid + 1;
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

/**
 * 升级编码方式同时添加 value 到集合数组
 */
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) {

    // 当前的编码方式
    uint8_t curenc = intrev32ifbe(is->encoding);

    // 新值的编码方式
    uint8_t newenc = _intsetValueEncoding(value);

    // 当前集合的元素数量, 为迭代移动已有元素所用
    int length = intrev32ifbe(is->length);

    // 如果 value 是因为最大值超过当前编码,那就添加到最后面
    // 如果 value 是因为最小值超过当前编码,那就添加到最前面
    int prepend = (value < 0) ? 1 : 0;

    // 更新集合编码方式
    is->encoding = intrev32ifbe(newenc);

    // 计算改用新编码方式后的空间大小
    // 重新分配内存空间
    is = intsetResize(is,intrev32ifbe(is->length)+1);

    // 现有元素按新编码方式移动元素
    // 从后向前移动, 因为空间大了, 后面一定是空的
    while (length--)
        _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));

    // 添加 value 到底层数组
    if (prepend) {
        _intsetSet(is,0,value);
    } else {
        _intsetSet(is,intrev32ifbe(is->length),value);
    }

    // 更新元素计数器
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

/**
 * 向前或向后移动元素
 * 把 n 个元素从 from 索引位置移动到 to 索引位置
 * 
 * T = O(N)
 */
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) {

    void *src, *dst;

    // 计算移动的元素数量
    uint32_t byte = intrev32ifbe(is->length) - from;

    // 通过编码方式,获取单元素长度
    uint8_t encoding = intrev32ifbe(is->encoding);

    // 计算起始位置,移动后的起始位置,移动长度
    if (encoding == INTSET_ENC_INT64) {
        src = ((int64_t*)is->contents)+from;
        dst = ((int64_t*)is->contents)+to;
        byte *= sizeof(int64_t);
    } else if (encoding == INTSET_ENC_INT32) {
        src = ((int32_t*)is->contents)+from;
        dst = ((int32_t*)is->contents)+to;
        byte *= sizeof(int32_t);
    } else {
        src = ((int16_t*)is->contents)+from;
        dst = ((int16_t*)is->contents)+to;
        byte *= sizeof(int16_t);
    }

    // 进行移动
    // T = O(N)
    memmove(dst,src,byte);
}

/*--------------------- API --------------------*/

/**
 * 创建并返回一个空整数结合
 * T = O(1)
 */
intset *intsetNew(void) {

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
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {

    // 获取 value 的编码凡是
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    if (success) *success = 1;

    // value 编码方式大于当前编码方式
    if (valenc > intrev32ifbe(is->encoding)) {

        // 升级编码方式并添加新元素
        return intsetUpgradeAndAdd(is,value);
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

        // 走到这里,说明 value 可以添加到数据
        // 集合数组扩容, 为 value 分配空间
        is = intsetResize(is,intrev32ifbe(is->length)+1);

        // 如果新元素不是添加到底层数组的末尾
        // 那么需要移动现有元素, 空出 pos 索引位置, 用于设置新值
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);
    }

    // 添加 value 到集合
    _intsetSet(is,pos,value);

    // 更新集合的元素数量
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

/**
 * 查找 value 是否存在于集合中
 * 存在返回 1, 不存在返回 0
 * 
 * T = O(log N)
 */
uint8_t intsetFind(intset *is, int64_t value) {

    // 计算编码方式
    uint8_t valenc = _intsetValueEncoding(value);

    // 如果 value 编码大于当前集合编码, 那么 value 一定不存在于集合
    // 当 value 编码小于等于当前集合, 再进行查找

    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,NULL);
}

/**
 * 从整数集合中删除值 value
 * 
 * *success 的值表示删除是否成功
 * 删除失败或值不存在, 该值为 0
 * 删除成功, 该值为 1
 * 
 * T = O(N)
 */
intset *intsetRemove(intset *is, int64_t value, int *success) {

    // 计算 value 的编码方式
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    // 默认返回删除失败
    if (success) *success = 0;

    // 当前编码大于等于 value的编码空间, 再进行查找
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, &pos)) {

        // 取出集合当前的元素数量
        uint32_t len = intrev32ifbe(is->length);

        // 默认返回删除成功
        if (success) *success = 1;

        // 移动元素,如果是最后一个元素就不用移动了
        if (pos < (len-1)) intsetMoveTail(is,pos+1,pos);

        // 重新分配内存空间, 缩小数组空间
        is = intsetResize(is, len-1);

        // 更新元素计数器
        is->length = intrev32ifbe(len-1);
    }

    return is;
}

/**
 * 取出集合底层数组指定位置的值, 并将它保存到 value 指针中
 * 
 * 取出值返回 1, 否则返回 0
 * 
 * T = O(1)
 */
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value) {

    // 检查 pos 是否符合数组的范围
    if (pos < intrev32ifbe(is->length)) {

        // 保存值到指针
        *value = _intsetGet(is, pos);

        return 1;
    }

    // 超出索引范围
    return 0;
}

/**
 * 从整数集合中随机返回一个元素
 * 只能在集合非空时使用
 * 
 * T = O(1)
 */
int64_t intsetRandom(intset *is) {
    return _intsetGet(is, rand()%intrev32ifbe(is->length));
}

/**
 * 返回整数集合现有的元素数量
 */
uint32_t intsetLen(intset *is) {
    return intrev32ifbe(is->length);
}

/**
 * 返回整数集合现在占用的字节总长度
 * 总长度包含整数集合的结构大小,以及所有元素的大小
 * 
 * T = O(1)
 */
uint32_t intsetBlobLen(intset *is) {
    return sizeof(intset)+intrev32ifbe(is->length)*intrev32ifbe(is->encoding);
}

// #ifdef INTSET_TEST_MAIN

/*---------------------  --------------------*/

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
