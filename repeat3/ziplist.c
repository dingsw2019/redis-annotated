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

/*--------------------- private --------------------*/

// 返回整型编码对应的字节数
static int zipIntSize(unsigned char encoding) {

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
/*--------------------- encode --------------------*/
// 前置节点长度编码到节点中
// 返回编码所需字节数
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {

    if (p == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len)+1;
    } else {
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;
        } else {
            p[0] = ZIP_BIGLEN;
            memcpy(p+1, &len, sizeof(len));
            memrev32ifbe(p+1);
            return sizeof(len)+1;
        }
    }
}

// 当前节点长度编码到节点中
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {
    unsigned char len = 1, buf[5];

    if (ZIP_IS_STR(encoding)) {

        if (rawlen <= 0x3f) {
            if (!p) return len;
            buf[0] = ZIP_STR_06B | rawlen;
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0xff);
            buf[1] = rawlen & 0xff;
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >>  8) & 0xff;
            buf[4] = rawlen & 0xff;
        }
    } else {
        if (!p) return len;
        buf[0] = encoding;
    }

    memcpy(p, buf, len);

    return len;
}

// 将前置节点长度编码到 5 字节的空间中
static void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len) {

    if (p == NULL) return ;
    p[0] = ZIP_BIGLEN;
    memcpy(p+1, &len, sizeof(len));
    memrev32ifbe(p+1);
}

/*--------------------- decode --------------------*/
// 获取节点编码
#define ZIP_ENTRY_ENCODING(ptr, encoding) do{                   \
    (encoding) = (ptr)[0];                                      \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK;  \
}while(0)

// 解码节点的前置节点长度占用字节数
#define ZIP_DECODE_PREVLENSIZE(p, prevlensize) do{  \
    if ((p)[0] < ZIP_BIGLEN)                        \
        (prevlensize) = 1;                          \
    else                                            \
        (prevlensize) = 5;                          \
}while(0)

// 解码节点的前置节点长度值和占用字节数
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do{   \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);               \
    if ((prevlensize) == 1) {                               \
        (prevlen) = (ptr)[0];                               \
    } else if ((prevlensize) == 5) {                        \
        assert(sizeof((prevlensize)) == 4);                 \
        memcpy(&(prevlen), ((char*)(ptr))+1, 4);            \
        memrev32ifbe(&(prevlen));                           \
    }                                                       \
}while(0)

// 解码节点的编码, 长度, 长度占用字节数
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do{  \
    ZIP_ENTRY_ENCODING(ptr, encoding);                      \
    if ((encoding) < ZIP_STR_MASK) {                        \
        if (encoding == ZIP_STR_06B) {                      \
            (lensize) = 1;                                  \
            (len) = (ptr)[0] & 0x3f;                        \
        } else if (encoding == ZIP_STR_14B) {               \
            (lensize) = 2;                                  \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];    \
        } else if (encoding == ZIP_STR_32B) {               \
            (lensize) = 5;                                  \
            (len) = ((ptr)[1] << 24) |                      \
                    ((ptr)[2] << 16) |                      \
                    ((ptr)[3] <<  8) |                      \
                    (ptr)[4];                               \
        } else {                                            \
            assert(NULL);                                   \
        }                                                   \
    } else {                                                \
        (lensize) = 1;                                      \
        (len) = zipIntSize(encoding);                       \
    }                                                       \
}while(0)

/*--------------------- base --------------------*/
// 节点总字节数
static unsigned int zipRawEntryLength(unsigned char *p) {
    unsigned int prevlensize, encoding, lensize, len;

    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);

    return prevlensize + lensize + len;
}

// 新前置节点长度(len) 与 p 所指向节点的前置节点字节数的差值
static int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {

    unsigned int prevlensize;

    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    return zipPrevEncodeLength(NULL,len) - prevlensize;
}

// 尝试将字符型数值转换为整型, 并将编码和整数写入指针中
// 成功返回 1, 否则返回 0
static int zipTryEncoding(unsigned char *s, unsigned int slen, long long *v, unsigned char *encoding) {

    long long value;

    if (slen >= 32 || slen == 0) return 0;

    if (string2ll(s, slen, &value)) {

        if (value >= 0 && value <= 12) {
            (*encoding) = ZIP_INT_IMM_MIN + value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            (*encoding) = ZIP_INT_8B;

        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            (*encoding) = ZIP_INT_16B;

        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            (*encoding) = ZIP_INT_24B;

        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            (*encoding) = ZIP_INT_32B;

        } else {
            (*encoding) = ZIP_INT_64B;
        }

        *v = value;

        return 1;
    }

    return 0;
}

// 将整数值保存到节点中
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {

    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT_16B) {
        i16 = value;
        memcpy(p, &i16, sizeof(i16));
        memrev16ifbe(p);
    } else if (encoding == ZIP_INT_24B) {
        i32 = value << 8;
        memrev32ifbe(&i32);
        memcpy(p, ((uint8_t*)&i32)+1, sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B) {
        i32 = value;
        memcpy(p, &i32, sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT_64B) {
        i64 = value;
        memcpy(p, &i64, sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        /* nothing */
    } else {
        assert(NULL);
    }
}

// 从节点中读取整数值
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
        ret = (encoding & ZIP_INT_IMM_MASK) -1;
    } else {
        assert(NULL);
    }

    return ret;
}

// 返回节点结构体
static zlentry zipEntry(unsigned char *p) {

    zlentry e;

    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    e.headersize = e.prevrawlensize + e.lensize;

    e.p = p;

    return e;
}

// 重分配压缩列表内存
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    zl = zrealloc(zl,len);

    ZIPLIST_BYTES(zl) = intrev32ifbe(len);
    zl[len-1] = ZIP_END;

    return zl;
}

// 连锁更新, p 之后的节点是否因前置节点长度改变而需要扩容
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char *np;
    zlentry cur, next;

    // 迭代 p 之后的所有节点
    while (p[0] != ZIP_END) {

        // cur节点的信息
        cur = zipEntry(p);
        rawlen = cur.headersize + cur.len;
        rawlensize = zipPrevEncodeLength(NULL, rawlen);

        // 是否存在下一个节点
        if (p[rawlen] == ZIP_END) break;

        // next节点的信息
        next = zipEntry(p+rawlen);

        // 不更新情况, 退出循环
        // 当前节点长度值完全等于next节点的前置节点长度值
        if (next.prevrawlen == rawlen) break;

        // cur节点所占字节数小于next.prevlensize
        // 需要扩容
        if (next.prevrawlensize < rawlensize) {
            
            // nextdiff
            extra = rawlensize - next.prevrawlensize;

            // 扩容
            offset = p-zl;
            zl = ziplistResize(zl, curlen+extra);
            p = zl+offset;

            // 记录 np 节点的指针
            np = p+rawlen;
            noffset = np-zl;

            // 非尾节点情况下, 更新尾节点偏移量
            if (zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) != np) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+extra);
            }

            // 移动原数据
            memmove(np+rawlensize, np+next.prevrawlensize, curlen-noffset-1-next.prevrawlensize);

            // 写入新前置节点长度值
            zipPrevEncodeLength(np, rawlen);

            // 处理下一个节点
            // 更新总长度, 添加扩容的长度
            p += rawlen;
            curlen += extra;

        // 无需扩容
        } else {

            // cur节点所占字节数大于next.prevlensize
            if (next.prevrawlensize > rawlensize) {
                zipPrevEncodeLengthForceLarge(p+rawlen, rawlen);

            // cur节点所占字节数等于next.prevlensize
            } else {
                zipPrevEncodeLength(p+rawlen, rawlen);
            }
            
            // 退出循环
            break;
        }
    }

    return zl;
}

// p 节点之前添加一个新节点
// 返回添加完成的列表指针
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), prevlen=0, reqlen, offset;
    int nextdiff = 0;
    long long value = 123456789;
    unsigned char encoding = 0;
    zlentry entry, tail;

    // 前置节点长度
    if (p[0] != ZIP_END) {
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 当前节点长度所占用的字节数
    if (zipTryEncoding(s, slen, &value, &encoding)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 前置节点长度占用字节数
    reqlen += zipPrevEncodeLength(NULL, prevlen);

    // 当前编码长度占用字节数
    reqlen += zipEncodeLength(NULL, encoding, slen);

    // 计算 nextdiff
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;
    
    // 扩容
    offset = p-zl;
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl+offset;

    // 添加新节点的位置
    // 中间添加
    if (p[0] != ZIP_END) {

        // 移动原数据
        memmove(p+reqlen, p-nextdiff, curlen-offset-1+nextdiff);

        // 更新前置节点长度到 nextdiff 所在的节点
        zipPrevEncodeLength(p+reqlen, reqlen);

        // 更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        // 尝试将 nextdiff 添加到尾节点偏移量
        tail = zipEntry(p+reqlen);
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }

    // 尾部添加
    } else {

        // 更新尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    // 连锁更新
    if (nextdiff != 0) {
        offset = p-zl;
        zl = __ziplistCascadeUpdate(zl, p+reqlen);
        p = zl+offset;
    }

    // 写入节点的前置节点长度, 编码, 节点值
    p += zipPrevEncodeLength(p, prevlen);
    p += zipEncodeLength(p, encoding, slen);
    if (ZIP_IS_STR(encoding)) {
        memcpy(p, s, slen);
    } else {
        zipSaveInteger(p, value, encoding);
    }

    // 更新节点数量
    ZIPLIST_INCR_LENGTH(zl, 1);

    return zl;
}

// 从 p 节点开始删除 num 个节点
static unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, int num) {

    size_t offset;
    unsigned int totlen, deleted = 0;
    int i, nextdiff = 0;
    zlentry first, tail;

    // 起始节点
    first = zipEntry(p);

    // 计算实际删除节点数
    for (i=0; i < num && p[0] != ZIP_END; i++) {
        p += zipRawEntryLength(p);
        deleted++;
    }

    // 计算实际删除的总字节数
    totlen = p - first.p;

    // 总字节数大于0
    if (totlen > 0) {

        // 中间删除
        if (p[0] != ZIP_END) {

            // 计算 nextdiff, 前移 p 节点,使其 prevlen 完整
            nextdiff = zipPrevLenByteDiff(p, first.prevrawlen);
            p -= nextdiff;

            // 写入前置节点长度
            zipPrevEncodeLength(p, first.prevrawlen);

            // 更新尾节点偏移量
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))-totlen);

            // nextdiff 不是尾节点的, 添加到尾节点偏移量
            tail = zipEntry(p);
            if (p[tail.headersize+tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
            }

            // 移动原数据
            memmove(first.p, p, intrev32ifbe(ZIPLIST_BYTES(zl))-(p-zl)-1);

        // 删到末端标识符了
        } else {

            // 更新尾节点偏移量为 first 的前置节点
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe((first.p-zl)-first.prevrawlen);
        }

        // 缩容
        offset = first.p-zl;
        zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl))-totlen+nextdiff);
        p = zl+offset;

        // 更新节点计数器
        ZIPLIST_INCR_LENGTH(zl, -deleted);
        // 连锁更新
        if (nextdiff != 0) {
            zl = __ziplistCascadeUpdate(zl, p);
        }
    }
    return zl;
}

/*--------------------- API --------------------*/

// 创建并返回一个空的压缩列表
unsigned char *ziplistNew(void) {

    unsigned int bytes = ZIPLIST_HEADER_SIZE+1;
    // 申请内存空间
    unsigned char *zl = zmalloc(bytes);

    // 设置属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
    ZIPLIST_LENGTH(zl) = 0;

    zl[bytes-1] = ZIP_END;

    return zl;
}

// 从列表左右两端添加新节点
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where) {

    unsigned char *p;

    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);

    return __ziplistInsert(zl, p, s, slen);
}

// 获取指定索引的节点
unsigned char *ziplistIndex(unsigned char *zl, int index) {
    unsigned char *p;
    zlentry entry;

    if (index < 0) {

        index = (-index)-1;
        p = ZIPLIST_ENTRY_TAIL(zl);

        if (p[0] != ZIP_END) {

            entry = zipEntry(p);

            while (entry.prevrawlen > 0 && index--) {
                p -= entry.prevrawlen;
                entry = zipEntry(p);
            }
        }
    } else {
        p = ZIPLIST_ENTRY_HEAD(zl);
        while (p[0]!=ZIP_END && index--) {
            p += zipRawEntryLength(p);
        }
    }

    return (p[0]==ZIP_END || index>0) ? NULL : p;
}

// 提取 p 的节点值
// 字符串, 提取字符串指针和字符长度 sstr, slen
// 整数, 提取整数到 sval
// 提取成功返回 1, 否则返回 0
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval) {

    zlentry entry;

    if (p==NULL || p[0]==ZIP_END) return 0;
    if (sstr) *sstr = NULL;

    entry = zipEntry(p);

    if (ZIP_IS_STR(entry.encoding)) {

        if (sstr) {
            *sstr = p+entry.headersize;
            *slen = entry.len;
        }
    } else {
        if (sval) {
            *sval = zipLoadInteger(p+entry.headersize, entry.encoding);
        }
    }

    return 1;
}

// 删除 p 节点, 并保持 p 的位置不变
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p) {

    size_t offset = *p-zl;
    zl = __ziplistDelete(zl, *p, 1);
    *p = zl+offset;

    return zl;
}

// 从 index 节点开始, 连续删除 num 个节点
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num) {

    // 获取起始节点
    unsigned char *p = ziplistIndex(zl, index);
    // 删除节点
    return (p == NULL) ? zl : __ziplistDelete(zl, p, num);
}

// 返回 p 指向节点的后置节点指针
// 没有节点了返回 NULL
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p) {
    ((void) zl);
    if (p[0]==ZIP_END)
        return NULL;

    p += zipRawEntryLength(p);

    if (p[0] == ZIP_END)
        return NULL;

    return p;
}

// 返回 p 的前置节点指针
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p) {

    zlentry entry;

    if (p == ZIPLIST_ENTRY_HEAD(zl)) {
        return NULL;
    } else if (p[0] == ZIP_END) {
        p = ZIPLIST_ENTRY_TAIL(zl);
        return (p[0] == ZIP_END) ? NULL : p;
    } else {
        entry = zipEntry(p);
        return p - entry.prevrawlen;
    }
}

// 节点值与 sstr 比对, 相同返回 1, 否则返回 0
unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen) {

    unsigned char sencoding = 0;
    long long zval, sval;

    if (p[0] == ZIP_END) return 0;

    // 节点信息
    zlentry entry = zipEntry(p);

    // 字符串
    if (ZIP_IS_STR(entry.encoding)) {

        // 长度相同
        if (entry.len == slen) {
            // 比对内容
            return memcmp(p+entry.headersize, sstr, slen) == 0;
        // 长度不同
        } else {
            return 0;
        }
    // 整型
    } else {

        // sstr 转为整数
        if (zipTryEncoding(sstr, slen, &sval, &sencoding)) {

            // 获取 p 的整数
            zval = zipLoadInteger(p+entry.headersize, entry.encoding);
            // 比对
            return zval == sval;
        }
    }

    return 0;
}

// 跳跃查找, 查找 vstr 相同的节点值
// 找到返回节点指针, 否则返回 NULL
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip) {

    int skipcnt = 0;
    unsigned char vencoding = 0;
    long long vll = 0;

    while (p[0] != ZIP_END) {

        unsigned int prevlensize, lensize, len, encoding;
        unsigned char *q;

        // 节点信息
        ZIP_DECODE_PREVLENSIZE(p, prevlensize);
        ZIP_DECODE_LENGTH(p+prevlensize, encoding, lensize, len);

        // 移动到节点值的指针
        q = p + prevlensize + lensize;

        if (skipcnt == 0) {

            // 字符串
            if (ZIP_IS_STR(encoding)) {

                // 比对
                if (len == vlen && memcmp(q, vstr, vlen)==0)
                    return p;
            // 整型
            } else {
                
                // 将 vstr 转为整数, 只转换一次
                if (vencoding == 0) {
                    if (!zipTryEncoding(vstr,vlen,&vll,&vencoding)) {
                        vencoding = UCHAR_MAX;
                    }
                    assert(vencoding);
                }

                // 获取节点的整数
                if (vencoding != UCHAR_MAX) {
                    long long ll = zipLoadInteger(q, encoding);
                    // 比对
                    if (vll == ll) {
                        return p;
                    }
                }
                
                
            }

            // 未找到节点, 重新设置跳跃节点数
            skipcnt = skip;
        } else {
            skipcnt--;
        }

        p = q + len;
    }

    return NULL;
}

// 返回压缩列表的节点数量
unsigned int ziplistLen(unsigned char *zl) {

    unsigned int len = 0;

    if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) {

        len = intrev16ifbe(ZIPLIST_LENGTH(zl));

    } else {
        unsigned char *p = zl + ZIPLIST_HEADER_SIZE;
        while (*p != ZIP_END) {
            p += zipRawEntryLength(p);
            len++;
        }

        if (len < UINT16_MAX) ZIPLIST_LENGTH(zl) = intrev32ifbe(len);   
    }

    return len;
}

// 压缩列表占用内存字节数
size_t ziplistBlobLen(unsigned char *zl) {
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}

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

long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

void stress(int pos, int num, int maxsize, int dnum) {
    int i,j,k;
    unsigned char *zl;
    char posstr[2][5] = { "HEAD", "TAIL" };
    long long start;
    for (i = 0; i < maxsize; i+=dnum) {
        zl = ziplistNew();
        for (j = 0; j < i; j++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,ZIPLIST_TAIL);
        }

        /* Do num times a push+pop from pos */
        start = usec();
        for (k = 0; k < num; k++) {
            zl = ziplistPush(zl,(unsigned char*)"quux",4,pos);
            zl = ziplistDeleteRange(zl,0,1);
        }
        printf("List size: %8d, bytes: %8d, %dx push+pop (%s): %6lld usec\n",
            i,intrev32ifbe(ZIPLIST_BYTES(zl)),num,posstr[pos],usec()-start);
        zfree(zl);
    }
}

void pop(unsigned char *zl, int where) {
    unsigned char *p, *vstr;
    unsigned int vlen;
    long long vlong;
    int index;

    index = (where == ZIPLIST_HEAD) ? 0 : -1;
    p = ziplistIndex(zl,index);
    if (ziplistGet(p,&vstr,&vlen,&vlong)) {
        if (where == ZIPLIST_HEAD)
            printf("Pop head: ");
        else
            printf("Pop tail: ");

        if (vstr) {
            if (vlen && fwrite(vstr,vlen,1,stdout) == 0) perror("fwrite");
        } else {
            printf("%lld", vlong);
        }

        printf("\n");
        ziplistDeleteRange(zl, index, 1);
    } else {
        printf("ERROR: Could not pop\n");
        exit(1);
    }
}

int randstring(char *target, unsigned int min, unsigned int max) {
    int p = 0;
    int len = min+rand()%(max-min+1);
    int minval, maxval;
    switch(rand() % 3) {
    case 0:
        minval = 0;
        maxval = 255;
    break;
    case 1:
        minval = 48;
        maxval = 122;
    break;
    case 2:
        minval = 48;
        maxval = 52;
    break;
    default:
        assert(NULL);
    }

    while(p < len)
        target[p++] = minval+rand()%(maxval-minval+1);
    return len;
}

void verify(unsigned char *zl, zlentry *e) {
    int i;
    int len = ziplistLen(zl);
    zlentry _e;

    for (i = 0; i < len; i++) {
        memset(&e[i], 0, sizeof(zlentry));
        e[i] = zipEntry(ziplistIndex(zl, i));

        memset(&_e, 0, sizeof(zlentry));
        _e = zipEntry(ziplistIndex(zl, -len+i));

        assert(memcmp(&e[i], &_e, sizeof(zlentry)) == 0);
    }
}

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

    // zl = createIntList();
    // ziplistRepr(zl);

    // zl = createList();
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_HEAD);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

    // pop(zl,ZIPLIST_TAIL);
    // ziplistRepr(zl);

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
    //     ziplistRepr(zl);
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