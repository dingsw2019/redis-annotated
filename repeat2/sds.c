#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sds.h"

//创建sds
//问题1: buf赋值写错,没有用memcpy,而是直接赋值。字符串不能直接赋值
//问题2: 不知道返回值格式, buf的内容以指针返回
//问题3：function的返回格式写成void*，应该是sds
//问题4: 参数类型写错, init不是const char , initlen不是int
sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    
    //申请空间
    sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
    
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

int main(void){

    return 0;
}