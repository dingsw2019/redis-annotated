#ifndef __SDS_H
#define __SDS_H

//类型别名，存储buf
typedef char *sds;

//sds结构体
struct sdshdr {
    int len; //已用空间
    int free;//剩余空间
    char buf[];//存储字符串
};

#endif
