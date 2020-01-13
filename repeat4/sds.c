/**
 * 简单动态字符串(simple dynamic string )
 * 
 * sds优势
 *   1.常数复杂度获取字符串长度
 *   2.动态扩容，避免内存溢出
 *   3.减少修改字符串长度所需的内存重分配次数
 *   4.二进制安全
 *   5.兼容部分C字符串函数
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"

sds sdsnewlen(const char *init,size_t len){

    struct sdshdr *sh;

    // 申请内存空间
    if (init) {
        sh = zmalloc(sizeof(struct sdshdr)+len+1);
    } else {
        sh = zcalloc(sizeof(struct sdshdr)+len+1);
    }

    // 赋值 字符串
    memcpy(sh->buf,init,len);

    // 添加终结符
    sh->buf[len] = '\0';

    // 更新属性
    sh->len = len;
    sh->free = 0;

    return sh->buf;
}

sds sdsnew(const char *init){
    return sdsnewlen(init,strlen(init));
}

sds sdsdup(sds s){
    return sdsnewlen(s,strlen(s));
}

sds sdsempty(void){
    return sdsnewlen("",0);
}

sds sdsfree(sds s){
    zfree(s-(sizeof(struct sdshdr)));
}

sds sdsclear(sds s){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));

    // 清空字符串
    sh->buf[0] = '\0';

    // 更新属性
    sh->free += sh->len;
    sh->len = 0;

    return s;
}

sds sdsMakeRoomFor(sds s,size_t addlen){

    struct sdshdr *newsh, *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t free, len, newlen;

    free = sh->free;
    len = sh->len;

    // 剩余空间足够,不扩容
    if (free >= addlen) return s;

    newlen = len+addlen;
    // 1M 以下两倍扩
    // 1M 以上, 1M扩
    if (newlen < SDS_MAX_PREALLOC)
        newlen *= 2;
    else
        newlen += SDS_MAX_PREALLOC;

    // 申请扩容内存
    newsh = zrealloc(sh,sizeof(struct sdshdr)+newlen+1);
    if (newsh == NULL) return NULL;

    // 更新属性
    newsh->free = newlen-len;

    return newsh->buf;
}

sds sdsgrowzero(sds s,size_t len){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen,curlen=sh->len;

    if (len < curlen) return s;

    // 扩展
    s = sdsMakeRoomFor(s,totlen-len);
    if (s == NULL) return NULL;
    sh = (void*)(s-(sizeof(struct sdshdr)));
    
    // 0填充
    memset(s+curlen,0,len-curlen+1);

    // 更新属性
    totlen = sh->free+sh->len;
    sh->len = len;
    sh->free = totlen-sh->len;

    return s;

}

sds sdscatlen(sds s,const char *t,size_t len){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t curlen = sdslen(s);
    // 扩容
    s = sdsMakeRoomFor(s,len);
    if (s == NULL) return NULL;

    // 追加
    memcpy(s+curlen,t,len);

    // 添加终结符
    s[curlen+len] = '\0';

    // 更新属性
    sh->len = curlen+len;
    sh->free = sh->free-len;

    return s;
}

sds sdscat(sds s,const char *t){
    return sdscatlen(s,t,strlen(t));
}

sds sdscatsds(const sds s,const sds t){
    return sdscatlen(s,t,strlen(t));
}

sds sdscpylen(sds s,const char *t,size_t len){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen;

    totlen = sh->free+sh->len;

    // 是否需要扩容
    if (totlen < len) {
        s = sdsMakeRoomFor(s,len-totlen);
        if (s == NULL) return NULL;
        sh = (void*)(s-(sizeof(struct sdshdr)));
        totlen = sh->free+sh->len;
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

sds sdstrim(sds s,const char *cset){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t newlen,len = sdslen(s);

    // 参数初始化
    sp = start = s;
    ep = end = s+sdslen(s)-1;

    // 两端删除 cset 中的字符
    while(sp <= end && strchr(cset,*sp)) sp++;
    while(ep > start && strchr(cset,*ep)) ep--;

    // 删除后的字符串长度
    newlen = (sp > ep) ? 0 : (ep-sp)+1;

    // 如果字符串起始位置变更,移动字符串
    if (sh->buf != sp) memmove(s,sp,newlen);

    // 添加终结符
    s[newlen] = '\0';

    // 更新属性
    sh->free = sh->free+(len-newlen);
    sh->len = newlen;

    return s;
}

sds sdsrange(sds s,int start,int end){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t newlen,len = sdslen(s);

    if (len == 0) return NULL;
    // 负索引转 正索引
    if (start < 0){
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0){
        end = len+end;
        if (end < 0) end = 0;
    }

    // 截取后的字符串长度
    newlen = (start > end) ? 0 : (end-start)+1;

    // 如果开始结束索引大于 字符串长度
    // start>len 截取长度为 0
    // end>len 结束索引为最大长度
    if (newlen != 0) {
        if (start >= (signed)len){
            newlen = 0;
        } else if (end >= (signed)len){
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 起始位置变更, 需要移动字符串
    if (start && newlen) memmove(s,s+start,newlen);

    // 添加终结符
    s[newlen] = '\0';

    // 更新属性
    sh->free = sh->free+(len-newlen);
    sh->len = newlen;
}

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

    y = sdsnew("sdscatsds");
    x = sdscatsds(x,y);
    test_cond("sdscatsds() y=sdscatsds,x=fobar",
        sdslen(x) == 14 && memcmp(x,"fobarsdscatsds\0",15) == 0)

    x = sdscpy(x,"a");
    test_cond("sdscpy() against an originally longer string",
        sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

    x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    test_cond("sdscpy() against an originally shorter string",
        sdslen(x) == 33 &&
        memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)
    
    sdsfree(x);
    x = sdsnew("xxciaoyyy");
    sdstrim(x,"y");
    test_cond("sdstrim() correctly trims characters",
        sdslen(x) == 6 && memcmp(x,"xxciao\0",5) == 0)

    sdsfree(x);
    x = sdsnew("xxciaoyyy");
    sdstrim(x,"xy");
    test_cond("sdstrim() correctly trims characters",
        sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

    y = sdsdup(x);
    printf("y1=%s,",y);
    sdsrange(y,1,1);
    printf("y2=%s\n",y);
    test_cond("sdsrange(...,1,1)",
        sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

    sdsfree(y);
    y = sdsdup(x);
    printf("y1=%s,",y);
    sdsrange(y,1,-1);
    printf("y2=%s\n",y);
    test_cond("sdsrange(...,1,-1)",
        sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    sdsfree(y);
    y = sdsdup(x);
    printf("y1=%s,",y);
    sdsrange(y,-2,-1);
    printf("y2=%s\n",y);
    test_cond("sdsrange(...,-2,-1)",
        sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

    sdsfree(y);
    y = sdsdup(x);
    printf("y1=%s,",y);
    sdsrange(y,2,1);
    printf("y2=%s\n",y);
    test_cond("sdsrange(...,2,1)",
        sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

    sdsfree(y);
    y = sdsdup(x);
    printf("y1=%s,",y);
    sdsrange(y,1,100);
    printf("y2=%s\n",y);
    test_cond("sdsrange(...,1,100)",
        sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    sdsfree(y);
    y = sdsdup(x);
    printf("y1=%s,",y);
    sdsrange(y,100,100);
    printf("y2=%s\n",y);
    test_cond("sdsrange(...,100,100)",
        sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("foo");
    y = sdsnew("foa");
    printf("x=%s,y=%s,cmp=%d\n",x,y,sdscmp(x,y));
    test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("bar");
    y = sdsnew("bar");
    printf("x=%s,y=%s,cmp=%d\n",x,y,sdscmp(x,y));
    test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("aar");
    y = sdsnew("bar");
    printf("x=%s,y=%s,cmp=%d\n",x,y,sdscmp(x,y));
    test_cond("sdscmp(aar,bar)", sdscmp(x,y) < 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("bara");
    y = sdsnew("bar");
    printf("x=%s,y=%s,cmp=%d\n",x,y,sdscmp(x,y));
    test_cond("sdscmp(bara,bar)", sdscmp(x,y) > 0)

    // sdsfree(y);
    // sdsfree(x);
    // x = sdsnewlen("\a\n\0foo\r",7);
    // y = sdscatrepr(sdsempty(),x,sdslen(x));
    // test_cond("sdscatrepr(...data...)",
    //     memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)

    return 0;
}