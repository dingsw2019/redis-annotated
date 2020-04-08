#ifndef __SDS_H
#define __SDS_H

#include <sys/types.h>
#include <stdarg.h>

//最大预分配长度
#define SDS_MAX_PREALLOC (1024*1024)


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


sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
int sdscmp(const sds s1, const sds s2);
void sdsfree(sds s);

sds sdstrim(sds s,const char *cset);
sds sdscat(sds s,const char *t);
void sdsrange(sds s,int start,int end);
sds sdscatsds(sds s, const sds t);
sds sdsfromlonglong(long long value);


sds sdsMakeRoomFor(sds s,size_t addlen);

sds sdsRemoveFreeSpace(sds s);

#endif