#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"
#include "limits.h"

// 创建一个 sds 字符串
sds sdsnewlen(const void *init, size_t initlen) {

    struct sdshdr *sh;
    // 申请 sdshdr 内存空间
    if (init) {
        sh = zmalloc(initlen+1+sizeof(struct sdshdr));
    } else {
        sh = zcalloc(initlen+1+sizeof(struct sdshdr));
    }

    // 拷贝字符串
    if (init && initlen)
        memcpy(sh->buf, init, initlen);

    // 添加终结符
    sh->buf[initlen] = '\0';

    // 设置属性
    sh->free = 0;
    sh->len = initlen;

    return (char*)sh->buf;
}

// 创建并返回一个空的 sds 字符串
sds sdsempty(void) {
    return sdsnewlen("",0);
}

// 创建一个 sds 字符串
sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init,initlen);
}

// 拷贝一个 sds 字符串
sds sdsdup(sds s) {
    return sdsnewlen(s,strlen(s));
}

// 释放 sds
void sdsfree(sds s) {
    if (s == NULL) return ;
    // 获取 sds 的首地址, 释放
    zfree(s-sizeof(struct sdshdr));
}

// sds 空间扩容
sds sdsMakeRoomFor(sds s, size_t addlen) {

    size_t totlen, len;
    // 获取 s 的空闲长度
    struct sdshdr *newsh, *sh = (void*)(s-sizeof(struct sdshdr));

    // 如果空闲长度大于等于 addlen, 无需扩容, 直接返回
    if (sh->free >= addlen) return s;

    // 计算总的字符串长度
    len = sh->len;
    totlen = len + addlen;

    // 扩容策略, 计算扩容后的总字符串长度, 1M下2倍扩容, 1M上1M扩容
    if (totlen > SDS_MAX_PREALLOC) {
        totlen += SDS_MAX_PREALLOC;
    } else {
        totlen *= 2;
    }

    // 重分配内存空间
    newsh = zrealloc(sh, totlen+1+sizeof(struct sdshdr));
    if (newsh == NULL) return NULL;

    // 设置属性
    newsh->free = totlen - len;

    return newsh->buf;
}

// 将字符串 s 拷贝到 sds, 会覆盖原内容
sds sdscpylen(sds s, const char *t, size_t len) {

    // 获取 s 的字符串的全部可用空间
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    size_t totlen = sh->len + sh->free;

    // 计算是否需要扩容 s
    if (totlen < len) {
        s = sdsMakeRoomFor(s, len-totlen);
        if (s == NULL) return NULL;
        sh = (void*)(s-sizeof(struct sdshdr));
        totlen = sh->len + sh->free;
    }

    // 拷贝字符串
    memcpy(s, t, len);

    // 添加终结符
    s[len] = '\0';

    // 设置属性
    sh->free = (totlen - len);
    sh->len = len;

    return s;
}

sds sdscpy(sds s, const char *t) {
    return sdscpylen(s, t, strlen(t));
}

// sds 后追加字符串
sds sdscatlen(sds s, const void *t, size_t len) {

    struct sdshdr *sh;
    // 当前字符串长度
    size_t curlen = sdslen(s);

    // 扩容
    s = sdsMakeRoomFor(s, len);
    if (s == NULL) return NULL;

    sh = (void*)(s-sizeof(struct sdshdr));
    // 追加字符串
    memcpy(s+curlen, t, len);

    // 设置终结符
    s[curlen+len] = '\0';

    // 设置属性
    sh->free -= len;
    sh->len = curlen + len;

    return s;
}

sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}

sds sdscatsds(sds s, const sds t) {
    return sdscatlen(s, t, strlen(t));
}

// 两端去除指定字符
sds sdstrim(sds s, const char *cset) {

    char *start, *sp, *end, *ep;
    size_t len;
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));

    start = sp = s;
    end = ep = s+sdslen(s)-1;

    // 跳过指定字符, 找到起始和结束的位置
    while (sp <= end && strchr(cset,*sp)) sp++;
    while (ep >= start && strchr(cset,*ep)) ep--;

    // 截取后的字符串长度
    len = (sp > ep) ? 0 : (ep-sp)+1;

    // 首地址变更, 移动字符串数据
    if (start != sp)
        memmove(s, sp, len);

    // 添加终结符
    s[len] = '\0';

    // 设置属性
    sh->free += sh->len - len;
    sh->len = len;

    return s;
}

// 范围截取 sds 字符串
// 支持负索引, 负索引从 -1 开始
void sdsrange(sds s, int start, int end) {

    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    size_t newlen, len = sdslen(s);
    // 负索引转换
    if (start < 0) {
        start = len +start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = len+ end;
        if (end < 0) end = 0;
    }

    // 计算截取后的字符串长度
    newlen = (start > end) ? 0 : (end-start)+1;

    // 超出范围处理
    if (newlen != 0) {
        if (start >= (signed)len) {
            newlen = 0;
        } else if (end >= (signed)len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 是否需要移动字符串
    if (newlen && start) memmove(s,s+start,newlen);

    // 添加终结符
    s[newlen] = '\0';

    // 设置属性
    sh->free += sh->len - newlen;
    sh->len = newlen;
}

// 比较两个 sds 字符串
int sdscmp(sds s1, sds s2) {

    size_t l1, l2, minLen;
    int cmp;

    l1 = strlen(s1);
    l2 = strlen(s2);
    minLen = l1 < l2 ? l1 : l2;

    cmp = memcmp(s1,s2,minLen);
    if (cmp == 0) return l1-l2;

    return cmp;
}

// 回收 free 空间
sds sdsRemoveFreeSpace(sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));

    sh = zrealloc(sh, sizeof(struct sdshdr)+sh->len+1);
    sh->free = 0;

    return sh->buf;
}

/**
 * long long 型整数转换成字符串
 * 所需要的数组长度
 */
#define SDS_LLSTR_SIZE 21

/**
 * 将 value 转换成字符串, 存入 *s 指针中
 * 返回字符串的长度
 */
int sdsll2str(char *s, long long value) {
    char *p, aux;
    unsigned long long v;
    size_t l;

    // 生成反方向字符串
    v = (value < 0) ? -value : value;
    p = s;
    do {
        *p++ = '0'+(v%10);
        v /= 10;
    } while(v);
    if (value < 0) *p++ = '-';

    // 计算字符串长度, 添加终结符
    l = p-s;
    *p = '\0';

    // 字符串反转
    p--;
    while (s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }

    return l;
}

/**
 * 将 long long 型整数转换成字符串后存入 sds
 */
sds sdsfromlonglong(long long value) {
    char buf[SDS_LLSTR_SIZE];
    int len = sdsll2str(buf, value);
    return sdsnewlen(buf, len);
}

#ifdef SDS_TEST_MAIN

//执行: gcc -g zmalloc.c testhelp.h sds.c
//执行: ./a.exe
int main(void){

    // printf("x=%s\n",x);
    struct sdshdr *sh;
    sds x = sdsnew("foo"), y;
    test_cond("Create a string and obtain the length",
        sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

    sdsfree(x);
    x = sdsnewlen("foo",2);
    test_cond("Create a string with specified length",
        sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

    x = sdscat(x,"bar");
    test_cond("Strings concatenation",
        sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

    x = sdscpy(x,"a");
    test_cond("sdscpy() against an originally longer string",
        sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

    x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    test_cond("sdscpy() against an originally shorter string",
        sdslen(x) == 33 &&
        memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

    // sdsfree(x);
    // x = sdscatprintf(sdsempty(),"%d",123);
    // test_cond("sdscatprintf() seems working in the base case",
    //     sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)
    
    sdsfree(x);
    x = sdsnew("xxciaoyyy");
    sdstrim(x,"xy");
    test_cond("sdstrim() correctly trims characters",
        sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

    y = sdsdup(x);
    sdsrange(y,1,1);
    test_cond("sdsrange(...,1,1)",
        sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,1,-1);
    test_cond("sdsrange(...,1,-1)",
        sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,-2,-1);
    test_cond("sdsrange(...,-2,-1)",
        sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,2,1);
    test_cond("sdsrange(...,2,1)",
        sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,1,100);
    test_cond("sdsrange(...,1,100)",
        sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,100,100);
    test_cond("sdsrange(...,100,100)",
        sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("foo");
    y = sdsnew("foa");
    test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("bar");
    y = sdsnew("bar");
    test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("aar");
    y = sdsnew("bar");
    test_cond("sdscmp(aar,bar)", sdscmp(x,y) < 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("bara");
    y = sdsnew("bar");
    test_cond("sdscmp(bara,bar)", sdscmp(x,y) > 0)

//     // sdsfree(y);
//     // sdsfree(x);
//     // x = sdsnewlen("\a\n\0foo\r",7);
//     // y = sdscatrepr(sdsempty(),x,sdslen(x));
//     // test_cond("sdscatrepr(...data...)",
//     //     memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)
    
//     // int count;

//     // // 双引号
//     // char *line = "set a \"ohnot\"";
//     // sds *argv = sdssplitargs(line, &count);
//     // sdsSplitArgsPrint(line,argv,count);

//     // // 双引号下的十六进制转十进制
//     // line = "\"\\x40\"  hisisthe value";
//     // argv = sdssplitargs(line, &count);
//     // sdsSplitArgsPrint(line,argv,count);

//     // // 单引号
//     // line = " hset name '\\\'name\\\':filed'";
//     // argv = sdssplitargs(line, &count);
//     // sdsSplitArgsPrint(line,argv,count);

//     // // 无单双引号的其他情况
//     // line = "timeout 10086\r\nport 123321\r\n";
//     // argv = sdssplitargs(line, &count);
//     // sdsSplitArgsPrint(line,argv,count);

    return 0;
}

#endif
