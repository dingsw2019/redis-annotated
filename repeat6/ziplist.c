#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
// #include "redisassert.h"

/*--------------------- private --------------------*/


// 从 p 中获取存储"前置节点长度"所需字节大小
#define ZIP_DECODE_PREVLENSIZE(p, prevlensize) do{      \
    if ((p)[0] < ZIP_BIGLEN) {                          \
        (prevlensize) = 1;                              \
    } else {                                            \
        (prevlensize) = 5;                              \
    }                                                   \
}while(0);

// 从 p 中获取前置节点信息
#define ZIP_DECODE_PREVLEN(p,prevlensize, prevlen) do {         \
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);                     \
                                                                \
    /* 按字节获取前置节点长度 */                                  \
    if ((prevlensize) == 1) {                                   \
        (prevlen) = p[0];                                       \
    } else if ((prevlensize) == 5){                             \
        /*assert(sizeof(prevlensize) == 4);*/                   \
        memcpy(&(prevlen), ((char*)(p))+1, 4);                  \
        memrev32ifbe(&(prevlen));                               \
    }                                                           \
} while(0);

// 获取编码方式
#define ZIP_ENTRY_ENCODING(p, encoding) do{                    \
    (encoding) = (p)[0];                                        \
    /* if (encoding < ZIP_STR_MASK) encoding &= ZIP_STR_MASK myerr*/      \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK;       \
}while(0)

// static unsigned int zipIntSize(unsigned int encoding) { myerr
static unsigned int zipIntSize(unsigned char encoding) {
    switch (encoding)
    {
    case ZIP_INT8B: return 1;
    case ZIP_INT16B: return 2;
    case ZIP_INT24B: return 3;
    case ZIP_INT32B: return 4;
    case ZIP_INT64B: return 8;
    default:return 0;
    }

    // assert(NULL);
    return 0;
}

#define ZIP_DECODE_LENGTH(p, encoding, lensize, len) do{        \
                                                                \
    /*ZIP_DECODE_ENCODING(p, encoding); myerr */                          \
    ZIP_ENTRY_ENCODING((p), (encoding));                           \
                                                                \
    /* 字符数组 */                                               \
    if ((encoding) < ZIP_STR_MASK) {                            \
                                                                \
        if ((encoding) == ZIP_STR06B) {                         \
            (lensize) = 1;                                      \
            (len) = (p)[0] & 0x3f;                              \
        } else if ((encoding) == ZIP_STR14B) {                  \
            (lensize) = 2;                                      \
            (len) = (((p)[0] & 0x3f) << 8) | (p)[1];            \
        } else if ((encoding) == ZIP_STR32B) {                  \
            (lensize) = 5;                                      \
            (len) = ((p)[1] << 24) |                            \
                    ((p)[2] << 16) |                            \
                    ((p)[3] <<  8) |                            \
                    ((p)[4]);                                   \
        } else {                                                \
            /*assert(NULL);*/                                   \
        }                                                       \
                                                                \
    /* 整数 */                                                  \
    } else {                                                    \
        (lensize) = 1;                                          \
        (len) = zipIntSize(encoding);                           \
    }                                                           \
}while(0);


// 从 p 指针中按读取 zlentry 结构的信息, 并返回 zlentry
// static zlentry zipEntry(unsigned char p) { myerr
static zlentry zipEntry(unsigned char *p) {

    zlentry e;

    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    e.headersize = e.prevrawlensize + e.lensize;

    e.p = p;

    return e;
}

// 获取节点长度(字节)
static unsigned int zipRawEntryLength(unsigned char *p) {

    unsigned int prevlensize,encoding,lensize,len;
    // 获取存储前置节点的长度
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    // 获取存储当前节点的长度
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
    // 返回
    return prevlensize+lensize+len;
}

// 将字符串型数值转换成整数
// 转换成功返回 1, 否则返回 0
static int zipTryEncoding(unsigned char *s, unsigned int slen, long long *v, unsigned char *encoding) {

    // myerr 缺少
    long long value;
    // 值太大或太小, 不处理
    if (slen >= 32 || slen == 0)
        return 0;

    // 转换
    // if (string2ll(s,slen,value)) { myerr
    if (string2ll((char*)s,slen,&value)) {

        // 根据值的大小判断 编码类型
        if (value >= 0 && value <= 12) {
            *encoding = ZIP_INT_IMM_MIN+value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT32B;
        } else {
            *encoding = ZIP_INT64B;
        }

        *v = value;

        return 1;
    }

    // 转换失败
    return 0;
}

// 按指定编码方式获取节点值
static int64_t zipLoadInteger(unsigned char *p, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64, len = 0;

    if (encoding == ZIP_INT8B) {
        len = ((uint8_t*)p)[0];
    } else if (encoding == ZIP_INT16B) {
        memcpy(&i16,p,sizeof(i16));
        memrev16ifbe(&i16);
        len = i16;
    } else if (encoding == ZIP_INT24B) {
        i32 = 0; //myerr:缺少
        memcpy(((uint8_t*)&i32)+1,p,sizeof(i32)-sizeof(uint8_t));
        memrev32ifbe(&i32);
        // len = i32; myerr
        len = i32 >> 8;
    } else if (encoding == ZIP_INT32B) {
        memcpy(&i32,p,sizeof(i32));
        memrev32ifbe(&i32);
        len = i32;
    } else if (encoding == ZIP_INT64B) {
        memcpy(&i64,p,sizeof(i64));
        memrev64ifbe(&i64);
        len = i64;
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        // len = ((uint8_t*)p)[0] & 0xf0; myerr
        len = (encoding & ZIP_INT_IMM_MASK) -1;
    } else {
        // assert(NULL);
    }

    return len;
}


// 返回前置节点长度编码
// 如果 p 存在, 写入长度编码
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {

    // 仅返回编码长度,不写入
    if (p == NULL) {
        return len < ZIP_BIGLEN ? 1 : sizeof(len)+1;
    } else {

        // 1 字节
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;
        // 5 字节
        } else {
            // 1 字节标识符
            p[0] = ZIP_BIGLEN;
            // 存长度
            memcpy(p+1,&len,sizeof(len));
            memrev32ifbe(p+1);
            return sizeof(len)+1;
        }
    }
}

// 返回当前节点长度编码
// 如果 p 存在, 写入长度编码
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {

    unsigned char len = 1, buf[5];

    // 字符串
    if (ZIP_IS_STR(encoding)) {

        // 1 字节
        if (rawlen <= 0x3f) {
            if (!p) return len;
            buf[0] = ZIP_STR06B | rawlen;
        // 2 字节
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        // 5 字节
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >>  8) & 0xff;
            buf[4] = rawlen & 0xff;
        }
    // 整数
    } else {
        // 保存节点值长度
        if (!p) return len;
        buf[0] = encoding;
    }

    memcpy(p,buf,len);

    return len;
}

// 新节点长度 len 与其后置节点 p 的前置节点长度的差值
static unsigned int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {

    unsigned prevlensize;

    ZIP_DECODE_PREVLENSIZE(p,prevlensize);

    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

// 扩容或缩容, 重分配列表内容空间
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    zl = zrealloc(zl,len);

    // 更新总长度
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);
    // 更新末端标识
    zl[len-1] = ZIP_END;

    return zl;
}

// 整数型节点值写入节点
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {

    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT24B) {
        i32 = value << 8;
        memrev32ifbe(&i32);
        // memcpy(p,((uint8_t)&i32)+1,sizeof(i32)-sizeof(uint8_t)); myerr
        memcpy(p,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        
    } else {
        // assert(NULL);
    }
}

// 添加新节点, 新节点长度编码超过其后置节点的prevlensize, 就要扩充 prevlensize 的字节大小
// 更新了 next 的prevlensize, 可能还需要更新 next 的下一个节点的 prevlensize, 依次类推
// 删除节点同理, 但是不会扩容。只变更 prevlen
// 返回更改后的压缩列表
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char *np;
    zlentry cur, next;

    // 迭代更新
    while (p[0] != ZIP_END) {

        // 获取当前节点的基础数据
        cur = zipEntry(p);

        // 计算当前节点占用的字节数
        rawlen = cur.headersize + cur.len;

        rawlensize = zipPrevEncodeLength(NULL,rawlen);

        // 是否存在下一个节点
        // 不存在说明不会出现连锁更新了, 跳出循环
        if (p[rawlen] == ZIP_END)  break;

        // 获取下一个节点
        next = zipEntry(p+rawlen);

        // next 的前置节点长度完全等于 cur 的长度
        // 那就啥也不用更新了, see goodbye
        if (rawlen == next.prevrawlen) break;

        // cur 的字节数大于 next 的前置节点字节数
        if (next.prevrawlensize < rawlensize) {

            // 计算字节数差值
            extra = rawlensize - next.prevrawlensize;

            offset = p - zl;
        
            // 扩容内存空间
            zl = ziplistResize(zl, curlen+extra);
            p = zl + offset;

            np = p+rawlen;
            noffset = np - zl;

            // 如果 next 不是尾节点
            // 那么更新尾节点的偏移量
            if (zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) != np) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+extra);
            }

            // 移动 cur 之后的数据
            // 给 next 的 prevlensize 腾空间
            // memmove(np+rawlensize, np+next.prevrawlensize, curlen-noffset-1-extra); myerr
            memmove(np+rawlensize, np+next.prevrawlensize, curlen-noffset-1-next.prevrawlensize);

            // 将前置节点长度写入 next 节点
            zipPrevEncodeLength(np,rawlen);

            // 处理下一个节点
            p = p+rawlen;
            curlen += extra;
        // 
        } else {

            // cur 的字节数小于 next 的前置节点字节数
            // 只更新前置节点长度
            if (next.prevrawlensize > rawlensize) {
                // zipPrevEncodeLengthForceLarge(p+rawlen,rawlen);
            }

            // cur 的字节数等于 next 的前置节点字节数
            // 只更新前置节点长度
            if (next.prevrawlensize == rawlensize) {
                // zipPrevEncodeLength(p, rawlen); myerr
                zipPrevEncodeLength(p+rawlen, rawlen);
            }

            // 跳出循环
            break;
        }
    }   

    return zl;
}

// 在 p 指向的位置上添加节点
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), offset, reqlen, prevlen = 0;
    // int nextdiff,encoding = 0, value; myerr
    int nextdiff = 0;
    unsigned char encoding = 0;
    long long value = 123456789;

    zlentry entry, tail;

    // 计算前置节点的长度
    if (p[0] != ZIP_END) {
        // 走到这里说明 p 之后有节点
        // 这个节点就是新节点的前置节点,获取它的长度
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        // 判断列表是否无节点
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {

            // 新节点的前置节点是尾节点
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 尝试将 s 转换成整数,如果它是字符串数字的话
    // 获取存储节点值的内存大小
    if (zipTryEncoding(s,slen,&value,&encoding)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算编码前置节点长度的大小
    reqlen += zipPrevEncodeLength(NULL, prevlen);
    // 计算编码当前节点长度的大小
    reqlen += zipEncodeLength(NULL, encoding, slen);

    // 新节点的后置节点的prev 是否足够
    // nextdiff = reqlen < ZIP_BIGLEN ? 0 : zipPrevLenByteDiff(p, reqlen); myerr
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

    // 扩容
    offset = p - zl;
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl + offset;

    // 如果新节点不是添加到尾节点
    if (p[0] != ZIP_END) {

        // 移动原有节点, 给新节点腾出空间
        // memmove(p+reqlen,p-nextdiff,offset-1+nextdiff); myerr
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        // 更新后置节点的前置长度编码 myerr:缺少
        zipPrevEncodeLength(p+reqlen,reqlen);

        // 更新到达尾节点的偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);
        // 如果新节点之后有多个节点
        // 尾节点偏移量还要加上 nextdiff
        // tail = p+reqlen; myerr
        tail = zipEntry(p+reqlen);
        // if (p[reqlen+tail.prevrawlensize+tail.lensize] != ZIP_END) { myerr
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) = 
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }

    // 新节点是尾节点
    } else {   
        // 更新总长度
        // ZIPLIST_BYTES(zl) = 
            // intrev32ifbe(intrev32ifbe(ZIPLIST_BYTES(zl))+reqlen);

        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    // 执行连锁更新
    if (nextdiff != 0) {
        offset = p - zl;
        zl = __ziplistCascadeUpdate(zl, p+reqlen);
        p = zl + offset;
    }

    // 前置节点长度写入
    p += zipPrevEncodeLength(p, prevlen);

    // 当前节点长度写入
    p += zipEncodeLength(p, encoding, slen);

    // 节点值写入
    if (ZIP_IS_STR(encoding)) {
        // memcpy(p,s,reqlen); myerr
        memcpy(p,s,slen);
    } else {
        // zipSaveInteger(p,value); myerr
        zipSaveInteger(p,value,encoding);
    }

    // 更新节点计数器
    // ZIPLIST_LENGTH(zl) = 
        // intrev32ifbe(intrev32ifbe(ZIPLIST_LENGTH(zl))+1);
    ZIPLIST_INCR_LENGTH(zl,1);

    return zl;
}

/*--------------------- API --------------------*/

// 创建并返回一个空的压缩列表
unsigned char *ziplistNew(void) {

    // size_t len = ZIPLIST_HEADER_SIZE+1; myerr
    unsigned int len = ZIPLIST_HEADER_SIZE+1;

    // 申请内存空间
    unsigned char *zl = zmalloc(len);

    // 初始化属性
    ZIPLIST_BYTES(zl) = len;
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);// myerr:缺少
    ZIPLIST_LENGTH(zl) = 0;// myerr:缺少

    zl[len-1] = ZIP_END;

    return zl;
}

// 从两端添加节点, 返回添加后的列表
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int direction) {

    unsigned char *p;
    // 根据添加方向确定头节点还是尾节点
    p = (direction == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_TAIL(zl);

    // 添加节点
    return __ziplistInsert(zl, p, s, slen);
}

/*--------------------- debug --------------------*/
#include <sys/time.h>
#include <assert.h>
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

// void pop(unsigned char *zl, int where) {
//     unsigned char *p, *vstr;
//     unsigned int vlen;
//     long long vlong;

//     p = ziplistIndex(zl,where == ZIPLIST_HEAD ? 0 : -1);
//     if (ziplistGet(p,&vstr,&vlen,&vlong)) {
//         if (where == ZIPLIST_HEAD)
//             printf("Pop head: ");
//         else
//             printf("Pop tail: ");

//         if (vstr)
//             if (vlen && fwrite(vstr,vlen,1,stdout) == 0) perror("fwrite");
//         else
//             printf("%lld", vlong);

//         printf("\n");
//         ziplistDeleteRange(zl,-1,1);
//     } else {
//         printf("ERROR: Could not pop\n");
//         exit(1);
//     }
// }

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