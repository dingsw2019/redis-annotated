#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"


sds sdsnewlen(const char *init,size_t initLen){

    struct sdshdr *sh;
    // 创建内存空间
    if (init) {
        sh = zmalloc(sizeof(struct sdshdr)+initLen+1);
    } else {
        sh = zcalloc(sizeof(struct sdshdr)+initLen+1);
    }
    // 创建失败,返回
    if (sh == NULL) return NULL;

    // 拷贝字符串
    if (init && initLen)
        memcpy(sh->buf,init,initLen);

    // 添加终结符
    sh->buf[initLen] = '\0';

    // 更新属性
    sh->len = initLen;
    sh->free = 0;

    return sh->buf;
}

sds sdsnew(const char *init){
    return sdsnewlen(init,strlen(init));
}

sds sdsfree(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    zfree(sh);
}

sds sdsMakeRoomFor(sds s,size_t addlen){

    struct sdshdr *sh,*newsh;
    size_t newlen,len;
    sh = (void*)(s-(sizeof(struct sdshdr)));
    
    // 可用空间足够,无需扩容
    if (sh->free > addlen) return s;

    // 扩容后的总长度
    len = sh->len;
    newlen = sh->len + addlen;
    // 扩容策略,翻倍或定额
    if (newlen < SDS_MAX_PREALLOC) {
        newlen *= 2;
    } else {
        newlen += SDS_MAX_PREALLOC;
    }

    // 扩容失败返回
    newsh = zrealloc(sh,newlen);
    if (!newsh) return NULL;

    // 更新可用空间属性
    newsh->free = newlen - len;

    return newsh->buf;
}

sds sdscatlen(sds s,const char *init,size_t len){

    struct sdshdr *sh;
    size_t curlen = sdslen(s);
    // 扩容
    s = sdsMakeRoomFor(s,len);
    if (s == NULL) return NULL;

    // 拷贝字符串
    sh = (void*)(s-(sizeof(struct sdshdr)));
    memcpy(s+curlen,init,len);

    // 添加终结符
    s[curlen+len] = '\0';

    // 更新属性
    sh->len = curlen+len;
    sh->free = sh->free - len;

    return s;
}

sds sdscat(sds s,const char *init){
    return sdscatlen(s,init,strlen(init));
}

sds sdscpylen(sds s,const char *t,size_t len){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen;

    totlen = sh->len+sh->free;
    // 不满足长度,需扩容
    if (totlen < len) {
        s = sdsMakeRoomFor(s,len-totlen);
        if (s == NULL) return NULL;
        sh = (void*)(s-(sizeof(struct sdshdr)));
        totlen = sh->len+sh->free;
    }

    // 拷贝字符串
    memcpy(s,t,len);

    // 添加终结符
    s[len] = '\0';

    // 更新属性
    sh->free = totlen-len;
    sh->len = len;

    return s;
}

sds sdscpy(sds s,const char *t){
    return sdscpylen(s,t,strlen(t));
}

sds sdsdup(sds s){
    return sdsnewlen(s,strlen(s));
}

void sdstrim(sds s,const char *cset){

    char *start,*end,*sp,*ep;
    size_t len;
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    // 初始化起始和停止节点
    start = sp = s;
    end = ep = s+sh->len;

    // 两端寻找指定字符
    while(sp <= end && strchr(cset,*sp)) sp++;
    while(ep >= start && strchr(cset,*ep)) ep--;

    len = (sp > ep) ? 0 : (ep-sp)+1;
    // 起始字符变更,移动字符串
    if (start != sp) memmove(s,sp,len);

    // 添加终结符
    s[len] = '\0';

    // 更新属性
    sh->free += sh->len - len;
    sh->len = len;
}

void sdsrange(sds s,int start,int end){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t len = sdslen(s),newlen;

    if (len == 0) return;
    // 负索引转正索引
    if (start < 0){
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0){
        end = len+end;
        if (end < 0) end = 0;
    }
    // 计算截取后长度
    newlen = (start > end) ? 0 : (end-start)+1;

    // 最大值检查
    if (newlen != 0) {
        if (start > len) {
            newlen = 0;
        } else if(end > len){
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 起始字符变更,移动字符串
    if (start && newlen) memmove(s,s+start,newlen);
    // 添加终结符
    s[newlen] = '\0';
    // 更新属性
    sh->free += len - newlen;
    sh->len = newlen;
}

int sdscmp(sds s1,sds s2){

    size_t l1,l2,minLen;
    int cmp;

    // 获取字符串长度
    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minLen = (l1<l2) ? l1 : l2;
    // 字符串最小长度比对
    cmp = memcmp(s1,s2,minLen);
    // 完整比对
    if (cmp == 0) return l1-l2;

    return cmp;
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

    x = sdscpy(x,"a");
    test_cond("sdscpy() against an originally longer string",
        sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

    x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    test_cond("sdscpy() against an originally shorter string",
        sdslen(x) == 33 &&
        memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)
    
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

    return 0;
}