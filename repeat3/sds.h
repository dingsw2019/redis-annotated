
#ifndef __SDS_H
#define __SDS_H

#define SDS_MAC_PREALLOC (1024 * 1024)

//sds字符串内容别名
typedef char* sds;

//sdshdr结构体,存储字符串信息
struct sdshdr {
    int len;//已用长度
    int free;//未用长度
    char buf[];//字符串内容
};

// static inline size_t sdslen(sds s){
//     void *ptr = s-sizeof(struct sdshdr);
//     return ptr->len;
// }
static inline size_t sdslen(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

static inline size_t sdsavail(sds s){
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    return sh->free;
}

#endif