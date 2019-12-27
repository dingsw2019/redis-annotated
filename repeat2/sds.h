#ifndef __SDS_H
#define __SDS_H

//保存buf
typedef char *sds;

//sds结构
struct sdshdr {
    int len;//已用空间
    int free;//未用空间
    char buf[];//字符串
};



#endif