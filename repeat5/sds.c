#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"
#include "limits.h"

// 创建 sds 字符串
sds sdsnewlen(const void *init,size_t initLen){

    // 申请 sdshdr 内存
    struct sdshdr *sh;

    if (init) {
        sh = zmalloc(sizeof(struct sdshdr)+1+initLen);
    } else {
        sh = zcalloc(sizeof(struct sdshdr)+1+initLen);
    }

    // 初始化属性
    sh->len = initLen;
    sh->free = 0;

    // 赋值
    if (init && initLen)
        memcpy(sh->buf,init,initLen);
    
    // 添加终结符
    sh->buf[initLen] = '\0';

    // 返回字符串
    return (char*)sh->buf;
}

sds sdsnew(const char *s){
    return sdsnewlen(s,strlen(s));
}

void sdsfree(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    zfree(sh);
}

sds sdsMakeRoomFor(sds s,size_t addlen){
    struct sdshdr *newsh,*sh;
    size_t newlen,len;

    sh = (void*)(s-(sizeof(struct sdshdr)));
    if (sh->free > addlen) return s;

    len = sh->len;
    newlen = sh->len+addlen;
    if (newlen < SDS_MAX_PREALLOC)
        newlen *= 2;
    else
        newlen += SDS_MAX_PREALLOC;

    newsh = zrealloc(sh,sizeof(struct sdshdr)+newlen+1);
    if (newsh == NULL) return NULL;

    newsh->free = newlen - len;

    return newsh->buf;
}

sds sdscatlen(sds s,const char *t,size_t len){

    struct sdshdr *sh;
    
    size_t curlen = sdslen(s);

    // 扩展内存
    s = sdsMakeRoomFor(s,len);
    if (s == NULL) return NULL;

    sh = (void*)(s-(sizeof(struct sdshdr)));
    memcpy(s+curlen,t,len);

    // 添加终结符
    s[curlen+len] = '\0';

    // 调整属性
    sh->len = curlen+len;
    sh->free = sh->free-len;

    return s;
}

sds sdscat(sds s,const char *t){
    return sdscatlen(s,t,strlen(t));
}

sds sdscpylen(sds s,const char *t,size_t len){

    struct sdshdr *sh;
    sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen;
    totlen = sh->len+sh->free;
    // 是否需要扩容
    if (totlen < len){
        s = sdsMakeRoomFor(s,totlen-len);
        if (s == NULL) return NULL;
        sh = (void*)(s-(sizeof(struct sdshdr)));
        totlen = sh->len+sh->free;
    }
        
    // 复制字符串
    memcpy(s,t,len);

    // 添加终结符
    s[len] = '\0';

    // 更新属性
    sh->len = len;
    sh->free = totlen-len;

    return s;
}

sds sdscpy(sds s,const char *t){
    return sdscpylen(s,t,strlen(t));
}

sds sdstrim(sds s,const char *cset){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    char *start,*end,*sp,*ep;
    size_t len;

    sp = start = s;
    ep = end = s+sdslen(s);

    while(sp <= end && strchr(cset,*sp)) sp++;
    while(ep >= start && strchr(cset,*ep)) ep--;

    len = (sp > ep) ? 0 : (ep-sp)+1;

    if (start != sp) memmove(s,sp,len);

    s[len] = '\0';
    
    sh->free = sh->free+(sh->len-len);
    sh->len = len;

    return s;
}

sds sdsdup(sds s){
    return sdsnewlen(s,strlen(s));
}

// 指定范围截取字符串
void sdsrange(sds s,int start,int end){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t newlen,len = sdslen(s);

    if (len == 0) return;
    // 索引，负数转正数
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }

    // 计算截取后的字符串长度
    newlen = (start > end) ? 0 : (end-start)+1;

    if (newlen != 0) {
        if (start > len) {
            newlen = 0;
        } else if (end > len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 截取字符串
    if (start && newlen) memmove(s,s+start,newlen);

    // 添加终结符
    s[newlen] = '\0';

    // 更新属性
    sh->free = sh->free + (sh->len-newlen);
    sh->len = newlen;
}

int sdscmp(sds s1,sds s2){

    int cmp;
    size_t l1,l2,minLen;

    // 获取两个字符串的长度
    l1 = sdslen(s1);
    l2 = sdslen(s2);
    // 取最小长度
    minLen = (l1 < l2) ? l1 : l2;

    // 比较最小长度的部分
    cmp = memcmp(s1,s2,minLen);

    // 如果最小长度部分完全相同,谁长谁大
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

    // sdsfree(y);
    // sdsfree(x);
    // x = sdsnewlen("\a\n\0foo\r",7);
    // y = sdscatrepr(sdsempty(),x,sdslen(x));
    // test_cond("sdscatrepr(...data...)",
    //     memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)
    
    // int count;

    // // 双引号
    // char *line = "set a \"ohnot\"";
    // sds *argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    // // 双引号下的十六进制转十进制
    // line = "\"\\x40\"  hisisthe value";
    // argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    // // 单引号
    // line = " hset name '\\\'name\\\':filed'";
    // argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    // // 无单双引号的其他情况
    // line = "timeout 10086\r\nport 123321\r\n";
    // argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    return 0;
}
