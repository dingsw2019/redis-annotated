#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>

typedef char *sds;

struct sdshdr {
    int len;
    int free;
    char buf[];
};

static inline size_t sdslen(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

static inline size_t sdsavail(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}

#endif