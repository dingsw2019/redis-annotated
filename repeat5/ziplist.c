#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"


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

// 连锁更新,新增或删除节点时, 会影响后置节点的prevlensize
// 如果新增导致当前 prevlensize 无法满足, 需要扩容
// 删除不会更改 prevlensize 空间大小, 只更改长度
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), offset, noffset;
    unsigned int rawlensize, rawlen;
    unsigned char *np;
    int extra = 0;

    zlentry cur, next;

    while (p[0] != ZIP_END) {
        
        // 当前节点信息
        cur = zipEntry(p);
        // 节点长度
        rawlen = cur.headersize + cur.len;
        // 节点长度占的字节数
        rawlensize = zipPrevEncodeLength(NULL,rawlen);

        // 下一个节点是否存在
        if (p[rawlen] == ZIP_END) break;

        // 获取后置节点信息
        next = zipEntry(p+rawlen);

        // 后置节点的 prevlensize 不满足当前长度存储
        if (next.prevrawlensize < rawlensize) {
            
            // 计算差值
            extra = rawlensize - next.prevrawlensize;

            // 记录当前节点偏移量, 扩容
            offset = p - zl;
            ziplistResize(zl, curlen+extra);
            p = zl + offset;

            // 计算后置节点偏移量
            np = p+rawlen;
            noffset = np - zl;

            // 后置节点不是尾节点, 更新尾节点偏移量
            if (zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) != np) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+extra);
            }

            // 移动原数据
            memmove(np+rawlen, 
                    np+next.prevrawlensize, 
                    curlen-noffset-next.prevrawlensize-1);

            // 更新前置节点长度
            zipPrevEncodeLength(np, rawlen);

            // 处理下一个节点
            p += rawlen;
            curlen += extra;
        } else {

            if (next.prevrawlensize > rawlensize) {
                zipPrevEncodeLengthForceLarge(p+rawlen, rawlen);
            } else {
                zipPrevEncodeLength(p+rawlen, rawlen);
            }

            break;
        }
    }

    // 迭代到最后一个节点 myerr
    // while (p[0] != ZIP_END) {

    //     unsigned int prevlensize, prevlen, encoding, lensize, len;
    //     // 当前节点信息
    //     ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
    //     ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);

    //     // 下一个节点是否存在
    //     np = p + zipRawEntryLength(p);
    //     if (np[0] == ZIP_END) break;

    //     // 获取下一个节点
    //     next = zipEntry(np);

    //     // 当前节点的长度与后置节点记录的长度相同, 退出
    //     if (prevlen == next.prevrawlen)
    //         break;

    //     // 计算两节点的 prevlen 差值
    //     nextdiff = prevlensize - next.prevrawlensize;

    //     // nextdiff > 0, 需要扩容
    //     if (nextdiff > 0) {

    //         // 
    //         np -= nextdiff;

    //         // 扩容
    //         offset = np - zl;
    //         zl = ziplistResize(zl, curlen+nextdiff);
    //         np = zl + offset;

    //         // 移动数据
    //         memmove(np,np+nextdiff,curlen-offset-1+nextdiff);
    //         // 前置节点长度写入到后置节点
    //         // 如果不是尾节点, 更新尾节点偏移量
            

    //         // 移动 p 指针到下一个节点
    //         // 总长度更新
    //         p = np;
    //         curlen += nextdiff;

    //     // nextdiff <= 0, 只修改前置长度, 然后退出
    //     } else {

    //         // < 0
    //         if (nextdiff < 0) {

    //             // 将一个小于 5 字节的长度写入 5 字节的空间中
    //         // == 0
    //         } else {
    //             // 修改前置节点长度
    //             zipPrevEncodeLength(np, prevlen);
    //         }
    //     }
    // }

    return zl;
}

// 在 p 指向的节点前添加新节点
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {

    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen, prevlen, offset;
    unsigned char encoding = 0;
    int nextdiff;
    long long value = 123456789;

    zlentry entry, tail;

    // 获取前置节点的长度
    if (p[0] != ZIP_END) {

        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {

        // 尾节点来作为前置节点
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 计算节点值所占用的字节数
    // 尝试将字符型数值转换成整数, 并获取编码和长度
    if (zipTryEncoding(s, slen, &value, &encoding)) {
        reqlen = zipIntSize(encoding);
    } else {
        reqlen = slen;
    }

    // 计算前置节点长度占用字节数
    // reqlen += zipPrevEncodeLength(NULL, entry.prevrawlen); myerr
    reqlen += zipPrevEncodeLength(NULL, prevlen);

    // 计算编码占用字节数
    reqlen += zipEncodeLength(NULL, encoding, slen);

    // 后置节点的prevlensize是否足够
    // 计算差值
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

    // 扩大列表字节数
    offset = p - zl;
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl + offset;

    // myerr, 缺少 是否尾节点的判断
    if (p[0] != ZIP_END) {
        // 移动原数据, 给新节点腾出空间
        // memmove(p+reqlen,p-nextdiff,curlen-(p-zl)); myerr
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        // 更新后置节点的prevlen
        zipPrevEncodeLength(p+reqlen, reqlen);

        // 更新列表的尾节点偏移量
        ZIPLIST_TAIL_OFFSET(zl) = 
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        // 如果后置节点不是尾节点
        // 那么尾节点偏移量需加上 nextdiff
        tail = zipEntry(p+reqlen);
        // if (p[tail.headersize+tail.len] != ZIP_END) { myerr
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) = 
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }


    } else {
        ZIPLIST_TAIL_OFFSET(zl) = 
            // intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+prevlen); myerr
            intrev32ifbe(p-zl);
    }

    // 连锁更新
    if (nextdiff != 0) {
        offset = p -zl;
        zl = __ziplistCascadeUpdate(zl, p);
        p = zl + offset;
    }

    // 写入前置节点长度
    p += zipPrevEncodeLength(p, prevlen);

    // 写入节点编码
    p += zipEncodeLength(p, encoding, slen);

    // 写入节点值
    if (ZIP_IS_STR(encoding)) {
        memcpy(p,s,slen);
    } else {
        zipSaveInteger(p, value, encoding);
    }

    // 更新节点计数器
    ZIP_INCR_LENGTH(zl,1);

    return zl;
}

/*--------------------- API --------------------*/
// 创建并返回一个空列表
unsigned char *ziplistNew(void) {

    unsigned int len = ZIPLIST_HEADER_SIZE+1;
    // 申请内存
    unsigned char *zl = zmalloc(len);

    // 初始化属性
    ZIPLIST_BYTES(zl) = len;
    ZIPLIST_TAIL_OFFSET(zl) = ZIPLIST_HEADER_SIZE;
    ZIPLIST_LENGTH(zl) = 0;

    zl[len-1] = ZIP_END;

    return zl;
}

// 向两端添加节点, 成功返回列表地址, 否则返回 NULL
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where) {

    unsigned char *p;

    // 通过 where 确定节点
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_TAIL(zl);

    // 添加节点
    return __ziplistInsert(zl,p,s,slen);
}



/*--------------------- debug --------------------*/
#include <sys/time.h>
#include <assert.h>
#include "adlist.h"
#include "sds.h"

// unsigned char *createList() {
//     unsigned char *zl = ziplistNew();
//     zl = ziplistPush(zl, (unsigned char*)"foo", 3, ZIPLIST_TAIL);
//     zl = ziplistPush(zl, (unsigned char*)"quux", 4, ZIPLIST_TAIL);
//     zl = ziplistPush(zl, (unsigned char*)"hello", 5, ZIPLIST_HEAD);
//     zl = ziplistPush(zl, (unsigned char*)"1024", 4, ZIPLIST_TAIL);
//     return zl;
// }

// unsigned char *createIntList() {
//     unsigned char *zl = ziplistNew();
//     char buf[32];

//     sprintf(buf, "100");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     sprintf(buf, "128000");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     sprintf(buf, "-100");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
//     sprintf(buf, "4294967296");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
//     sprintf(buf, "non integer");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     sprintf(buf, "much much longer non integer");
//     zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
//     return zl;
// }

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

// void ziplistRepr(unsigned char *zl) {
//     unsigned char *p;
//     int index = 0;
//     zlentry entry;

//     printf(
//         "{total bytes %d} "
//         "{length %u}\n"
//         "{tail offset %u}\n",
//         intrev32ifbe(ZIPLIST_BYTES(zl)),
//         intrev16ifbe(ZIPLIST_LENGTH(zl)),
//         intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
//     p = ZIPLIST_ENTRY_HEAD(zl);
//     while(*p != ZIP_END) {
//         entry = zipEntry(p);
//         printf(
//             "{"
//                 "addr 0x%08lx, "
//                 "index %2d, "
//                 "offset %5ld, "
//                 "rl: %5u, "
//                 "hs %2u, "
//                 "pl: %5u, "
//                 "pls: %2u, "
//                 "payload %5u"
//             "} ",
//             (long unsigned)p,
//             index,
//             (unsigned long) (p-zl),
//             entry.headersize+entry.len,
//             entry.headersize,
//             entry.prevrawlen,
//             entry.prevrawlensize,
//             entry.len);
//         p += entry.headersize;
//         if (ZIP_IS_STR(entry.encoding)) {
//             if (entry.len > 40) {
//                 if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
//                 printf("...");
//             } else {
//                 if (entry.len &&
//                     fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
//             }
//         } else {
//             printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
//         }
//         printf("\n");
//         p += entry.len;
//         index++;
//     }
//     printf("{end}\n\n");
// }

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