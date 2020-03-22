#ifndef __SDS_H
#define __SDS_H

#include <sys/types.h>
#include <stdarg.h>

// 最大预分配长度 myerr:缺少
#define SDS_MAX_PREALLOC (1024*1024)

typedef char* sds;

typedef struct sdshdr
{
    int len;
    int free;
    char buf[];
} sdshdr;

// sds字符串已用长度
// static int sdslen(sds s) myerr
static inline size_t sdslen(const sds s)
{
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    return sh->len;
}

// sds字符串空闲长度
// static int sdsavail(sds s) myerr
static inline size_t sdsavail(const sds s)
{
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
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