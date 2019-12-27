#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"

/**
 * 创建sds字符串,返回字符串内容 
 */
sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    sh = zmalloc(sizeof(struct sdshdr)+1+initlen);

    sh->len = initlen;
    sh->free = 0;

    if(init && initlen)
        memcpy(sh->buf,init,initlen);

    sh->buf[initlen] = '\0';

    return (char*)sh->buf;
}

int main(void){

    return 0;
}