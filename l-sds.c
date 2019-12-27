#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "l-sds.h"
#include "l-zmalloc.h"
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
sds sdsnewlen(const void *init, size_t initlen){
   struct sdshdr *sh;

   printf("%s,%d\n",init,initlen);

   if (init){
      //zmalloc 不初始化所分配的内存
      sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
   } else {
      sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
   }

   //内存分配失败
   if (sh == NULL) return NULL;

   //设置初始化长度
   sh->len = initlen;
   //新 sds 不预留任何空间
   sh->free = 0;

   if(initlen && init)
      memcpy(sh->buf, init, initlen);

   //以 \0 结尾
   sh->buf[initlen] = '\0';
   printf("1 len=%d,free=%d,buf=%s\n",sh->len,sh->free,sh->buf);
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


//编译命令
//gcc zmalloc.c testhelp.h sds.c -D SDS_TEST_MAIN# 
//gdb编译
//gcc -g zmalloc.c testhelp.h sds.c -D SDS_TEST_MAIN
//gcc -g l-zmalloc.c testhelp.h l-sds.c
int main(void){

   struct sdshdr *sh;
   sds x = sdsnew("foo"), y;
   printf("buf=%s\n",x);
   test_cond("Create a string and obtain the length",
      sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

   // sdsfree(x);
   // printf("buf=%s\n",x);
   // sds x = sdsnewlen("foo",2);
   // printf("%d,buf=%s\n",x);

   // test_cond("Create a string with specified length",
   //    sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

   return 0;
}