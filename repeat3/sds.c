#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"

//创建sds
sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    //申请空间
    if (init) {
        sh = zmalloc(sizeof(struct sdshdr)+1+initlen);
    } else {
        sh = zcalloc(sizeof(struct sdshdr)+1+initlen);
    }

    //初始化值
    sh->len = initlen;
    sh->free = 0;
    
    if(init && initlen)
        memcpy(sh->buf,init,initlen);
    
    //todo 少写这里了
    sh->buf[initlen] = '\0';

    //返回字符串
    return (char*)sh->buf;
}

// sds sdsnew(const void *init){
//     size_t initlen = strlen(init);
//     return sdsnewlen(init,initlen);
// }
sds sdsnew(const char *init){
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init,initlen);
}

// void sdsfree(sds s){
//     zfree(s-sizeof(struct sdshdr));
// }
void sdsfree(sds s){
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));
}

//控制每次申请的内存大小和 sdshdr 结构
sds sdsMakeRoomFor(sds s,size_t addlen){

    struct sdshdr *sh,*newsh;
    
    size_t len,newlen;
    size_t free = sdsavail(s);
    if (free >= addlen) return s;

    len = sdslen(s);
    newlen = len + addlen;
    //多申请内存的规则
    if (newlen < SDS_MAC_PREALLOC)
        //小于1M，申请新空间两倍的内存
        newlen *= 2;
    else
        //大于1M，申请新空间的基础上再加1M的内存
        newlen += SDS_MAC_PREALLOC;

    //重新分配内存
    sh = (void*)(s-sizeof(struct sdshdr));
    newsh = zrealloc(sh,newlen);

    //漏写, 内存不足,分配失败
    if (newsh == NULL) return NULL;
    
    // 内存扩容,原内存地址不变,在此基础上增加
    // 所以 len 不变, free 变大
    newsh->free = newlen - len;

    return newsh->buf;
}

//将字符串 t 追加到 sds上, 控制 sdshdr 结构
sds sdscatlen(sds s,const void *t,size_t len){

    struct sdshdr *sh;
    size_t curlen = sdslen(s);
    // 内存扩容
    s = sdsMakeRoomFor(s,len);
    if (s == NULL) return NULL;

    // 追加 字符串
    memcpy(s+curlen,t,len);

    sh = (void*)(s-sizeof(struct sdshdr));
    // 调整 len free
    sh->len = curlen + len;
    sh->free = sh->free - len;
    
    s[curlen+len] = '\0';

    return s;
}

//在 sds 末尾追加 t字符串
sds sdscat(sds s,const void *t){
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

    printf("x=%s\n",x);
    x = sdscat(x,"bar");
    printf("x=%s\n",x);
    test_cond("Strings concatenation",
        sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);
    
    return 0;
}