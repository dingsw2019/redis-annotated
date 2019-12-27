
#ifndef __SDS_H
#define __SDS_H

//类型别名,用于指向 sdshdr 的 buf 属性
typedef char *sds;

//保存字符串对象的结构
struct sdshdr {

    //buf 已占用空间的长度
    int len;

    //buf 中剩余可用空间的长度
    int free;

    //数据空间
    char buf[];
};

static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

#endif