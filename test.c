#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIZE 5

//8 = sizeof(size_t)
#define PREFIX_SIZE (sizeof(size_t))

int test_calloc(){
   //申请长度为5的数组空间
   int *p = NULL;
   int i = 0;
   p = (int *)calloc(SIZE,sizeof(int));
   if (p == NULL) {
      return -1;
   }
   
   //填充数组
   for(i=0;i<SIZE;i++){
      p[i] = i;
   }

   //输出数组
   for(i=0;i<SIZE;i++){
      printf("p[%d]=%d\n",i,p[i]);
   }

   //释放空间
   free(p);
   p = NULL;

   return 0;
}

int test_malloc(){
   int *p = NULL;
   //申请空间
   p = (int *)malloc(sizeof(int));
   if(p == NULL){
      return -1;
   }
   //分配给空间上的随机值
   printf("%d\n",*p);
   //将p指向的空间清0
   memset(p,0,sizeof(int));
   printf("%d\n",*p);
   *p=2;
   printf("%d\n",*p);
   return 0;
}

void test_ptr(){

   size_t size = 97;
   printf("%lu\n",size);
   void *ptr = malloc(size+PREFIX_SIZE);
   *((size_t *)ptr) = size;

   printf("%d\n",size);
   printf("%p\n",&size);
   printf("%p\n",ptr);
   printf("%d\n",*((size_t *)ptr));

   //错误,ptr是void类型,void是没有返回的,所以*ptr没有返回
   //指针跟着声明时的类型走,*((size_t *)ptr)只是想其中写入值
   //printf("%d\n",*ptr));
}