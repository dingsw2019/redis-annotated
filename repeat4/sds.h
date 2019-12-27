#ifndef __SDS_H
#define __SDS_H

//类型别名,存储sdshdr结构的buf内容
typedef char *sds

//sdshdr结构体,用来存储字符串,及字符串长度信息
struct sdshdr {
    int len;//已用长度
    int free;//剩余长度
    char buf[];//字符串内容
};

#endif