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

//执行: gcc -g zmalloc.c testhelp.h sds.c
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

    // x = sdscat(x,"bar");
    // test_cond("Strings concatenation",
    //     sdslen(x) == 5 && memcmp(x,"fobar\0",6) == 0);

    // x = sdscpy(x,"a");
    // test_cond("sdscpy() against an originally longer string",
    //     sdslen(x) == 1 && memcmp(x,"a\0",2) == 0)

    // x = sdscpy(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    // test_cond("sdscpy() against an originally shorter string",
    //     sdslen(x) == 33 &&
    //     memcmp(x,"xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0",33) == 0)

    // sdsfree(x);
    // x = sdscatprintf(sdsempty(),"%d",123);
    // test_cond("sdscatprintf() seems working in the base case",
    //     sdslen(x) == 3 && memcmp(x,"123\0",4) == 0)
    
    // sdsfree(x);
    // x = sdsnew("xxciaoyyy");
    // sdstrim(x,"xy");
    // test_cond("sdstrim() correctly trims characters",
    //     sdslen(x) == 4 && memcmp(x,"ciao\0",5) == 0)

    // y = sdsdup(x);
    // sdsrange(y,1,1);
    // test_cond("sdsrange(...,1,1)",
    //     sdslen(y) == 1 && memcmp(y,"i\0",2) == 0)

    // sdsfree(y);
    // y = sdsdup(x);
    // sdsrange(y,1,-1);
    // test_cond("sdsrange(...,1,-1)",
    //     sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    // sdsfree(y);
    // y = sdsdup(x);
    // sdsrange(y,-2,-1);
    // test_cond("sdsrange(...,-2,-1)",
    //     sdslen(y) == 2 && memcmp(y,"ao\0",3) == 0)

    // sdsfree(y);
    // y = sdsdup(x);
    // sdsrange(y,2,1);
    // test_cond("sdsrange(...,2,1)",
    //     sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

    // sdsfree(y);
    // y = sdsdup(x);
    // sdsrange(y,1,100);
    // test_cond("sdsrange(...,1,100)",
    //     sdslen(y) == 3 && memcmp(y,"iao\0",4) == 0)

    // sdsfree(y);
    // y = sdsdup(x);
    // sdsrange(y,100,100);
    // test_cond("sdsrange(...,100,100)",
    //     sdslen(y) == 0 && memcmp(y,"\0",1) == 0)

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