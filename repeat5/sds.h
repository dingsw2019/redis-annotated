#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>

typedef char* sds;

// sds 结构
struct sdshdr {
    int len; // 已用长度
    int free;// 未用长度
    char buf[];// 字符串
};

static inline size_t sdslen(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

static inline size_t sdsavail(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}

sds sdsnew(const char *init);
int sdscmp(const sds s1, const sds s2);
void sdsfree(sds s);

sds sdstrim(sds s,const char *cset);
sds sdscat(sds s,const char *t);
void sdsrange(sds s,int start,int end);
sds sdscatsds(sds s, const sds t);
#endif