#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include <assert.h>


/*--------------------- encode --------------------*/

// 计算并返回前置节点长度编码所需字节数
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {
    // 只返回计算的字节数
    if (p == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : 5;
    // 计算字节数并写入 p 指向的位置
    } else {
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;
        } else {
            p[0] = ZIP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));
            memrev32ifbe(p+1);
            return 5;
        }
    }
}

// 计算并返回当前节点长度编码所需字节数
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {
    
    unsigned char len = 1, buf[5];
    // 字符串
    if (ZIP_IS_STR(encoding)) {
        
        if (rawlen <= 0x3f) {
            if (!p) return len;
            buf[0] = ZIP_STR_06B | (rawlen & 0x3f);
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = (rawlen & 0xff);
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = ((rawlen >> 24) & 0xff);
            buf[2] = ((rawlen >> 16) & 0xff);
            buf[3] = ((rawlen >>  8) & 0xff);
            buf[4] = (rawlen & 0xff);
        }
    // 整型
    } else {
        if (!p) return len;
        buf[0] = encoding;
    }

    memcpy(p,buf,len);

    return len;
}

// 将字符型数字转换成整型,并将编码和值写入指针
// 转换成功返回 1, 失败返回 0
static int zipTryEncoding(unsigned char *s, unsigned int slen, long long *v, unsigned char *encoding) {

    long long value;

    // myerr:缺少
    if (slen >= 32 || slen ==0) return 0;

    // 转换成功
    // if (string2ll(s, slen, &value)) {
    if (string2ll((char*)s, slen, &value)) {

        // if (value >= ZIP_INT_IMM_MIN && value <= ZIP_INT_IMM_MAX) { myerr
        //     *encoding = 0xf0 | value;
        // } 
        if (value >= 0 && value <= 12) {
            *encoding = ZIP_INT_IMM_MIN + value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT_8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT_16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT_24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT_32B;
        } else {
            *encoding = ZIP_INT_64B;
        }

        *v = value;

        return 1;
    }

    // 转换失败
    return 0;
}


/*--------------------- decode --------------------*/
// 提取前置节点长度编码
#define ZIP_DECODE_PREVLENSIZE(p, prevlensize) do{  \
    if ((p)[0] < ZIP_BIGLEN) {                        \
        (prevlensize) = 1;                          \
    } else {                                        \
        (prevlensize) = 5;                          \
    }                                               \
}while(0)                                           \

// 提取前置节点长度编码和长度
#define ZIP_DECODE_PREVLEN(p, prevlensize, prevlen) do{     \
    /*长度编码*/                                             \
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);                 \
    /*提取长度*/                                             \
    if ((prevlensize) == 1) {                               \
        (prevlen) = ((int8_t*)p)[0];                        \
    } else if ((prevlensize) == 5) {                        \
        /*memcpy(prevlen,p,4); myerr*/                      \
        assert(sizeof((prevlensize)) == 4);                 \
        memcpy(&(prevlen), ((char*)(p))+1, 4);              \
        memrev32ifbe(&prevlen);                             \
    }                                                       \
}while(0)

// 通过编码获取整型的存储长度
static unsigned int zipIntSize(unsigned char encoding) {

    switch(encoding){
    case ZIP_INT_8B: return 1;
    case ZIP_INT_16B: return 2;
    case ZIP_INT_24B: return 3;
    case ZIP_INT_32B: return 4;
    case ZIP_INT_64B: return 8;
    default: return 0;
    }

    assert(NULL);
    return 0;
}

// 提取节点编码、长度编码、长度
#define ZIP_DECODE_LENGTH(p, encoding, lensize, len) do{    \
                                                            \
    ZIP_ENTRY_ENCODING((p), (encoding));                    \
                                                            \
    if ((encoding) < ZIP_STR_MASK) {                        \
        if ((encoding) == ZIP_STR_06B) {                    \
            (lensize) = 1;                                  \
            (len) = (p)[0] & 0x3f;                          \
        } else if ((encoding) == ZIP_STR_14B) {             \
            (lensize) = 2;                                  \
            (len) = (((p)[0] & 0x3f) << 8) | (p)[1];        \
        } else if ((encoding) == ZIP_STR_32B){              \
            (lensize) = 5;                                  \
            (len) = ((p)[1] << 24) |                        \
                    ((p)[2] << 16)  |                       \
                    ((p)[3] <<  8)  |                       \
                    ((p)[4]);                               \
        } else {                                            \
            assert(NULL);                                   \
        }                                                   \
    } else {                                                \
        (lensize) = 1;                                      \
        (len) = zipIntSize(encoding);                       \
    }                                                       \
}while(0)


/*--------------------- privdata --------------------*/

// 以 encoding 的编码方式写入整数值到 p 指向的位置 myerr:不会
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {

    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {
        p[0] = value;
    } else if (encoding == ZIP_INT_16B){
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev16ifbe(p);
    } else if (encoding == ZIP_INT_24B){
        i32 = value << 8;
        memrev32ifbe(&i32);
        memcpy(p, ((uint8_t*)&i32)+1, sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B){
        i32 = value;
        memcpy(p, &i32, sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT_64B){
        i64 = value;
        memcpy(p, &i64, sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        /* 值存在 encoding 中*/
    } else {
        assert(NULL);
    }
}

// 以 encoding 的编码方式读取并返回整数值
static int64_t zipLoadInteger(unsigned char *p, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64, ret = 0;

    if (encoding == ZIP_INT_8B) {
        ret = ((int8_t*)p)[0];
    } else if (encoding == ZIP_INT_16B) {
        memcpy(&i16, p, sizeof(i16));
        memrev16ifbe(&i16);
        ret = i16;
    } else if (encoding == ZIP_INT_24B) {
        // memrev32ifbe(&i32); myerr
        // memcpy(((int8_t*)&i32)+1, p, sizeof(int32_t)-sizeof(int8_t));
        // ret = ((int8_t*)&i32)+1;
        i32 = 0;
        memcpy(((uint8_t*)&i32)+1, p, sizeof(i32)-sizeof(uint8_t));
        memrev32ifbe(&i32);
        ret = i32 >> 8;
    } else if (encoding == ZIP_INT_32B) {
        memcpy(&i32, p, sizeof(i32));
        memrev32ifbe(&i32);
        ret = i32;
    } else if (encoding == ZIP_INT_64B) {
        memcpy(&i64, p, sizeof(i64));
        memrev64ifbe(&i64);
        ret = i64;
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        // ret = encoding & 0x0f; myerr
        ret = (encoding & ZIP_INT_IMM_MASK) - 1;
    } else {
        assert(NULL);
    }

    return ret;
}

// 获取 p 指向的节点数据
static zlentry zipEntry(unsigned char *p) {

    zlentry e;

    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);
    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    e.headersize = e.prevrawlensize + e.lensize;

    e.p = p;

    return e;
}

// 获取 p 指向的节点的完整字节数
static unsigned int zipRawEntryLength(unsigned char *p) {
    unsigned int prevlensize, encoding, lensize, len;
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);

    return prevlensize+lensize+len;
}

// 计算 len 按前置节点编码长度 与 p指向的节点的前置节点编码长度的差值
static unsigned int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {

    unsigned int prevlensize;
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

// 重分配列表内存空间
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    // 重分配内存
    zl = zrealloc(zl, len);
    // 总长度 
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);
    // 末端标识符
    zl[len-1] = ZIP_END;

    return zl;
}

// 按 5 字节方式存储前置节点长度
static void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len) {
    if (p == NULL) return ;
    p[0] = ZIP_BIGLEN;
    memcpy(p+1, &len, sizeof(len));
    memrev32ifbe(p+1);
}

// 连锁更新, 当新增或删除节点会触发其后置节点的 prevlensize 
// 的字节数变化, 返回处理后的整数列表的首地址
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {

    unsigned int curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen,rawlensize,offset,noffset;
    unsigned char *np;
    int nextdiff = 0;
    zlentry cur, next;
    while (p[0] != ZIP_END) {

        // 获取当前节点的长度和字节数
        cur = zipEntry(p);
        rawlen = cur.headersize + cur.len;
        rawlensize = zipPrevEncodeLength(NULL, rawlen);

        // 是否存在下一个节点
        if (p[rawlen] == ZIP_END) break;

        // 获取下一个节点的信息
        next = zipEntry(p+rawlen);

        // 完全相同, 无需更新, 退出
        if (next.prevrawlen == rawlen) break;

        // 后置节点需要扩容
        if (next.prevrawlensize < rawlensize) {

            // 计算 nextdiff
            nextdiff = rawlensize - next.prevrawlensize;

            // 扩容
            offset = p-zl;
            zl = ziplistResize(zl, curlen+nextdiff);
            p = zl+offset;

            np = p + rawlen;
            noffset = np - zl;
            // 尝试将 nextdiff 加到尾节点偏移量
            if (zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) != np) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
            }

            // 移动原数据
            memmove(np+rawlensize,
                    np+next.prevrawlensize,
                    curlen-noffset-1-next.prevrawlensize);

            // 将长度值写入 next节点
            zipPrevEncodeLength(np, rawlen);

            // 移动到下一个节点
            // 更新总长度
            p += rawlen;
            curlen += nextdiff;

        // 后置节点无需扩容
        } else {
            
            if (next.prevrawlensize > rawlensize) {
                zipPrevEncodeLengthForceLarge(p+rawlen, rawlen);
            } else {
                zipPrevEncodeLength(p+rawlen, rawlen);
            }

            // 退出
            break;
        }
    }

    return zl;
}

// 向 p 指向节点之前添加一个节点
// 返回处理完的列表首地址
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), prevlen, reqlen, offset;
    unsigned char encoding = 0;
    int nextdiff = 0;
    long long value = 123456789;
    zlentry entry, tail;

    // 获取前置节点长度
    if (p[0] != ZIP_END) {
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 获取节点值所需的字节数
    // 尝试将字符型整数转换成整数
    if (zipTryEncoding(s, slen, &value, &encoding)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算前置节点长度所需字节数
    reqlen += zipPrevEncodeLength(NULL, prevlen);

    // 计算当前节点编码所需字节数
    reqlen += zipEncodeLength(NULL, encoding, slen);

    // 计算 nextdiff, 新节点的后置节点
    // 的 prevlensize 是否足够
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0; 

    // 扩容
    offset = p-zl;
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl+offset;

    // 添加新节点的位置
    if (p[0] != ZIP_END) {
    // 中间添加
        // 移动原数据
        memmove(p+reqlen, p-nextdiff, curlen-offset-1+nextdiff);

        // 写入后置节点 prevlen 值
        zipPrevEncodeLength(p+reqlen, reqlen);

        // 更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        // next 所在节点非尾节点, 将其加入尾节点偏移量
        tail = zipEntry(p+reqlen);
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }
    // 尾部添加
    } else {

        // 尾节点偏移量指向新节点
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    // 连锁更新
    if (nextdiff != 0) {
        offset = p-zl;
        // zl = __ziplistCascadeUpdate(zl, p); myerr
        zl = __ziplistCascadeUpdate(zl, p+reqlen);
        p = zl+offset;
    }

    // 写入节点数据
    p += zipPrevEncodeLength(p, prevlen);
    p += zipEncodeLength(p, encoding, slen);
    if (ZIP_IS_STR(encoding)) {
        memcpy(p, s, slen);
    } else {
        zipSaveInteger(p, value, encoding);
    }

    // 更新节点计数器
    ZIP_INCR_LENGTH(zl, 1);

    return zl;

}

// 从 p 开始连续删除 num 个节点
// 返回删除后的整数列表首地址
static unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num) {
    
    unsigned int totlen, deleted = 0, offset;
    int i, nextdiff = 0;
    zlentry first, tail;

    first = zipEntry(p);
    // 计算删除的节点数
    for (i=0; p[0]!=ZIP_END && i<num; i++) {
        p += zipRawEntryLength(p);
        deleted++;
    }

    // 计算删除的总字节数
    totlen = p - first.p;

    // 存在可删除的字节
    if (totlen > 0) {

        // 中间删除
        if (p[0] != ZIP_END) {

            // 计算 nextdiff
            nextdiff = zipPrevLenByteDiff(p, first.prevrawlen);
            // 移动 p 指针, 包含 nextdiff
            p -= nextdiff;
            
            // 将前置节点长度写入 nextdiff 的节点
            zipPrevEncodeLength(p, first.prevrawlen);

            // 更新尾节点偏移量
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))-totlen);

            // 尝试将 nextdiff 添加到尾节点偏移量
            tail = zipEntry(p);
            if (p[tail.headersize+tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
            }
            
            // 移动原数据
            memmove(first.p, p, 
                    intrev32ifbe(ZIPLIST_BYTES(zl)) - (p-zl) -1);

        // 删到末端标识符了
        } else {

            // 更新尾节点偏移量
            ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(first.p - zl) - first.prevrawlen;
        }

        // 缩容
        offset = first.p - zl;
        zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl))-totlen+nextdiff);
        p = zl+offset;

        // 更新节点计数器
        ZIP_INCR_LENGTH(zl, -deleted);

        // 连锁更新
        if (nextdiff != 0) {
            zl = __ziplistCascadeUpdate(zl, p);
        }
    }

    return zl;
}
/*--------------------- API --------------------*/
// 创建并返回一个空列表
unsigned char *ziplistNew(void) {

    size_t len = ZIPLIST_HEADER_SIZE+1;
    // 申请内存空间
    unsigned char *zl = zmalloc(len);
    // 初始化属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
    ZIPLIST_LENGTH(zl) = 0;

    zl[len-1] = ZIP_END;

    return zl;
}

// 从两端添加节点
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where) {

    unsigned char *p;
    // 添加方向确定节点
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_TAIL(zl);

    return __ziplistInsert(zl, p, s, slen);
}

// 返回指定索引的节点地址
// 负索引, 从尾到头迭代, 从 -1 开始
// 正索引, 从头到尾迭代, 从 0 开始
// 索引超范围或者列表为空, 返回NULL
unsigned char *ziplistIndex(unsigned char *zl, int index) {

    unsigned char *p;
    zlentry entry;

    // 负索引
    if (index < 0) {

        // 计算距离
        index = (-index) - 1;
        // 起始节点
        p = ZIPLIST_ENTRY_TAIL(zl);

        // 从后向前迭代
        if (p[0] != ZIP_END) {

            entry = zipEntry(p);
            while (entry.prevrawlen > 0 && index--) {
                p -= entry.prevrawlen;
                entry = zipEntry(p);
            }
        }

    // 正索引
    } else {

        // 起始节点
        p = ZIPLIST_ENTRY_HEAD(zl);
        // 从前向后迭代
        while(p[0] != ZIP_END && index--) {
            p += zipRawEntryLength(p);
        }
    }

    return (p[0] == ZIP_END || index > 0) ? NULL : p;
}

// 返回 p 的后置节点, 到表尾节点或末端标识符返回 NULL
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p) {
    ((void) zl);
    // p 不是尾节点
    if (p[0] == ZIP_END)
        return NULL;

    // 移动到下一个节点
    p += zipRawEntryLength(p);

    // p 不是尾节点
    if (p[0] == ZIP_END)
        return NULL;

    // 返回
    return p;
}

// 返回 p 的前置节点, 如果是末端表示符, 移动到尾节点开始迭代
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p) {

    zlentry entry;
    if (p == ZIPLIST_ENTRY_HEAD(zl)) {
        return NULL;
    } else if(p[0] == ZIP_END) {
        p = ZIPLIST_ENTRY_TAIL(zl);
        return (p[0] == ZIP_END) ? NULL : p;
    } else {
        entry = zipEntry(p);
        assert(entry.prevrawlen > 0);
        return p - entry.prevrawlen;
    }
}

// 将 p 指向节点的值填入指针中, 提取成功返回 1, 否则返回 0
// 如果是值是字符串, 填入sstr和slen
// 如果是整数, 填入 sval
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval) {

    // 有效值判断
    if (p==NULL || p[0]==ZIP_END)
        return 0;
    if (*sstr) *sstr = NULL;

    // 获取 p 的节点
    zlentry entry = zipEntry(p);

    // 字符串
    if (ZIP_IS_STR(entry.encoding)) {
        if (sstr) {
            *sstr = p+entry.headersize;
            *slen = entry.len;
        }
    // 整数
    } else {
        if (sval) {
            *sval = zipLoadInteger(p+entry.headersize, entry.encoding);
        }
    }

    return 1;
}

// 从列表中删除 *p 指向节点, 返回列表首地址
// 更新 *p 位置
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p) {

    // 记住偏移量
    size_t offset = *p - zl;

    // 删除节点
    zl = __ziplistDelete(zl, *p, 1);

    // 更新 p 的位置
    *p = zl + offset;

    return zl;
}

// 从指定索引的节点开始, 连续删除 num 个节点, 返回删除后的列表首地址
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num) {

    // 获取索引指向的节点
    unsigned char *p = ziplistIndex(zl, index);

    // 执行删除
    return (p == NULL) ? zl : __ziplistDelete(zl, p, num);
}

// 将 p 指向节点值与 sstr 比对
// 相同返回 1, 否则返回 0
unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen) {

    if (p[0] == ZIP_END) return 0;

    // 获取节点信息
    zlentry entry = zipEntry(p);

    // 字符串
    if (ZIP_IS_STR(entry.encoding)) {

        // 比对长度
        if (entry.len == slen) {
            // 比对内容
            return memcpy(p+entry.headersize, sstr, slen) == 0;
        } else {
            return 0;
        }
    // 数值
    } else {
        long long sval,zval;
        unsigned char sencoding;
        // 将 sstr 转换成整数
        if (zipTryEncoding(sstr,slen, &sval, &sencoding)) {

            // 获取 p 的节点值
            // zval = zipLoadInteger(p, entry.encoding); myerr
            zval = zipLoadInteger(p+entry.headersize, entry.encoding);
            // 比对值
            return zval == sval;
        }
    }

    // 不相同
    return 0;
}

// 从 p 节点开始, 每次跳过 skip 个节点, 然后比对节点值
// 节点值相同返回
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip) {

    unsigned int skipcnt = 0;
    unsigned char vencoding = 0;
    long long vll;

    // 查找节点
    while (p[0] != ZIP_END) {
        unsigned int prevlensize, encoding, lensize, len;
        unsigned char *q;
        // 获取节点的前置节点长度
        ZIP_DECODE_PREVLENSIZE(p, prevlensize);
        // 获取节点编码和长度
        ZIP_DECODE_LENGTH(p+prevlensize, encoding, lensize, len);

        q = p + prevlensize + lensize;

        if (skipcnt == 0) {


            // 字符串
            if (ZIP_IS_STR(encoding)) {

                // 比对字符串
                if (len = vlen && memcmp(q, vstr, len) == 0)
                    return p; 
            // 整数
            } else {

                // 获取 vstr 的整数值
                if (vencoding == 0) {

                    if (!zipTryEncoding(vstr,vlen, &vll, &vencoding)) {
                        vencoding = UCHAR_MAX;
                    }
                }

                if (vencoding != UCHAR_MAX) {

                    // 获取节点的整数值
                    long long ll = zipLoadInteger(q, encoding);
                    // 比对
                    if (ll == vll) {
                        return p;
                    }
                }
            }

            // 重置跳跃节点数
            skipcnt = skip;
        } else {

            // 跳过节点
            skipcnt--;
        }

        p = q + len;

    }

    return NULL;
}

// 返回压缩列表的节点数量
unsigned int ziplistLen(unsigned char *zl) {

    // 节点数量大于 16位整数最大值 时, 要迭代计算节点数
    unsigned int size = 0;
    if (intrev32ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) {
        size = intrev32ifbe(ZIPLIST_LENGTH(zl));
    } else {
        unsigned char *p = zl + ZIPLIST_HEADER_SIZE;
        while(*p != ZIP_END) {
            p += zipRawEntryLength(p);
            size++;
        }

        if (size < UINT16_MAX) ZIPLIST_LENGTH(zl) = intrev32ifbe(size);
    }

    return size;
}

// 列表总字节数
size_t ziplistBlobLen(unsigned char *zl) {
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}

#ifdef ZIPLIST_TEST_MAIN
/*--------------------- debug --------------------*/
#include <sys/time.h>
#include "adlist.h"
#include "sds.h"

unsigned char *createList() {
    unsigned char *zl = ziplistNew();
    zl = ziplistPush(zl, (unsigned char*)"foo", 3, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"quux", 4, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"hello", 5, ZIPLIST_HEAD);
    zl = ziplistPush(zl, (unsigned char*)"1024", 4, ZIPLIST_TAIL);
    return zl;
}

unsigned char *createIntList() {
    unsigned char *zl = ziplistNew();
    char buf[32];

    sprintf(buf, "100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "128000");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "-100");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "4294967296");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    sprintf(buf, "non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    sprintf(buf, "much much longer non integer");
    zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    return zl;
}

// long long usec(void) {
//     struct timeval tv;
//     gettimeofday(&tv,NULL);
//     return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
// }

// void stress(int pos, int num, int maxsize, int dnum) {
//     int i,j,k;
//     unsigned char *zl;
//     char posstr[2][5] = { "HEAD", "TAIL" };
//     long long start;
//     for (i = 0; i < maxsize; i+=dnum) {
//         zl = ziplistNew();
//         for (j = 0; j < i; j++) {
//             zl = ziplistPush(zl,(unsigned char*)"quux",4,ZIPLIST_TAIL);
//         }

//         /* Do num times a push+pop from pos */
//         start = usec();
//         for (k = 0; k < num; k++) {
//             zl = ziplistPush(zl,(unsigned char*)"quux",4,pos);
//             zl = ziplistDeleteRange(zl,0,1);
//         }
//         printf("List size: %8d, bytes: %8d, %dx push+pop (%s): %6lld usec\n",
//             i,intrev32ifbe(ZIPLIST_BYTES(zl)),num,posstr[pos],usec()-start);
//         zfree(zl);
//     }
// }

void pop(unsigned char *zl, int where) {
    unsigned char *p, *vstr;
    unsigned int vlen;
    long long vlong;

    p = ziplistIndex(zl,where == ZIPLIST_HEAD ? 0 : -1);
    if (ziplistGet(p,&vstr,&vlen,&vlong)) {
        if (where == ZIPLIST_HEAD)
            printf("Pop head: ");
        else
            printf("Pop tail: ");

        if (vstr)
            if (vlen && fwrite(vstr,vlen,1,stdout) == 0) perror("fwrite");
        else
            printf("%lld", vlong);

        printf("\n");
        ziplistDeleteRange(zl,-1,1);
    } else {
        printf("ERROR: Could not pop\n");
        exit(1);
    }
}

// int randstring(char *target, unsigned int min, unsigned int max) {
//     int p = 0;
//     int len = min+rand()%(max-min+1);
//     int minval, maxval;
//     switch(rand() % 3) {
//     case 0:
//         minval = 0;
//         maxval = 255;
//     break;
//     case 1:
//         minval = 48;
//         maxval = 122;
//     break;
//     case 2:
//         minval = 48;
//         maxval = 52;
//     break;
//     default:
//         assert(NULL);
//     }

//     while(p < len)
//         target[p++] = minval+rand()%(maxval-minval+1);
//     return len;
// }

// void verify(unsigned char *zl, zlentry *e) {
//     int i;
//     int len = ziplistLen(zl);
//     zlentry _e;

//     for (i = 0; i < len; i++) {
//         memset(&e[i], 0, sizeof(zlentry));
//         e[i] = zipEntry(ziplistIndex(zl, i));

//         memset(&_e, 0, sizeof(zlentry));
//         _e = zipEntry(ziplistIndex(zl, -len+i));

//         assert(memcmp(&e[i], &_e, sizeof(zlentry)) == 0);
//     }
// }

void ziplistRepr(unsigned char *zl) {
    unsigned char *p;
    int index = 0;
    zlentry entry;

    printf(
        "{total bytes %d} "
        "{length %u}\n"
        "{tail offset %u}\n",
        intrev32ifbe(ZIPLIST_BYTES(zl)),
        intrev16ifbe(ZIPLIST_LENGTH(zl)),
        intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
    p = ZIPLIST_ENTRY_HEAD(zl);
    while(*p != ZIP_END) {
        entry = zipEntry(p);
        printf(
            "{"
                "addr 0x%08lx, "
                "index %2d, "
                "offset %5ld, "
                "rl: %5u, "
                "hs %2u, "
                "pl: %5u, "
                "pls: %2u, "
                "payload %5u"
            "} ",
            (long unsigned)p,
            index,
            (unsigned long) (p-zl),
            entry.headersize+entry.len,
            entry.headersize,
            entry.prevrawlen,
            entry.prevrawlensize,
            entry.len);
        p += entry.headersize;
        if (ZIP_IS_STR(entry.encoding)) {
            if (entry.len > 40) {
                if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
                printf("...");
            } else {
                if (entry.len &&
                    fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
            }
        } else {
            printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
        }
        printf("\n");
        p += entry.len;
        index++;
    }
    printf("{end}\n\n");
}

// gcc -g zmalloc.c sds.c util.c ziplist.c
int main(void) {
    unsigned char *zl, *p;
    unsigned char *entry;
    unsigned int elen;
    long long value;

    /* If an argument is given, use it as the random seed. */
    // if (argc == 2)
        // srand(atoi(argv[1]));
    srand(time(NULL));

    zl = createIntList();
    ziplistRepr(zl);

    zl = createList();
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_HEAD);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    pop(zl,ZIPLIST_TAIL);
    ziplistRepr(zl);

    // printf("Get element at index 3:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 3);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index 3\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index 4 (out of range):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 4);
    //     if (p == NULL) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
    //         return 1;
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -1 (last element):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index -1\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -4 (first element):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -4);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("ERROR: Could not access index -4\n");
    //         return 1;
    //     }
    //     if (entry) {
    //         if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         printf("\n");
    //     } else {
    //         printf("%lld\n", value);
    //     }
    //     printf("\n");
    // }

    // printf("Get element at index -5 (reverse out of range):\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -5);
    //     if (p == NULL) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR: Out of range index should return NULL, returned offset: %ld\n", p-zl);
    //         return 1;
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 0 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 0);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 1 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate list from 2 to end:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 2);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistNext(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate starting out of range:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, 4);
    //     if (!ziplistGet(p, &entry, &elen, &value)) {
    //         printf("No entry\n");
    //     } else {
    //         printf("ERROR\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate from back to front:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         p = ziplistPrev(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Iterate from back to front, deleting all items:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl, -1);
    //     while (ziplistGet(p, &entry, &elen, &value)) {
    //         printf("Entry: ");
    //         if (entry) {
    //             if (elen && fwrite(entry,elen,1,stdout) == 0) perror("fwrite");
    //         } else {
    //             printf("%lld", value);
    //         }
    //         zl = ziplistDelete(zl,&p);
    //         p = ziplistPrev(zl,p);
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    // printf("Delete inclusive range 0,0:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 0, 1);
    //     ziplistRepr(zl);
    // }

    // printf("Delete inclusive range 0,1:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 0, 2);
    //     ziplistRepr(zl);
    // }

    // printf("Delete inclusive range 1,2:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 1, 2);
    //     ziplistRepr(zl);
    // }

    // printf("Delete with start index out of range:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 5, 1);
    //     ziplistRepr(zl);
    // }

    // printf("Delete with num overflow:\n");
    // {
    //     zl = createList();
    //     zl = ziplistDeleteRange(zl, 1, 5);
    //     ziplistRepr(zl);
    // }

    // printf("Delete foo while iterating:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl,0);
    //     while (ziplistGet(p,&entry,&elen,&value)) {
    //         if (entry && strncmp("foo",(char*)entry,elen) == 0) {
    //             printf("Delete foo\n");
    //             zl = ziplistDelete(zl,&p);
    //         } else {
    //             printf("Entry: ");
    //             if (entry) {
    //                 if (elen && fwrite(entry,elen,1,stdout) == 0)
    //                     perror("fwrite");
    //             } else {
    //                 printf("%lld",value);
    //             }
    //             p = ziplistNext(zl,p);
    //             printf("\n");
    //         }
    //     }
    //     printf("\n");
    //     ziplistRepr(zl);
    // }

    // printf("Regression test for >255 byte strings:\n");
    // {
    //     char v1[257],v2[257];
    //     memset(v1,'x',256);
    //     memset(v2,'y',256);
    //     zl = ziplistNew();
    //     zl = ziplistPush(zl,(unsigned char*)v1,strlen(v1),ZIPLIST_TAIL);
    //     zl = ziplistPush(zl,(unsigned char*)v2,strlen(v2),ZIPLIST_TAIL);

    //     /* Pop values again and compare their value. */
    //     p = ziplistIndex(zl,0);
    //     assert(ziplistGet(p,&entry,&elen,&value));
    //     assert(strncmp(v1,(char*)entry,elen) == 0);
    //     p = ziplistIndex(zl,1);
    //     assert(ziplistGet(p,&entry,&elen,&value));
    //     assert(strncmp(v2,(char*)entry,elen) == 0);
    //     printf("SUCCESS\n\n");
    // }

    // printf("Regression test deleting next to last entries:\n");
    // {
    //     char v[3][257];
    //     zlentry e[3];
    //     int i;

    //     for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
    //         memset(v[i], 'a' + i, sizeof(v[0]));
    //     }

    //     v[0][256] = '\0';
    //     v[1][  1] = '\0';
    //     v[2][256] = '\0';

    //     zl = ziplistNew();
    //     for (i = 0; i < (sizeof(v)/sizeof(v[0])); i++) {
    //         zl = ziplistPush(zl, (unsigned char *) v[i], strlen(v[i]), ZIPLIST_TAIL);
    //     }

    //     verify(zl, e);

    //     assert(e[0].prevrawlensize == 1);
    //     assert(e[1].prevrawlensize == 5);
    //     assert(e[2].prevrawlensize == 1);

    //     /* Deleting entry 1 will increase `prevrawlensize` for entry 2 */
    //     unsigned char *p = e[1].p;
    //     zl = ziplistDelete(zl, &p);

    //     verify(zl, e);

    //     assert(e[0].prevrawlensize == 1);
    //     assert(e[1].prevrawlensize == 5);

    //     printf("SUCCESS\n\n");
    // }

    // printf("Create long list and check indices:\n");
    // {
    //     zl = ziplistNew();
    //     char buf[32];
    //     int i,len;
    //     for (i = 0; i < 1000; i++) {
    //         len = sprintf(buf,"%d",i);
    //         zl = ziplistPush(zl,(unsigned char*)buf,len,ZIPLIST_TAIL);
    //     }
    //     for (i = 0; i < 1000; i++) {
    //         p = ziplistIndex(zl,i);
    //         assert(ziplistGet(p,NULL,NULL,&value));
    //         assert(i == value);
    //         p = ziplistIndex(zl,-i-1);
    //         assert(ziplistGet(p,NULL,NULL,&value));
    //         assert(999-i == value);
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Compare strings with ziplist entries:\n");
    // {
    //     zl = createList();
    //     p = ziplistIndex(zl,0);
    //     if (!ziplistCompare(p,(unsigned char*)"hello",5)) {
    //         printf("ERROR: not \"hello\"\n");
    //         return 1;
    //     }
    //     if (ziplistCompare(p,(unsigned char*)"hella",5)) {
    //         printf("ERROR: \"hella\"\n");
    //         return 1;
    //     }

    //     p = ziplistIndex(zl,3);
    //     if (!ziplistCompare(p,(unsigned char*)"1024",4)) {
    //         printf("ERROR: not \"1024\"\n");
    //         return 1;
    //     }
    //     if (ziplistCompare(p,(unsigned char*)"1025",4)) {
    //         printf("ERROR: \"1025\"\n");
    //         return 1;
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Stress with random payloads of different encoding:\n");
    // {
    //     int i,j,len,where;
    //     unsigned char *p;
    //     char buf[1024];
    //     int buflen;
    //     list *ref;
    //     listNode *refnode;

    //     /* Hold temp vars from ziplist */
    //     unsigned char *sstr;
    //     unsigned int slen;
    //     long long sval;

    //     for (i = 0; i < 20000; i++) {
    //         zl = ziplistNew();
    //         ref = listCreate();
    //         listSetFreeMethod(ref,sdsfree);
    //         len = rand() % 256;

    //         /* Create lists */
    //         for (j = 0; j < len; j++) {
    //             where = (rand() & 1) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
    //             if (rand() % 2) {
    //                 buflen = randstring(buf,1,sizeof(buf)-1);
    //             } else {
    //                 switch(rand() % 3) {
    //                 case 0:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()) >> 20);
    //                     break;
    //                 case 1:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()));
    //                     break;
    //                 case 2:
    //                     buflen = sprintf(buf,"%lld",(0LL + rand()) << 20);
    //                     break;
    //                 default:
    //                     assert(NULL);
    //                 }
    //             }

    //             /* Add to ziplist */
    //             zl = ziplistPush(zl, (unsigned char*)buf, buflen, where);

    //             /* Add to reference list */
    //             if (where == ZIPLIST_HEAD) {
    //                 listAddNodeHead(ref,sdsnewlen(buf, buflen));
    //             } else if (where == ZIPLIST_TAIL) {
    //                 listAddNodeTail(ref,sdsnewlen(buf, buflen));
    //             } else {
    //                 assert(NULL);
    //             }
    //         }

    //         assert(listLength(ref) == ziplistLen(zl));
    //         for (j = 0; j < len; j++) {
    //             /* Naive way to get elements, but similar to the stresser
    //              * executed from the Tcl test suite. */
    //             p = ziplistIndex(zl,j);
    //             refnode = listIndex(ref,j);

    //             assert(ziplistGet(p,&sstr,&slen,&sval));
    //             if (sstr == NULL) {
    //                 buflen = sprintf(buf,"%lld",sval);
    //             } else {
    //                 buflen = slen;
    //                 memcpy(buf,sstr,buflen);
    //                 buf[buflen] = '\0';
    //             }
    //             assert(memcmp(buf,listNodeValue(refnode),buflen) == 0);
    //         }
    //         zfree(zl);
    //         listRelease(ref);
    //     }
    //     printf("SUCCESS\n\n");
    // }

    // printf("Stress with variable ziplist size:\n");
    // {
    //     stress(ZIPLIST_HEAD,100000,16384,256);
    //     stress(ZIPLIST_TAIL,100000,16384,256);
    // }

    return 0;
}

#endif