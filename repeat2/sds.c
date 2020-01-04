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
    return 0;
}