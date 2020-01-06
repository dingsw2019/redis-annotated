#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"

//创建sds
//问题1: buf赋值写错,没有用memcpy,而是直接赋值。字符串不能直接赋值
//问题2: 不知道返回值格式, buf的内容以指针返回
//问题3：function的返回格式写成void*，应该是sds
//问题4: 参数类型写错, init不是const char , initlen不是int
sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    
    //申请空间
    if(init){
        sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
    }else{
        sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
    }
    
    //已用字符串空间大小
    sh->len = initlen;
    //未用字符串空间大小
    sh->free = 0;

    //填充存储内容
    if (init && initlen){
        memcpy(sh->buf,init,initlen);
    }

    //字符串添加结束符,就可以使用c的字符串处理函数了
    sh->buf[initlen] = '\0';

    //返回buf内容
    return (char*)sh->buf;
}

sds sdsnew(const char *init){
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init,initlen);
}

void sdsfree(sds s){
    if (s == NULL) return;
    return zfree(s-(sizeof(struct sdshdr)));
}

sds sdsMakeRoomFor(sds s,size_t addlen){

    struct sdshdr *sh, *newsh;
    size_t len, newlen;
    size_t free = sdsavail(s);
    if (free >= addlen) return s;

    len = strlen(s);
    newlen = len + addlen;
    if (newlen < SDS_MAX_PREALLOC)
        newlen *= 2;
    else
        newlen += SDS_MAX_PREALLOC;

    // sh = (s-sizeof(struct sdshdr));
    // newsh = zrealloc(sh,newlen);
    // if (!newsh) return NULL;
    sh = (void*)(s-sizeof(struct sdshdr));
    newsh = zrealloc(sh,sizeof(struct sdshdr)+newlen+1);
    if (newsh == NULL) return NULL;

    // newsh->len = len;
    newsh->free = newlen - len;
    return newsh->buf;
}

sds sdscatlen(sds s,const void *t,size_t len){

    struct sdshdr *sh;
    size_t curlen = strlen(s);

    s = sdsMakeRoomFor(s,len);
    // if (!s) return NULL;
    if (s == NULL) return NULL;

    // sh = (s-sizeof(struct sdshdr));
    sh = (void*)(s-sizeof(struct sdshdr));
    memcpy(s+curlen,t,len);

    sh->len = curlen + len;
    sh->free = sh->free - len;

    s[curlen+len] = '\0';

    return s;
}

sds sdscat(sds s,const void *t){
    return sdscatlen(s,t, strlen(t));
}

// 删除 s 两端的 cset 指定的字符
sds sdstrim(sds s,const char *cset){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t len;

    // 设置起始和指针
    sp = start = s;
    ep = end = s+sdslen(s)-1;

    // 修剪
    while(sp <= end && strchr(cset,*sp)) sp++;
    while(ep > start && strchr(cset,*ep)) ep--;

    // 修剪后的字符串长度
    len = (sp > ep) ? 0 : ((ep-sp)+1);

    // 如果左侧有修剪,需移动字符
    if (sh->buf != sp) memmove(sh->buf,sp,len);

    // 添加终结符
    sh->buf[len] = '\0';

    // 更新属性
    sh->free = sh->free+(sh->len-len);
    sh->len = len;

    return s;
}

// 复制 sds 内容 并返回
sds sdsdup(sds s){
    return sdsnewlen(s,strlen(s));
}

// 指定范围，截取 sds
void sdsrange(sds s,int start,int end){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t newlen, len = sdslen(s);
    // 负数索引 转换成正数索引
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }

    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }

    // 计算截取后的 sds 长度
    newlen = (start > end) ? 0 : (end-start)+1;

    // 判断 start,end 是否在 len 范围内
    if (newlen != 0) {
        if (start > (signed)len) {
            newlen = 0;
        }else if (end > (signed)len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 如果需要,移动字符串
    if (start && newlen) memmove(sh->buf,s+start,newlen);

    // 添加终结符
    sh->buf[newlen] = '\0';

    // 更新属性
    sh->free = sh->free+(sh->len-newlen);
    sh->len = newlen;
}

int main(void){
    
    struct sdshdr *sh;
    sds x = sdsnew("foo"), y;
    test_cond("Create a string and obtain the length",
        sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0);

    sdsfree(x);
    x = sdsnewlen("foo",2);
    test_cond("Create a string with specified length",
        sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

    x = sdscat(x,"bar");
    test_cond("Strings concatenation",
        sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

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

    return 0;
}