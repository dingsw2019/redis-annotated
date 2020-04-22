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

sds sdsempty(void);
sds sdscatlen(sds s,const void *t,size_t len);
sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
int sdscmp(const sds s1, const sds s2);
void sdsfree(sds s);

sds sdstrim(sds s,const char *cset);
sds sdscat(sds s,const char *t);
void sdsrange(sds s,int start,int end);
sds sdscatsds(sds s, const sds t);
sds sdsfromlonglong(long long value);

sds sdsgrowzero(sds s,size_t len);
sds sdsMakeRoomFor(sds s,size_t addlen);

sds sdsRemoveFreeSpace(sds s);

sds sdscpy(sds s,const char *t);
sds sdscpylen(sds s,const char *t,size_t len);

#endif