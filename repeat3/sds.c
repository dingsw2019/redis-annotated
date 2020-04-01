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

// 复制 sds
sds sdsdup(sds s){
    return sdsnewlen(s,strlen(s));
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
    newsh = zrealloc(sh,(sizeof(struct sdshdr))+newlen+1);

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
sds sdscat(sds s,const char *t){
    return sdscatlen(s,t,strlen(t));
}

// 将 sds 追加到另一个 sds 的末尾
sds sdscatsds(sds s,const sds t){
    return sdscatlen(s, t, strlen(t));
}

// 从 t 复制 len 个字符到 sds
sds sdscpylen(sds s,const char *t,size_t len){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen = sh->free+sh->len;

    //是否需要扩容
    if (totlen < len){
        s = sdsMakeRoomFor(s,len-totlen);
        if (s == NULL) return NULL;
        sh = (void*)(s-(sizeof(struct sdshdr)));
        totlen = sh->free+sh->len;
    }

    //复制
    memcpy(s, t, len);
    s[len] = '\0';

    //更新属性
    sh->len = len;
    sh->free = totlen-len;

    return s;
}

sds sdscpy(sds s,const char *t){
    return sdscpylen(s,t,strlen(t));
}

// 在 sds 两端删除 cset 中的字符
sds sdstrim(sds s,const char *cset){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t len;

    sp = start = s;
    ep = end = s+sdslen(s)-1;

    // 两端删除
    while(sp <= end && strchr(cset,*sp)) sp++;
    while(ep > start && strchr(cset,*ep)) ep--;

    // 截取后的长度
    len = (sp > ep) ? 0 : (ep-sp)+1;

    // 如果有需要，移动起始位
    if (sp > start) memmove(s,sp,len);

    // 添加终结符
    s[len] = '\0';

    // 更新属性
    sh->free = sh->free+(sh->len-len);
    sh->len = len;

    return s;
}

// 指定范围截取 sds
// -1 代表最后1位
void sdsrange(sds s,int start,int end){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t newlen, len = sdslen(s);
    // 负数索引 转换为正数索引
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }

    // 截取后的字符串长度
    newlen = (start > end) ? 0 : (end-start)+1;

    // 索引值是否超出范围,头部索引超出,长度为0
    // 如果是尾部索引超出,给最大长度
    if (newlen != 0) {
        if (start >= (signed)len){
            newlen = 0;
        } else if (end >= (signed)len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }  

    // 如果有需要，移动字符串起始位置
    if (start && newlen) memmove(sh->buf,s+start,newlen);

    // 添加终结符
    sh->buf[newlen] = '\0';
    
    // 更新属性
    sh->free = sh->free+(len-newlen);
    sh->len = newlen;
}

//比较两个字符串
//相等返0; s1>s2 返1; s2>s1返-1
int sdscmp(const sds s1,const sds s2){

    size_t l1,l2,minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;

    cmp = memcmp(s1,s2,minlen);

    if (cmp == 0) return l1-l2;

    return cmp;
}

//执行: gcc -g zmalloc.c testhelp.h sds.c
//执行: ./a.exe
// int main(void){
//     // printf("x=%s\n",x);
//     struct sdshdr *sh;
//     sds x = sdsnew("foo"), y;
//     test_cond("Create a string and obtain the length",
//         sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

//     sdsfree(x);
//     x = sdsnewlen("foo",2);
//     test_cond("Create a string with specified length",
//         sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

//     x = sdscat(x,"bar");
//     test_cond("Strings concatenation",
//         sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

//     y = sdsnew("sdscatsds");
//     x = sdscatsds(x,y);
//     test_cond("sdscatsds() y=sdscatsds,x=fobar",
//         sdslen(x) == 14 && memcmp(x,"fobarsdscatsds\0",15) == 0)

//     x = sdscpy(x,"a");
//     test_cond("sdscpy() against an originally longer string",
//         sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

//     x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
//     test_cond("sdscpy() against an originally shorter string",
//         sdslen(x) == 33 &&
//         memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

//     // sdsfree(x);
//     // x = sdscatprintf(sdsempty(),"%d",123);
//     // test_cond("sdscatprintf() seems working in the base case",
//     //     sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)
    
//     sdsfree(x);
//     x = sdsnew("xxciaoyyy");
//     sdstrim(x,"xy");
//     test_cond("sdstrim() correctly trims characters",
//         sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

//     y = sdsdup(x);
//     sdsrange(y,1,1);
//     test_cond("sdsrange(...,1,1)",
//         sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

//     sdsfree(y);
//     y = sdsdup(x);
//     sdsrange(y,1,-1);
//     test_cond("sdsrange(...,1,-1)",
//         sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

//     sdsfree(y);
//     y = sdsdup(x);
//     sdsrange(y,-2,-1);
//     test_cond("sdsrange(...,-2,-1)",
//         sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

//     sdsfree(y);
//     y = sdsdup(x);
//     sdsrange(y,2,1);
//     test_cond("sdsrange(...,2,1)",
//         sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

//     sdsfree(y);
//     y = sdsdup(x);
//     sdsrange(y,1,100);
//     test_cond("sdsrange(...,1,100)",
//         sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

//     sdsfree(y);
//     y = sdsdup(x);
//     sdsrange(y,100,100);
//     test_cond("sdsrange(...,100,100)",
//         sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

//     sdsfree(y);
//     sdsfree(x);
//     x = sdsnew("foo");
//     y = sdsnew("foa");
//     test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

//     sdsfree(y);
//     sdsfree(x);
//     x = sdsnew("bar");
//     y = sdsnew("bar");
//     test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

//     sdsfree(y);
//     sdsfree(x);
//     x = sdsnew("aar");
//     y = sdsnew("bar");
//     test_cond("sdscmp(aar,bar)", sdscmp(x,y) < 0)

//     sdsfree(y);
//     sdsfree(x);
//     x = sdsnew("bara");
//     y = sdsnew("bar");
//     test_cond("sdscmp(bara,bar)", sdscmp(x,y) > 0)

//     // sdsfree(y);
//     // sdsfree(x);
//     // x = sdsnewlen("\a\n\0foo\r",7);
//     // y = sdscatrepr(sdsempty(),x,sdslen(x));
//     // test_cond("sdscatrepr(...data...)",
//     //     memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)

//     return 0;
// }