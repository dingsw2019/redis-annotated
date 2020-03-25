#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void) {

    uint32_t i32;

    // ‭1111 1111 1101 0110 1101 1011‬

    // 0011 1111 1101 0110 1101 1011
    uint64_t value = 9766683;
    // int64_t value = ‭4183771‬;

    char s[40];//要转换成的字符数组
    itoa(value,s,2);//itoa转为二进制
    printf("itoa %s\n",s);//s就是你转换成的数组


    unsigned char *p;

    // ‭1101 0110 1101 1011‬ 0000 0000

    // ‭1111 1111 1101 0110 1101 1011 0000 0000‬

    i32 = value << 8;
    itoa(i32,s,2);//itoa转为二进制
    printf("itoa %s\n",s);//s就是你转换成的数组

    memcpy(p, ((uint8_t*)&i32)+1, sizeof(i32)-sizeof(uint8_t));
    // memcpy(p,((uint8_t*)&i32),sizeof(i32));

    // ‭1111 1111 1101 0110 1101 1011 0000 0000‬
    // <-  p2 -> <-  p1 -> <-  p0 ->
    printf("p0=%lld,p1=%lld,p2=%lld,p3=%lld\n",p[0],p[1],p[2],p[3]);

    printf("value = %lld, i32=%lld\n",value,i32);
    // memcpy(p, )


    i32 = 0;
    memcpy(((uint8_t*)&i32)+1, p, sizeof(i32)-sizeof(uint8_t));
    // printf("p0=%lld,p1=%lld,p2=%lld,p3=%lld\n",p[0],p[1],p[2],p[3]);
    itoa(i32,s,2);//itoa转为二进制
    printf("itoa %s\n",s);//s就是你转换成的数组
    printf("i32=%lld\n",i32);
    i32 = i32 >> 8;
    itoa(i32,s,2);//itoa转为二进制
    printf("itoa %s\n",s);//s就是你转换成的数组
    printf("i32=%lld\n",i32);

        // 11111111110101101101101100000000
    // 11111111111111111101011011011011
    return 0;
}