#ifndef __SDS_H
#define __SDS_H

//最大预分配长度
#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>

//类型别名,存sdshdr的buf属性
typedef char *sds;

//sdshdr结构体
struct sdshdr {
    int len;//已用内存长度
    int free; //剩余内存长度
    char buf[];//存储的字符串
};

//返回sds已用长度
static inline size_t sdslen(const sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

//返回sds剩余长度
static inline size_t sdsavail(const sds s){
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    return sh->free;
}

#endif