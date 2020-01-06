#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sds.h"
#include "zmalloc.h"
#include "testhelp.h"
#include "limits.h"

/*
 * 根据给定的初始化字符串 init 和字符串长度 initlen
 * 创建一个新的sds 
 * 
 * 参数
 *  init ： 初始化字符串指针
 *  initlen ： 初始化字符串的长度
 * 
 * 返回值
 *  sds ：创建成功返回sdshdr 的 buf
 *        创建失败返回NULL
 * 
 * 复杂度
 *  T = O(N)
 */
sds sdsnewlen(const void *init,size_t initlen){

    struct sdshdr *sh;
    //申请内存空间
    if (init){
        sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
    } else {
        sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
    }
    //已用长度
    sh->len = initlen;
    //剩余长度
    sh->free = 0;
    //buf填充
    if(initlen && init)
        memcpy(sh->buf,init,initlen);
    //\0结尾
    sh->buf[initlen] = '\0';
    //返回buf内容
    return (char*)sh->buf;
}

/**
 * 创建并返回一个只保存了空字符串 "" 的 sds
 * 
 * 返回值
 * 成功 返回 sds 的 buf
 * 失败 返回NULL
 * 
 * 复杂度
 * T = O(1)
 */
sds sdsempty(void){
    return sdsnewlen("",0);
}

/**
 * 根据给定字符串init，创建一个包含同样字符串的sds 
 * 
 * 参数
 *  init ：如果输入为NULL，创建一个空白sds
 *         否则，新建的sds中包含和init内容相同的字符串
 * 
 * 返回值
 *  sds ：创建成功返回sdshdr相对应的sds
 *        创建失败返回NULL
 * 
 * 复杂度
 *  T = O(N)
 */
sds sdsnew(const char *init){
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init,initlen);
}

/**
 * 复制给定的 sds 的副本
 * 
 * 返回值
 * 成功 ： 返回 sds 的副本
 * 失败 ： 返回 NULL
 * 
 * 复杂度
 *  T = O(N)
 */
sds sdsdup(sds s){
    return sdsnewlen(s,strlen(s));
}

/**
 * 在不释放 sds 的字符串空间的情况下
 * 重置 sds 所保存的字符串为空字符串
 * 
 * 复杂度
 * T = O(1)
 */
void sdsclear(sds s){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));

    sh->free += sh->len;
    sh->len = 0;

    sh->buf[0] = '\0';
}

/**
 * 释放给定的 sds
 * 
 * 复杂度
 *  T = O(N)
 */
void sdsfree(sds s){
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));
}

/**
 * 对 sds 中 buf 的长度进行扩展，确保在函数执行之后
 * buf 至少会有 addlen + 1 长度的空余空间
 * (额外的 1 字节是为 \0 准备的)
 * 
 * 返回值
 *  sds ：成功返回扩展后的 sds
 *        失败返回NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdsMakeRoomFor(sds s,size_t addlen){

    struct sdshdr *sh, *newsh;

    //获取 s 目前剩余的空间长度
    size_t free = sdsavail(s);

    size_t len, newlen;

    //s 目前的剩余空间足够 , 无须扩展
    if (free >= addlen) return s;

    //获取 s 目前已用的空间长度
    len = sdslen(s);
    sh = (void*)(s-sizeof(struct sdshdr));

    //s 最少需要的长度
    newlen = len + addlen;

    //根据新长度, 为 s 分配新空间所需的大小
    if (newlen < SDS_MAX_PREALLOC)
        //如果新长度小于1M
        //那么分配两倍所需空间
        newlen *= 2;
    else
        //否则,新长度基础上再加 1M 
        newlen += SDS_MAX_PREALLOC;

    newsh = zrealloc(sh, sizeof(struct sdshdr)+newlen+1);

    //内存不足,分配失败
    if (newsh == NULL) return NULL;

    //更新 sds 的剩余长度
    newsh->free = newlen - len;

    //返回 sds
    return newsh->buf;
}

/**
 * 将 sds 扩充至指定长度，未使用的空间以 0 字节填充
 * 
 * 返回值
 * 成功：返回 sds
 * 失败：返回 NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdsgrowzero(sds s,size_t len){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen, curlen = sh->len;

    // 如果 len 比字符串的现有长度小，
    // 那么直接返回，不做动作
    if (len <= curlen) return s; 
    
    // 扩展 sds
    s = sdsMakeRoomFor(s,len-curlen);
    // 如果内存不足，直接返回
    if (s == NULL) return NULL;

    // 将新分配的空间用 0 填充，防止出现垃圾内容
    sh = (void*)(s-(sizeof(struct sdshdr)));
    memset(s+curlen,0,(len-curlen+1));

    // 更新属性
    totlen = sh->len+sh->free;
    sh->len = len;
    sh->free = totlen-sh->len;

    return s;
}

/**
 * 将长度为 len 的字符串 t 追加到 sds 的字符串末尾
 * 
 * 返回值
 * 成功: 返回新sds
 * 失败: 返回NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdscatlen(sds s,const void *t,size_t len){

    struct sdshdr *sh;

    //原有字符串长度
    size_t curlen = sdslen(s);

    //扩展 sds 空间
    s = sdsMakeRoomFor(s,len);
    //内存不足 直接返回
    if (s == NULL) return NULL;

    //复制 t 的内容到字符串后面
    sh = (void*)(s-sizeof(struct sdshdr));
    memcpy(s+curlen,t,len);

    //更新属性
    sh->len = curlen + len;
    sh->free = sh->free - len;

    //添加新结尾符号
    s[curlen+len] = '\0';

    //返回新 sds
    return s;
}

/**
 * 将给定字符串 t 追加到 s 的末尾
 * 
 * 返回值
 * sds 成功返回新sds ， 失败返回NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdscat(sds s,const char *t){
    return sdscatlen(s,t,strlen(t));
}

/**
 * 将另一个 sds 追加到 一个 sds 的末尾
 * 
 * 返回值
 * 成功：返回 sds
 * 失败：返回 NULL
 * 
 * 复杂度
 * T = O(N)
 */
sds sdscatsds(sds s, const sds t){
    return sdscatlen(s, t, strlen(t));
}

/**
 * 将字符串 t 的前 len 个字符复制到 s 中,覆盖原内容
 * 并在字符串最后添加终结符
 * 
 * 如果 sds 的长度少于 len 个字符，那么扩展 sds
 * 
 * 复杂度
 * T = O(N)
 * 
 * 返回值
 * 成功：新的 sds
 * 失败：NULL
 */
sds sdscpylen(sds s,const char *t,size_t len){
    
    struct sdshdr *sh = (void*)(s-sizeof(struct sdshdr));

    // sds 现有 buf 的长度
    size_t totlen = sh->free+sh->len;

    //如果 s 的长度小于 len , 扩展 s 的长度
    if (len > totlen){
        s = sdsMakeRoomFor(s,len-totlen);
        //内存申请失败
        if (s == NULL) return NULL;
        //获取新 s 的长度
        sh = (void*)(s-(sizeof(struct sdshdr)));
        totlen = sh->free+sh->len;
    }
    
    //复制内容 T = O(N)
    memcpy(s,t,len);

    // 添加终结符
    s[len] = '\0';

    //更新 新字符串的 len,free 属性
    sh->len = len;
    sh->free = totlen - len;

    return s;
}

/**
 * 将字符串复制到 sds 中, 覆盖原字符串
 * 
 * 如果 sds 的长度少于 len 个字符，那么扩展 sds
 * 
 * 复杂度
 * T = O(N)
 * 
 * 返回值
 * 成功：新的 sds
 * 失败：NULL
 */
sds sdscpy(sds s,const char *t){
    return sdscpylen(s, t, strlen(t));
}

/**
 * 打印函数, 被 sdscatprintf 调用
 */
sds sdscatvprintf(sds s, const char *fmt, va_list ap){
    va_list cpy;

    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt)*2;

    // 优先静态缓冲区,如果空间不够,变为堆分配
    if (buflen > sizeof(staticbuf)) {
        buf = zmalloc(buflen);
        if (buf == NULL) return NULL;
    } else {
        buflen = sizeof(staticbuf);
    }

    // 缓冲区大小小于字符串大小,
    // 将缓冲区大小扩容两倍
    while(1) {
        buf[buflen-2] = '\0';
        va_copy(cpy,ap);

        vsnprintf(buf,buflen,fmt,cpy);
        if (buf[buflen-2] != '\0') {
            if (buf != staticbuf) zfree(buf);
            buflen *= 2;
            buf = zmalloc(buflen);
            if (buf == NULL) return NULL;
            continue;
        }
        break;
    }

    t = sdscat(s,buf);
    if (buf != staticbuf) zfree(buf);
    return t;

}

/**
 * 打印任意数量个字符串，并将这些字符串追加到 sds 的末尾
 * 
 * Example:
 *
 * s = sdsempty("Sum is: ");
 * s = sdscatprintf(s,"%d+%d = %d",a,b,a+b).
 */
sds sdscatprintf(sds s, const char *fmt, ...){
    //可变函数
    va_list ap;
    char *t;

    //将第一个变量给可变函数
    va_start(ap,fmt);

    t = sdscatvprintf(s,fmt,ap);
    va_end(ap);
    return t;
}

/**
 * 对 sds 左右两端进行修剪，清除其中 cset 指定的所有字符
 * 
 * 比如 sdstrim(xxyyabcyyxy, "xy") 将返回 "abc"
 * 
 * 复杂性
 *  T = O(M*N) ， M 为 SDS 长度，N 为 cset 长度
 */
sds sdstrim(sds s,const char *cset){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t len;

    // 设置和记录指针
    sp = start = s;
    ep = end = s+sdslen(s)-1;

    // 修剪，T = O(N^2)
    while(sp <= end && strchr(cset,*sp)) sp++;
    while(ep >= start && strchr(cset,*ep)) ep--;

    // 计算 trim 完毕之后剩余的字符串长度
    len = (sp > ep) ? 0 : ((ep-sp)+1);

    // 如果有需要，前移字符串内容
    if (sh->buf != sp) memmove(sh->buf,sp,len);

    // 添加终结符
    sh->buf[len] = '\0';

    // 更新属性 len free 
    sh->free = sh->free+(sh->len-len);
    sh->len = len;

    return s;
}

/**
 * 按索引对截取 sds 字符串的其中一段
 * start 和 end 都是闭区间(包含在内)
 * 
 * 索引从 0 开始，最大为 sdslen(s)-1
 * 索引可以是负数，sdslen(s) - 1 == -1
 * 
 * 复杂度
 * T = O(N)
 */
void sdsrange(sds s,int start,int end){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t newlen, len = sdslen(s);

    if (len == 0) return;
    
    // 索引值小于 0 ,转换成大于 0 的索引值
    // 索引值超出 字符串大小 , 索引值给 0
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }

    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }

    // 计算截取后的字符串长度
    newlen = (start > end) ? 0 : (end-start)+1;
    if (newlen != 0){
        if (start >= (signed)len) {
            newlen = 0;
        } else if (end >= (signed)len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 如果有需要，对字符串进行移动
    if (start && newlen) memmove(sh->buf, sh->buf+start, newlen);

    // 添加终结符
    sh->buf[newlen] = '\0';

    // 更新属性
    sh->free = sh->free+(sh->len-newlen);
    sh->len = newlen;
}

/**
 * 对比两个 sds 
 * 
 * 返回值
 * int : 相等返回 0 ，s1 较大返回正数 ， s2 较大返回负数
 * 
 * T = O(N)
 */
int sdscmp(const sds s1, const sds s2){
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;

    // 比较相同长度的字符
    cmp = memcmp(s1,s2,minlen);

    // 如果相同长度的字符完全相同
    // 按长度返回
    if (cmp == 0) return l1-l2;

    return cmp;
}


//执行: gcc -g zmalloc.c testhelp.h sds.c
//执行: ./a.exe
int main(void){
    // printf("x=%s\n",x);
    struct sdshdr *sh;
    sds x = sdsnew("foo"), y;
    test_cond("Create a string and obtain the length",
        sdslen(x) == 3 && memcmp(x,"foo\0",4) == 0)

    sdsfree(x);
    x = sdsnewlen("foo",2);
    test_cond("Create a string with specified length",
        sdslen(x) == 2 && memcmp(x,"fo\0",3) == 0)

    x = sdscat(x,"bar");
    test_cond("Strings concatenation",
        sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

    x = sdscpy(x,"a");
    test_cond("sdscpy() against an originally longer string",
        sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

    x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    test_cond("sdscpy() against an originally shorter string",
        sdslen(x) == 33 &&
        memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

    sdsfree(x);
    x = sdscatprintf(sdsempty(),"%d",123);
    test_cond("sdscatprintf() seems working in the base case",
        sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)
    
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

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("foo");
    y = sdsnew("foa");
    test_cond("sdscmp(foo,foa)", sdscmp(x,y) > 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("bar");
    y = sdsnew("bar");
    test_cond("sdscmp(bar,bar)", sdscmp(x,y) == 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("aar");
    y = sdsnew("bar");
    test_cond("sdscmp(aar,bar)", sdscmp(x,y) < 0)

    sdsfree(y);
    sdsfree(x);
    x = sdsnew("bara");
    y = sdsnew("bar");
    test_cond("sdscmp(bara,bar)", sdscmp(x,y) > 0)

    // sdsfree(y);
    // sdsfree(x);
    // x = sdsnewlen("\a\n\0foo\r",7);
    // y = sdscatrepr(sdsempty(),x,sdslen(x));
    // test_cond("sdscatrepr(...data...)",
    //     memcmp(y,"\"\\a\\n\\x00foo\\r\"",15) == 0)

    return 0;
}
