#include "sds.h"

// 创建一个指定长度的 sds
sds sdsnewlen(const char *init, size_t initlen)
{
    // 申请内存空间
    struct sdshdr *sh;

    if (init) {
        sh = zmalloc(initlen+1+sizeof(struct sdshdr));
    } else {
        sh = zcalloc(initlen+1+sizeof(struct sdshdr));
    }
    // 拷贝字符串到 sds
    if (init && initlen) // myerr 缺少
        memcpy(sh->buf,init,initlen);

    // 设置终结符
    sh->buf[initlen] = '\0';

    // 设置 sds 属性
    sh->len = initlen;
    sh->free = 0;

    return (char*)sh->buf;
}

// 创建一个sds
sds sdsnew(const char *init)
{
    size_t initlen = (init == NULL) ? 0 : strlen(init); // myerr:缺少
    return sdsnewlen(init,initlen);
}

void sdsfree(sds s)
{
    // struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    // zfree(sh);
    if (s == NULL) return; 
    zfree(s-sizeof(struct sdshdr));
}

// 扩容
sds sdsMakeRoomFor(sds s, size_t addlen)
{
    size_t newlen,len;
    struct sdshdr *newsh,*sh;
    // 当前空余空间大于扩容空间, 不执行
    if (sdsavail(s) > addlen) return s;

    // 计算扩容空间
    len = sdslen(s);
    newlen = len + addlen;
    if (newlen < SDS_MAX_PREALLOC) {
        newlen *= 2;
    } else {
        newlen += SDS_MAX_PREALLOC;
    }

    // 重新分配内存
    sh = (void*)(s-sizeof(struct sdshdr));
    newsh = zrealloc(sh,sizeof(struct sdshdr)+1+newlen);

    // myerr:缺少
    if (newsh == NULL) return NULL;

    // 更新属性
    newsh->free = newlen - len;

    // 返回
    return newsh->buf;
}

// 尾部追加字符串
sds sdscatlen(sds s,const void *t,size_t len)
{
    struct sdshdr *sh;
    size_t newlen;
    // 尝试扩容
    s = sdsMakeRoomFor(s,len);
    if (s == NULL) return NULL;

    // 尾部追加新字符串
    sh = (void*)(s-sizeof(struct sdshdr));
    newlen = sh->len + len;
    // memcpy(s,s+sh->len,len); myerr
    memcpy(s+sh->len,t,len);

    // 添加终结符
    s[newlen] = '\0';

    // 更新属性
    sh->len = newlen;
    sh->free = sh->free - len;
    
    return s;
}
// 追加字符串
sds sdscat(sds s,const char *t)
{
    return sdscatlen(s,t,strlen(t));
}

// t 的 len 长度的内容,赋值给s,并覆盖 s 原内容
sds sdscpylen(sds s,const char *t,size_t len)
{
    size_t addlen;
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));

    // 是否尝试扩容
    addlen = len - (sh->len + sh->free);
    if (addlen > 0) {
        s = sdsMakeRoomFor(s,addlen);
        if (s == NULL) return NULL;
        sh = (void*)(s-sizeof(struct sdshdr));
    }
    
    // 拷贝字符串
    memcpy(s,t,len);

    // 添加终结符
    s[len] = '\0';

    // 更新属性
    sh->free += sh->len - len;
    sh->len = len;

    // 返回
    return sh->buf;
}

// 拷贝
sds sdscpy(sds s,const char *t)
{
    return sdscpylen(s,t,strlen(t));
}

// 复制指定 sds 内容并创建一个 sds
sds sdsdup(sds s)
{
    return sdsnewlen(s,strlen(s));
}

// 删除两端指定字符
sds sdstrim(sds s,const char *cset)
{
    char *start,*sp,*end,*ep;
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    // int len; myerr
    size_t len;

    start = sp = s;
    // end = ep = s+sdslen(s); myerr
    end = ep = s+sdslen(s)-1;

    // 确定边界 myerr
    // while(sp < end && strchr(cset,*sp)) sp++;
    // while(ep > start && strchr(cset,*ep)) ep--;
    while(sp <= end && strchr(cset,*sp)) sp++;
    while(ep >= start && strchr(cset,*ep)) ep--;

    // 计算新字符串长度
    len =  (sp > ep) ? 0 : (ep-sp)+1;

    // 如果头部改变才移动字符串
    if (start != sp) {
        // memcpy(s,sp,len);
        memmove(s,sp,len);
    }

    // 添加终结符
    s[len] = '\0';

    // 更新属性
    sh->free += sh->len - len;
    sh->len = len;

    return s;
}

void sdsrange(sds s,int start,int end)
{
    size_t len,newlen;
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));
    len = sdslen(s);

    // myerr:缺少
    if (len == 0) return ;
    
    // 负索引转正数
    if (start < 0) {
        start = (len + start);
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = (len + end);
        if (end < 0) end = 0;
    }

    // 计算长度
    newlen = (start > end) ? 0 : (end-start)+1;

    // 超出范围处理
    if (newlen != 0) {
        if (start > len) {
            newlen = 0;
        } else if(end > len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 截取字符串
    if (start && newlen)
        memmove(s,s+start,newlen);

    // 添加终结符
    s[newlen] = '\0';

    // 更新属性
    sh->len = newlen;
    sh->free += newlen - len;

    // 返回
    // return s;
}

//执行: gcc -g zmalloc.c sds.c
//执行: ./a.exe
int main(void){

    struct sdshdr *sh;
    sds x = sdsnew("foo"), y;
    test_cond("Create a string and obtain the length",
        sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

    sdsfree(x);
    x = sdsnewlen("foo",2);
    test_cond("Create a string with specified length",
        sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

    x = sdscat(x,"bar");
    printf("%s\n",x);
    test_cond("Strings concatenation",
        sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

    x = sdscpy(x,"a");
    test_cond("sdscpy() against an originally longer string",
        sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

    x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    test_cond("sdscpy() against an originally shorter string",
        sdslen(x) == 33 &&
        memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

    // sdsfree(x);
    // x = sdscatprintf(sdsempty(),"%d",123);
    // test_cond("sdscatprintf() seems working in the base case",
    //     sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)
    
    sdsfree(x);
    x = sdsnew("xxciaoyyy");
    sdstrim(x,"xy");
    test_cond("sdstrim() correctly trims characters",
        sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

    y = sdsdup(x);
    sdsrange(y,1,1);
    test_cond("sdsrange(...,1,1)",
        sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,1,-1);
    test_cond("sdsrange(...,1,-1)",
        sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,-2,-1);
    test_cond("sdsrange(...,-2,-1)",
        sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,2,1);
    test_cond("sdsrange(...,2,1)",
        sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,1,100);
    test_cond("sdsrange(...,1,100)",
        sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    sdsfree(y);
    y = sdsdup(x);
    sdsrange(y,100,100);
    test_cond("sdsrange(...,100,100)",
        sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

    // sdsfree(y);
    // sdsfree(x);
    // x = sdsnew("foo");
    // y = sdsnew("foa");
    // test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

    // sdsfree(y);
    // sdsfree(x);
    // x = sdsnew("bar");
    // y = sdsnew("bar");
    // test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

    // sdsfree(y);
    // sdsfree(x);
    // x = sdsnew("aar");
    // y = sdsnew("bar");
    // test_cond("sdscmp(aar,bar)", sdscmp(x,y) < 0)

    // sdsfree(y);
    // sdsfree(x);
    // x = sdsnew("bara");
    // y = sdsnew("bar");
    // test_cond("sdscmp(bara,bar)", sdscmp(x,y) > 0)

    // // sdsfree(y);
    // // sdsfree(x);
    // // x = sdsnewlen("\a\n\0foo\r",7);
    // // y = sdscatrepr(sdsempty(),x,sdslen(x));
    // // test_cond("sdscatrepr(...data...)",
    // //     memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)
    
    // int count;

    // // 双引号
    // char *line = "set a \"ohnot\"";
    // sds *argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    // // 双引号下的十六进制转十进制
    // line = "\"\\x40\"  hisisthe value";
    // argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    // // 单引号
    // line = " hset name '\\\'name\\\':filed'";
    // argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    // // 无单双引号的其他情况
    // line = "timeout 10086\r\nport 123321\r\n";
    // argv = sdssplitargs(line, &count);
    // sdsSplitArgsPrint(line,argv,count);

    return 0;
}