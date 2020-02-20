#ifndef __SDS_H
#define __SDS_H

#include <sys/types.h>
#include <stdarg.h>

#define SDS_MAX_PREALLOC (1024*1024)

typedef char *sds;
// sds 结构
struct sdshdr
{
    int len; // 使用长度
    int free; // 空闲长度
    char buf[]; // 字符串
};

// 获取sds的len长度
// static inline size_t sdslen(const sds s){
//     struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
//     return sh->len;
// }
static inline size_t sdslen(const sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

// 获取sds的free长度
static inline size_t sdsavail(const sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}

#endif