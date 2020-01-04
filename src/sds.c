#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"
#include "limits.h"

/*
 * 根据给定的初始化字符串 init 和字符串长度 initlen
 * 创建一个新的sds 
 * 
 * 参数
 *  init ： 初始化字符串指针
 *  initlen ： 初始化字符串的长度
 * 
 * 返回值
 *  sds ：创建成功返回sdshdr 的 buf
 *        创建失败返回NULL
 * 
 * 复杂度
 *  T = O(N)
 */
sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    //申请内存空间
    if (init){
        sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
    } else {
        sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
    }
    //已用长度
    sh->len = initlen;
    //剩余长度
    sh->free = 0;
    //buf填充
    if(initlen && init)
        memcpy(sh->buf,init,initlen);
    //\0结尾
    sh->buf[initlen] = '\0';
    //返回buf内容
    return (char*)sh->buf;
}

/**
 * 根据给定字符串init，创建一个包含同样字符串的sds 
 * 
 * 参数
 *  init ：如果输入为NULL，创建一个空白sds
 *         否则，新建的sds中包含和init内容相同的字符串
 * 
 * 返回值
 *  sds ：创建成功返回sdshdr相对应的sds
 *        创建失败返回NULL
 * 
 * 复杂度
 *  T = O(N)
 */
sds sdsnew(const char *init){
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init,initlen);
}

/**
 * 释放给定的 sds
 * 
 * 复杂度
 *  T = O(N)
 */
void sdsfree(sds s){
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));
}

/**
 * 对 sds 中 buf 的长度进行扩展，确保在函数执行之后
 * buf 至少会有 addlen + 1 长度的空余空间
 * (额外的 1 字节是为 \0 准备的)
 * 
 * 返回值
 *  sds ：成功返回扩展后的 sds
 *        失败返回NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdsMakeRoomFor(sds s,size_t addlen){

    struct sdshdr *sh, *newsh;

    //获取 s 目前剩余的空间长度
    size_t free = sdsavail(s);

    size_t len, newlen;

    //s 目前的剩余空间足够 , 无须扩展
    if (free >= addlen) return s;

    //获取 s 目前已用的空间长度
    len = sdslen(s);
    sh = (void*)(s-sizeof(struct sdshdr));

    //s 最少需要的长度
    newlen = len + addlen;

    //根据新长度, 为 s 分配新空间所需的大小
    if (newlen < SDS_MAX_PREALLOC)
        //如果新长度小于1M
        //那么分配两倍所需空间
        newlen *= 2;
    else
        //否则,新长度基础上再加 1M 
        newlen += SDS_MAX_PREALLOC;

    newsh = zrealloc(sh, sizeof(struct sdshdr)+newlen+1);

    //内存不足,分配失败
    if (newsh == NULL) return NULL;

    //更新 sds 的剩余长度
    newsh->free = newlen - len;

    //返回 sds
    return newsh->buf;
}

/**
 * 将长度为 len 的字符串 t 追加到 sds 的字符串末尾
 * 
 * 返回值
 * 成功: 返回新sds
 * 失败: 返回NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdscatlen(sds s,const void *t,size_t len){

    struct sdshdr *sh;

    //原有字符串长度
    size_t curlen = sdslen(s);

    //扩展 sds 空间
    s = sdsMakeRoomFor(s,len);
    //内存不足 直接返回
    if (s == NULL) return NULL;

    //复制 t 的内容到字符串后面
    sh = (void*)(s-sizeof(struct sdshdr));
    memcpy(s+curlen,t,len);

    //更新属性
    sh->len = curlen + len;
    sh->free = sh->free - len;

    //添加新结尾符号
    s[curlen+len] = '\0';

    //返回新 sds
    return s;
}

/**
 * 将给定字符串 t 追加到 s 的末尾
 * 
 * 返回值
 * sds 成功返回新sds ， 失败返回NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdscat(sds s,const char *t){
    return sdscatlen(s,t,strlen(t));
}


//执行: gcc -g zmalloc.c testhelp.h sds.c
//执行: ./a.exe
int main(void){

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

    return 0;
}
