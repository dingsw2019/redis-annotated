#ifndef __SDS_H
#define __SDS_H

//类型别名,存sdshdr的buf内容
typedef char *sds;

//sdshdr结构体
struct sdshdr {
    int len;//已用空间
    int free; //未用空间
    char buf[];//存储的字符串
};

//返回sdshdr已用长度
static inline size_t sdslen(const char s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

#endif