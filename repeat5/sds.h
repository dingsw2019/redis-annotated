#ifndef __SDS_H
#define __SDS_H

#include <sys/types.h>
#include <stdarg.h>

#define SDS_MAX_PREALLOC (1024*1024)

typedef char* sds;

// sds 字符串结构
typedef struct sdshdr {

    // 已用长度
    int len;
    // 空闲长度
    int free;
    // 字符串
    char buf[];
} sdshdr;

// 已用长度
static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    return sh->len;
}

// 空闲长度
static inline size_t sdsavail(const sds s) {
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    return sh->free;
}

#endif