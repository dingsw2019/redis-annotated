#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"

sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    //申请空间
    sh = zmalloc(sizeof(struct sdshdr)+1+initlen);
    //参数初始化,已用长度
    sh->len = initlen;
    //未用长度
    sh->free = 0;
    //字符串
    if(init && initlen)
        memcpy(sh->buf,init,initlen);
    
    return sh->buf;
}

int main(void){

    return 0;
}