/**
 * 
 * 
 * 为什么用 unsigned char, 详见以下链接
 * @link https://www.runoob.com/cprogramming/c-function-printf.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "redisassert.h"
#include <sys/time.h>


/*--------------------- private --------------------*/

// 迭代方向
#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

/**
 * ziplist 标识符
 */
// 末端标识符, 尾节点之后的 1 字节长度的内容
#define ZIP_END 255
// 5 字节长 长度标识符
#define ZIP_BIGLEN 254

/**
 * 字符串编码和整数编码的掩码
 */
#define ZIP_STR_MASK 0xc0
#define ZIP_INT_MASK 0x30

/**
 * 节点编码类型为字符串 ??咋用
 * 字符串编码的类型
 */
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)

/**
 * 整数编码类型
 */
// (1100 0000) | (0000 0000) => 1100 0000 => 192
#define ZIP_INT_16B (0xc0 | 0<<4)
// (1100 0000) | (0001 0000) => 1101 0000 => 208
#define ZIP_INT_32B (0xc0 | 1<<4)
// (1100 0000) | (0010 0000) => 1110 0000 => 224
#define ZIP_INT_64B (0xc0 | 2<<4)
// (1100 0000) | (0011 0000) => 1111 0000 => 240
#define ZIP_INT_24B (0xc0 | 3<<4)
// 0xfe => ‭11111110‬ => 254
#define ZIP_INT_8B 0xfe

/**
 * ziplist 属性宏
 */
// ziplist 的 bytes 属性, 该属性记录了整个 ziplist 所占用的内存字节数
// 用于取出 bytes 属性的值, 或者为 bytes 赋新值
#define ZIPLIST_BYTES(zl) (*((uint32_t*)(zl)))
// ziplist 的 tail 属性, 该属性记录了从列表起始地址到表尾节点的距离
// 用于取出 offset 属性的值, 或者为 offset 赋新值
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// ziplist 的 length 属性, 该属性记录了列表的节点数量
// 用于取出 length 属性的值, 或者为 length 赋新值
#define ZIPLIST_LENGTH(zl) (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
// ziplist 表头的大小
#define ZIPLIST_HEADER_SIZE (sizeof(uint32_t)*2+sizeof(uint16_t))
// 返回指定列表的第一个节点(起始位置)的指针
#define ZIPLIST_ENTRY_HEAD(zl) ((zl)+ZIPLIST_HEADER_SIZE)
// 返回指定列表的最后一个节点(起始位置)的指针
#define ZIPLIST_ENTRY_TAIL(zl) ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
// 返回指定列表的 ZIP_END(起始位置)的指针
#define ZIPLIST_ENTRY_END(zl) ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

// ziplist 的节点结构
typedef struct zlentry {

    // prevrawlen : 前置节点的长度
    // prevrawlensize : 编码 prevrawlen 所需的字节大小
    unsigned int prevrawlen, prevrawlensize;

    // len : 当前节点值的长度
    // lensize : 编码 len 所需的字节大小
    unsigned int len, lensize;

    // 当前节点 header 的大小
    // 等于 prevrawlensize + lensize
    unsigned int headersize;

    // 当前节点值使用的编码类型
    unsigned char encoding;

    // 指向当前节点值的指针
    unsigned char *p;

} zlentry;

/**
 * 解码 ptr 指针
 * 取出编码前置节点长度所需的字节数, 并将它保存到 prevlensize 变量中
 * 
 * T = O(1)
 */
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do {   \
    if ((ptr)[0] < ZIP_BIGLEN) {                        \
        (prevlensize) = 1;                              \
    } else {                                            \
        (prevlensize) = 5;                              \
    }                                                   \
} while(0);

/**
 * 解码 ptr 指针
 * 取出"编码前置节点长度所需的字节数", 保存到 prevlensize 中
 * 取出"前置节点长度", 保存到 prevlen 中
 *
 * T = O(1)
 */
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do{                       \
                                                                                \
    /* 计算编码前置节点的字节数 */                                                \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);                                   \
                                                                                \
    /* 如果是 1 字节, 取 1 字节的内容就是前置节点的长度 */                          \
    if ((prevlensize) == 1) {                                                   \
        (prevlen) = (ptr)[0];                                                   \
    }                                                                           \
    /* 如果是 5 字节, 需跳过 1 字节(第一个字节被设置为 0xFE,十进制值254) */         \
    /* 后 4 字节才是前置节点的长度 */                                             \
    else if ((prevlensize) == 5){                                               \
        /*assert(sizeof((prevlensize)) == 4);*/                                     \
        memcpy(&(prevlen), ((char*)(ptr))+1, 4);                                \
        memrev32ifbe(&prevlen);                                                 \
    }                                                                           \
}while(0);

/**
 * 从 ptr 中取出节点值的编码类型, 并保存到 encoding 中
 * T = O(1)
 */
#define ZIP_ENTRY_ENCODING(ptr, encoding) do {  \
    (encoding) = (ptr[0]);  \
    if (encoding < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK;    \
}while(0)

/**
 * 返回保存 encoding 编码的值所需的字节数量
 * 
 * T = O(1)
 */
static unsigned int zipIntSize(unsigned char encoding) {
    
    switch (encoding) {
    case ZIP_INT_8B: return 1;
    case ZIP_INT_16B: return 2;
    case ZIP_INT_24B: return 3;
    case ZIP_INT_32B: return 4;
    case ZIP_INT_64B: return 8;
    default: return 0; /* 4 bit*/
    }

    // assert(NULL);
    return 0;
}

/**
 * 解码 ptr 指针, 取出列表节点的属性, 并保存在以下变量中
 * 
 * - encoding 保存节点值的编码类型
 * - lensize 保存编码节点长度所需的字节数
 * - len 保存节点的长度
 * 
 * T = O(1)
 */
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do {         \
                                                                    \
    /* 取出节点值的编码类型 */                                        \
    ZIP_ENTRY_ENCODING((ptr), (encoding));                          \
                                                                    \
    /* 字符串编码 */                                                 \
    if ((encoding) < ZIP_STR_MASK) {                                \
        if ((encoding) == ZIP_STR_06B) {                            \
            (lensize) = 1;                                          \
            (len) = (ptr)[0] & 0x3f;                                \
        } else if ((encoding) == ZIP_STR_14B) {                     \
            (lensize) = 2;                                          \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];            \
        } else if ((encoding) == ZIP_STR_32B) {                     \
            (lensize) = 5;                                          \
            (len) = ((ptr)[1] << 24) |                              \
                    ((ptr)[2] << 16) |                              \
                    ((ptr)[3] <<  8) |                              \
                    ((ptr)[4]);                                     \
        } else {                                                    \
            /*assert(NULL);*/                                           \
        }                                                           \
    /* 整数编码 */                                                   \
    } else {                                                        \
        (lensize) = 1;                                              \
        (len) = zipIntSize(encoding);                               \
    }                                                               \
                                                                    \
}while(0);

/**
 * 将 p 所指向的列表节点的信息全部保存到 zlentry 中, 并返回该 zlentry
 * 
 * T = O(1)
 */
static zlentry zipEntry(unsigned char *p) {
    zlentry e;

    // e.prevrawlensize 保存编码前一个节点的长度所需的字节数
    // e.prevrawlen 保存着前一个节点的长度
    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    // p + e.prevrawlensize, 当前节点跳到编码起始地址
    // e.encoding 保存节点值的编码类型
    // e.lensize 保存着编码节点值长度所需的节点数
    // e.len 保存着节点值的长度
    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    // 计算头节点的字节数
    e.headersize = e.prevrawlensize + e.lensize;

    // 记录指针
    e.p = p;

    return e;
}


/*--------------------- API --------------------*/
/**
 * 创建并返回一个空的压缩列表
 */
unsigned char *ziplistNew(void) {

    // 空列表的总字节数
    // ZIPLIST_HEADER_SIZE 是表头的大小
    // +1 是 ZIP_END 的大小
    unsigned int bytes = ZIPLIST_HEADER_SIZE+1;

    // 为表头和ZIP_END 申请内存空间
    unsigned char *zl = zmalloc(bytes);

    // 初始化属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
    ZIPLIST_LENGTH(zl) = 0;

    // 设置表末端标识符
    zl[bytes-1] = ZIP_END;

    // 返回
    return zl;
}

/**
 * 将长度为 slen 的字符串 s 添加到压缩列表中
 * 
 * where 表示添加方向
 * 如果等于 ZIPLIST_HEAD , 从表头插入新节点
 * 如果等于 ZIPLIST_TAIL , 从表尾插入新节点
 * 
 * 返回值为添加新值后的压缩列表
 * 
 * T = O(N^2)
 */
// unsigned char *ziplistPush(unsigned char *zl,unsigned char *s,unsigned int slen,int where) {

//     // 根据添加方向计算添加的位置的起始地址
//     unsigned char *p;
//     p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);

//     // 添加新值
//     return __ziplistInsert(zl,p,s,slen);
// }


/*--------------------- debug --------------------*/
unsigned char *createIntList() {
    unsigned char *zl = ziplistNew();
    char buf[32];

    sprintf(buf, "100");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    // sprintf(buf, "128000");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    // sprintf(buf, "-100");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    // sprintf(buf, "4294967296");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_HEAD);
    // sprintf(buf, "non integer");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    // sprintf(buf, "much much longer non integer");
    // zl = ziplistPush(zl, (unsigned char*)buf, strlen(buf), ZIPLIST_TAIL);
    return zl;
}


// gcc -g zmalloc.c ziplist.c
int main(void) {
    unsigned char *zl, *p;
    unsigned char *entry;
    unsigned int elen;
    long long value;

    /* If an argument is given, use it as the random seed. */
    // if (argc == 2)
        // srand(atoi(argv[1]));
    srand(time(NULL));
    
    printf("0<<4=%d\n",0<<4);
    printf("1<<4=%d\n",1<<4);
    printf("2<<4=%d\n",2<<4);
    printf("3<<4=%d\n",3<<4);
    printf("4<<4=%d\n",4<<4);
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