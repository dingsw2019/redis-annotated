#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024 * 1024)
//保存buf
typedef char *sds;

//sds结构
struct sdshdr {
    int len;//已用空间
    int free;//未用空间
    char buf[];//字符串
};

static inline size_t sdslen(sds s){
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    return sh->len;
}

static inline size_t sdsavail(sds s){
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    return sh->free;
}


#endif