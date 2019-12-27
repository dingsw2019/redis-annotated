#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"
#include "limits.h"

/*
 * 根据给定的初始化字符串 init 和字符串长度 initlen
 * 创建一个新的sds 
 * 
 * 参数
 *  init ： 初始化字符串指针
 *  initlen ： 初始化字符串的长度
 * 
 * 返回值
 *  sds ：创建成功返回sdshdr 的 buf
 *        创建失败返回NULL
 * 
 * 复杂度
 *  T = O(N)
 */
sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    //申请内存空间
    if (init){
        sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
    } else {
        sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
    }
    //已用长度
    sh->len = initlen;
    //剩余长度
    sh->free = 0;
    //buf填充
    if(initlen && init)
        memcpy(sh->buf,init,initlen);
    //\0结尾
    sh->buf[initlen] = '\0';
    //返回buf内容
    return (char*)sh->buf;
}

/**
 * 根据给定字符串init，创建一个包含同样字符串的sds 
 * 
 * 参数
 *  init ：如果输入为NULL，创建一个空白sds
 *         否则，新建的sds中包含和init内容相同的字符串
 * 
 * 返回值
 *  sds ：创建成功返回sdshdr相对应的sds
 *        创建失败返回NULL
 * 
 * 复杂度
 *  T = O(N)
 */
sds sdsnew(const char *init){
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init,initlen);
}

/**
 * 释放给定的 sds
 * 
 * 复杂度
 *  T = O(N)
 */
void sdsfree(sds s){
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));
}

//执行: gcc -g zmalloc.c testhelp.h sds.c
//执行: ./a.exe
int main(void){

   struct sdshdr *sh;
   sds x = sdsnew("foo"), y;
   printf("buf=%s\n",x);
   test_cond("Create a string and obtain the length",
      sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

    sdsfree(x);
    printf("buf=%s\n",x);
    return 0;
}
