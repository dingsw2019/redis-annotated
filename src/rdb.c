#include "redis.h"
#include "lzf.h"    /* LZF compression library */
#include "zipmap.h"
#include "endianconv.h"

#include <math.h>
#include <sys/types.h>
#include <sys/time.h>
// #include <sys/resource.h>
// #include <sys/wait.h>
// #include <arpa/inet.h>
#include <sys/stat.h>

/**
 * 将长度为 len 的字符数组 p 写入到 rdb 中
 * 写入成功返回 len, 失败返回 -1
 */
static int rdbWriteRaw(rio *rdb, void *p, size_t len) {
    if (rdb && rioWrite(rdb,p,len) == 0)
        return -1;
    return len;
}

/**
 * 将长度为 1字节的字符 type 写入 rdb
 * 成功返回字符串长度, 失败返回 -1
 */
int rdbSaveType(rio *rdb, unsigned char type) {
    return rdbWriteRaw(rdb,&type,1);
}

/**
 * 从 rdb 载入 1 字节长的 type 数据
 * 
 * 成功返回 type, 失败返回 -1
 * 
 * 既可载入键的类型(rdb.h/REDIS_RDB_TYPE_*)
 * 也可载入特殊标识符(rdb.h/REDIS_RDB_OPCODE_*)
 */
int rdbLoadType(rio *rdb) {
    unsigned int type;
    if (rioRead(rdb,&type,1) == 0) return -1;
    return type;
}

/**
 * 载入以秒为单位的过期时间，长度 4 字节
 */
time_t rdbLoadTime(rio *rdb) {
    int32_t t32;
    if (rioRead(rdb,&t32,4) == 0) return -1;
    return (time_t)t32;
}

/**
 * 将长度为 8 字节的毫秒过期时间写入 rdb
 */
int rdbSaveMillisecondTime(rio *rdb, long long t) {
    int64_t t64 = (int64_t) t;
    return rdbWriteRaw(rdb,&t64,8);
}

/**
 * 从 rdb 中载入 8 字节的毫秒过期时间
 */
long long rdbLoadMillisecondTime(rio *rdb) {
    int64_t t64;
    if (rioRead(rdb,&t64,8) == 0) return -1;
    return (long long)t64;
}

/**
 * 对 len 进行特殊编码后写入 rdb
 * 写入成功返回编码后所需字节数
 * 写入失败, 返回 -1
 */
int rdbSaveLen(rio *rdb, uint32_t len) {
    unsigned char buf[2];
    size_t nwritten;

    // 6 bit
    if (len < (1<<6)) {
        buf[0] = (len&0xFF) | (REDIS_RDB_6BITLEN<<6);
        if (rdbWriteRaw(rdb,buf,1) == -1) return -1;
        nwritten = 1;

    // 14 bit
    } else if (len < (1<<14)) {
        buf[0] = ((len>>8)&0xFF) | (REDIS_RDB_14BITLEN<<6);
        buf[1] = len&0xFF;
        if (rdbWriteRaw(rdb,buf,2) == -1) return -1;
        nwritten = 2;

    // 32 bit
    } else {
        buf[0] = (REDIS_RDB_32BITLEN<<6);
        if (rdbWriteRaw(rdb,buf,1) == -1) return -1;
        len = htonl(len);
        if (rdbWriteRaw(rdb,&len,4) == -1) return -1;
        nwritten = 1+4;
    }

    return nwritten;
}

/**
 * 读一个被编码的长度值, 并返回
 * 如果 length 值不是整数, 而是一个被编码的值, 那么 isencoded 被设为 1
 * REDIS_RDB_ENCVAL 会编码值
 */
uint32_t rdbLoadLen(rio *rdb, int *isencoded) {
    unsigned char buf[2];
    uint32_t len;
    int type;

    if (isencoded) *isencoded = 0;

    // 读取类型
    if (rioRead(rdb,buf,1) == 0) return REDIS_RDB_LENERR;
    type = (buf[0] & 0xC0) >> 6;

    // 特殊编码, 解码
    if (type == REDIS_RDB_ENCVAL) {
        if (isencoded) *isencoded = 1;
        return buf[0]&0x3F;

    // 6 bit
    } else if (type == REDIS_RDB_6BITLEN) {
        return buf[0]&0x3F;

    // 14 bit
    } else if (type == REDIS_RDB_14BITLEN) {
        if (rioRead(rdb,buf+1,1) == 0) return REDIS_RDB_LENERR;
        return ((buf[0]&0x3F) << 8) | buf[1];

    // 32 bit
    } else {
        if (rioRead(rdb,&len,4) == 0) return REDIS_RDB_LENERR;
        return ntohl(len);
    }
}

/**
 * 尝试使用特殊的整数编码来保存 value, 要求 value 需在指定范围内
 * 
 * 如果可以编码，将编码后的值保存在 enc 指针中
 * 并返回值在编码后所需的长度
 * 
 * 如果不能编码，返回 0
 */
int rdbEncodeInteger(long long value, unsigned char *enc) {

    // 8 bit
    if (value >= -(1<<7) && value <= (1<<7)-1) {
        enc[0] = (REDIS_RDB_ENCVAL<<6) | REDIS_RDB_ENC_INT8;
        enc[1] = value&0xFF;
        return 2;

    // 16 bit
    } else if (value >= -(1<<13) && value <= (1<<13)-1) {
        enc[0] = (REDIS_RDB_ENCVAL<<6) | REDIS_RDB_ENC_INT16;
        enc[1] = value&0xFF;
        enc[2] = (value>>8)&0xFF;
        return 3;

    // 32 bit
    } else if (value >= -(1<<31) && value <= (1<<31)-1) {
        enc[0] = (REDIS_RDB_ENCVAL<<6) | REDIS_RDB_ENC_INT32;
        enc[1] = value&0xFF;
        enc[2] = (value>>8)&0xFF;
        enc[3] = (value>>16)&0xFF;
        enc[4] = (value>>24)&0xFF;
        return 5;

    } else {
        return 0;
    }
}

/**
 * 提取被编码的整数，以 robj 格式返回
 * 如果设置了 encoded, 返回一个整数编码的字符串对象
 * 否则返回 raw 编码的字符串对象
 */
robj *rdbLoadIntegerObject(rio *rdb, int enctype, int encode) {
    unsigned char enc[4];
    long long val;

    // 提取值
    if (enctype == REDIS_RDB_ENC_INT8) {
        if (rioRead(rdb,enc,1) == 0) return NULL;
        val = (signed char)enc[0];

    } else if (enctype == REDIS_RDB_ENC_INT16) {
        uint16_t v;
        if (rioRead(rdb,enc,2) == 0) return NULL;
        v = enc[0] | (enc[1]<<8);
        val = (int16_t)v;

    } else if (enctype == REDIS_RDB_ENC_INT32) {
        uint32_t v;
        if (rioRead(rdb,enc,4) == 0) return NULL;
        v = enc[0] | (enc[1]<<8) | (enc[2]<<16) | (enc[3]<<24);
        val = (int32_t)v;
    } else {
        val = 0;
        redisPanic("Unknown RDB integer encoding type");
    }

    // 返回
    if (encode) {
        return createStringObjectFromLongLong(val);
    } else {
        return createObject(REDIS_STRING,sdsfromlonglong(val));
    }
}

/**
 * 字符串对象存储的是整数，将其转换成整数格式保存到 rdb 文件
 * 
 * 转换成功, 返回保存整数所需字节数
 * 转换失败, 返回 0
 */
int rdbTryIntegerEncoding(char *s, size_t len, unsigned char *enc) {
    long long value;
    char *endptr, buf[32];

    // 尝试将值转换成整数
    value = strtoll(s, &endptr, 10);
    if (endptr[0] != '\0') return 0;

    // 将整数再转回字符串
    ll2string(buf,sizeof(buf),value);

    // 比对两次转换的字符串和传入的字符串内容是否一致
    // 如果不一样, 转换失败
    if (strlen(buf) != len || memcmp(s,buf,len)) return 0;

    // 转换成功, 对整数进行编码
    return rdbEncodeInteger(value,enc);
}

/**
 * 尝试对字符串 s 进行压缩后写入 rdb 文件
 * 写入成功, 返回存入字符串所需字节数
 * 压缩失败, 返回 0
 * 写入失败, 返回 -1
 */
int rdbSaveLzfStringObject(rio *rdb, unsigned char *s, size_t len) {
    size_t comprlen, outlen;
    unsigned char byte;
    int n, nwritten = 0;
    void *out;

    // 压缩字符串
    if (len <= 4) return 0;
    outlen = len-4;
    if ((out = zmalloc(outlen+1)) == NULL) return 0;
    comprlen = lzf_compress(s,len,out,outlen);
    if (comprlen == 0) {
        zfree(out);
        return 0;
    }

    // 写入压缩方式
    byte = (REDIS_RDB_ENCVAL<<6)|REDIS_RDB_ENC_LZF;
    if ((n = rdbWriteRaw(rdb,&byte,1)) == -1) goto writeerr;
    nwritten += n;

    // 写入压缩后长度
    if ((n = rdbSaveLen(rdb,comprlen)) == -1) goto writeerr;
    nwritten += n;

    // 写入原字符串长度
    if ((n = rdbSaveLen(rdb,len)) == -1) goto writeerr;
    nwritten += n;

    // 写入压缩后的内容
    if ((n = rdbWriteRaw(rdb,out,comprlen)) == -1) goto writeerr;
    nwritten += n;

    zfree(out);

    return nwritten;

writeerr:
    zfree(out);
    return -1;
}

/**
 * 载入被 LZF 压缩的字符串, 将其写入字符串对象并返回
 */
robj *rdbLoadLzfStringObject(rio *rdb) {
    unsigned int len, clen;
    unsigned char *c = NULL;
    sds val = NULL;

    // 读入压缩后的长度
    if ((clen = rdbLoadLen(rdb,NULL)) == REDIS_RDB_LENERR) return NULL;

    // 读入压缩前字符串的长度
    if ((len = rdbLoadLen(rdb,NULL)) == REDIS_RDB_LENERR) return NULL;

    // 读取压缩内容
    if ((c = zmalloc(clen)) == NULL) goto err;
    if (rioRead(rdb,c,clen) == 0) goto err;

    // 创建 sds
    if ((val = sdsnewlen(NULL,len)) == NULL) goto err;

    // 解压缩, 得到字符串
    if (lzf_decompress(c,clen,val,len) == 0) goto err;
    zfree(c);

    // 创建字符串对象
    return createObject(REDIS_STRING,val);

err:
    zfree(c);
    sdsfree(val);
    return NULL;
}

/**
 * 将字符串写入 rdb 文件
 * 1. 内容为整数, 转成整数后存储
 * 2. 长度大于20, 压缩后存储
 * 3. 长度小于等于20, 直接存了
 * 
 * 返回保存字符串所需字节数
 */
int rdbSaveRawString(rio *rdb, unsigned char *s, size_t len) {
    int enclen;
    int n, nwritten = 0;

    // 整数存入
    if (len <= 11) {
        unsigned char buf[5];
        if ((enclen = rdbTryIntegerEncoding(s,len,buf)) > 0) {
            // 整数转换成功, 写入
            if (rdbWriteRaw(rdb,buf,enclen) == -1) return -1;
            return enclen;
        }
    }

    // 压缩存入
    if (server.rdb_compression && len > 20) {
        n = rdbSaveLzfStringObject(rdb,s,len);

        if (n == -1) return -1;
        if (n > 0) return n;
    }

    // 原样存入
    // 写入长度
    if((n = rdbSaveLen(rdb,len)) == -1) return -1;
    nwritten += n;
    // 写入内容
    if (len > 0) {
        if (rdbWriteRaw(rdb,s,len) == -1) return -1;
        nwritten += len;
    }

    return nwritten;
}

/**
 * 将整数转换成字符串, 并存入 rdb
 * 1. 优先可编码的字符串, 空间占用小
 * 2. 直接将整数转换成字符串
 * 
 * 返回字符串所需字节数
 */
int rdbSaveLongLongAsStringObject(rio *rdb, long long value) {
    unsigned char buf[32];
    int n, nwritten;

    // 尝试转换成编码的字符串
    int enclen = rdbEncodeInteger(value,buf);

    // 编码成功, 直接存储
    if (enclen > 0) {
        return rdbWriteRaw(rdb,buf,enclen);

    // 编码失败, 转字符串存储
    } else {
        // 转成字符串
        enclen = ll2string((char*)buf,32,value);
        redisAssert(enclen < 32);
        // 写入字符串长度
        if ((n = rdbSaveLen(rdb,enclen)) == -1) return -1;
        nwritten += n;

        // 写入字符串
        if ((n = rdbWriteRaw(rdb,buf,enclen)) == -1) return -1;
        nwritten += n;
    }

    return nwritten;
}

/**
 * 将字符串对象保存到 rdb 文件
 * 返回保存到 rdb 文件所需的字节数
 */
int rdbSaveStringObject(rio *rdb, robj *obj) {

    // 整数保存
    if (obj->encoding == REDIS_ENCODING_INT) {
        return rdbSaveLongLongAsStringObject(rdb,(long)obj->ptr);

    // 字符保存
    } else {
        redisAssertWithInfo(NULL,obj,sdsEncodedObject(obj));
        return rdbSaveRawString(rdb,obj->ptr,sdslen(obj->ptr));
    }
}

/**
 * 从 rdb 中载入一个字符串对象
 */
robj *rdbGenericLoadStringObject(rio *rdb, int encode) {
    int isencoded;
    uint32_t len;
    sds val;

    // 长度
    len = rdbLoadLen(rdb,&isencoded);

    // 特殊编码字符串
    if (isencoded) {
        switch(len){
        // 整数编码
        case REDIS_RDB_ENC_INT8:
        case REDIS_RDB_ENC_INT16:
        case REDIS_RDB_ENC_INT32:
            return rdbLoadIntegerObject(rdb,len,encode);
        
        // LZF 压缩
        case REDIS_RDB_ENC_LZF:
            return rdbLoadLzfStringObject(rdb);

        default:
            redisPanic("Unknown RDB encoding type");
        }
    }

    if (len == REDIS_RDB_LENERR) return NULL;

    // 执行到这里, 说明字符串既没有被压缩, 也不是整数
    val = sdsnewlen(NULL,len);
    if (len && rioRead(rdb,val,len) == 0) {
        sdsfree(val);
        return NULL;
    }

    return createObject(REDIS_STRING,val);
}

robj *rdbLoadStringObject(rio *rdb) {
    rdbGenericLoadStringObject(rdb,0);
}
robj *rdbLoadEncodedStringObject(rio *rdb) {
    rdbGenericLoadStringObject(rdb,1);
}

/**
 * 以字符串来保存双精度浮点数, 写入 rdb
 * 字符串的前 8 位为无符号整数值, 指定浮点数的长度
 * 但有以下特殊值, 做特殊情况处理
 * 253: 不是数字
 * 254：正无穷
 * 255：负无穷
 */
int rdbSaveDoubleValue(rio *rdb, double val) {
    unsigned char buf[128];
    int len;

    // 不是数字
    if (isnan(val)) {
        buf[0] = 253;
        len = 1;

    // 无穷
    } else if (!isfinite(val)) {
        buf[0] = (val < 0) ? 255 : 254;
        len = 1;

    // 浮点数转成字符串
    } else {
        snprintf((char*)buf+1,sizeof(buf)-1,"%.17g",val);
        buf[0] = strlen((char*)buf+1);
        len = buf[0]+1;
    }

    return rdbWriteRaw(rdb,buf,len);
}

/**
 * 载入字符串表示的双精度浮点数, 将值写入 val 中
 * 读取错误, 返回 -1
 * 成功, 返回 0
 */
int rdbLoadDoubleValue(rio *rdb, double *val) {
    char buf[256];
    unsigned char len;

    // 载入字符串长度
    if (rioRead(rdb,&len,1) == 0) return -1;

    switch(len){
    // 特殊值
    case 255: *val = R_NegInf; return 0;
    case 254: *val = R_PosInf; return 0;
    case 253: *val = R_Nan; return 0;
    // 载入字符串
    default:
        if (rioRead(rdb,buf,len) == 0) return -1;
        buf[len] = '\0';
        sscanf(buf,"lg",val);
        return 0;
    }
}

/**
 * 将对象的类型写入 rdb
 * 写入成功, 返回写入字符所需内存长度
 * 写入失败, 返回 -1
 */
int rdbSaveObjectType(rio *rdb, robj *o) {
    switch (o->type)
    {
    case REDIS_STRING:
        return rdbSaveType(rdb,REDIS_RDB_TYPE_STRING);

    case REDIS_LIST:
        if (o->encoding == REDIS_ENCODING_ZIPLIST) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_LIST_ZIPLIST);
        else if (o->encoding == REDIS_ENCODING_LINKEDLIST) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_LIST);
        else 
            redisPanic("Unknown list encoding");

    case REDIS_SET:
        if (o->encoding == REDIS_ENCODING_INTSET) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_SET_INTSET);
        else if (o->encoding == REDIS_ENCODING_HT) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_SET);
        else 
            redisPanic("Unknown set encoding");

    case REDIS_ZSET:
        if (o->encoding == REDIS_ENCODING_ZIPLIST) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_ZSET_ZIPLIST);
        else if (o->encoding == REDIS_ENCODING_SKIPLIST) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_ZSET);
        else 
            redisPanic("Unknown sorted set encoding");

    case REDIS_HASH:
        if (o->encoding == REDIS_ENCODING_ZIPLIST) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_HASH_ZIPLIST);
        else if (o->encoding == REDIS_ENCODING_HT) 
            return rdbSaveType(rdb,REDIS_RDB_TYPE_HASH);
        else 
            redisPanic("Unknown hash encoding");
    
    default:
        redisPanic("Unknown object type");
    }

    return -1;
}

/**
 * 载入对象的类型,并返回
 * 载入失败返回 -1
 */
int rdbLoadObjectType(rio *rdb) {
    int type;
    if ((type = rdbLoadType(rdb)) == -1) return -1;
    if (!rdbIsObjectType(type)) return -1;

    return type;
}

/**
 * 将给定对象 o 保存到 rdb 文件
 * 保存成功返回, 保存对象所用的字节数
 * 保存失败, 返回 0
 */
int rdbSaveObject(rio *rdb, robj *o) {
    int n, nwritten = 0;

    if (o->type == REDIS_STRING) {

        if ((n = rdbSaveStringObject(rdb,o)) == -1) return -1;
        nwritten += n;

    } else if (o->type == REDIS_LIST) {

        if (o->encoding == REDIS_ENCODING_ZIPLIST)  {

            // 计算长度, 写入 rdb
            size_t l = ziplistBlobLen((unsigned char*)o->ptr);

            if ((n = rdbSaveRawString(rdb,o->ptr,l)) == -1) return -1;
            nwritten += n;

        } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
            list *list = o->ptr;
            listIter *li;
            listNode *ln;

            // 写入节点数量
            if ((n = rdbSaveLen(rdb,listLength(list))) == -1) return -1;
            nwritten += n;

            // 遍历所有节点, 写入
            listRewind(list, &li);
            while ((ln = listNext(&li))) {
                robj *eleobj = listNodeValue(ln);
                if ((n = rdbSaveStringObject(rdb,eleobj)) == -1) return -1;
                nwritten += n;
            }
            
        } else {
            redisPanic("Unknown list encoding");
        }

    } else if (o->type == REDIS_SET) {

        if (o->encoding == REDIS_ENCODING_INTSET) {
            // 计算长度, 写入
            size_t l = intsetBlobLen((intset*)o->ptr);

            if ((n = rdbSaveRawString(rdb,o->ptr,l)) == -1) return -1;
            nwritten += n;

        } else if (o->encoding == REDIS_ENCODING_HT) {
            dict *set = o->ptr;
            dictIterator *di = dictGetIterator(set);
            dictEntry *de;

            // 计算长度
            if ((n = rdbSaveLen(rdb,dictSize(set))) == -1) return -1;
            nwritten += n;

            // 写入节点
            while ((de = dictNext(di)) != NULL) {
                robj *eleobj = dictGetKey(de);
                // 写入值
                if ((n = rdbSaveStringObject(rdb,eleobj)) == -1) return -1;
                nwritten += n;
            }
            dictReleaseIterator(di);

        } else {
           redisPanic("Unknown set encoding");
        }

    } else if (o->type == REDIS_ZSET) {

        if (o->encoding == REDIS_ENCODING_ZIPLIST) {
            size_t l = ziplistBlobLen(o->ptr);

            if ((n = rdbSaveRawString(rdb,o->ptr,l)) == -1) return -1;
            nwritten += n;
            
        } else if (o->encoding == REDIS_ENCODING_SKIPLIST) {
            zset *zs = o->ptr;
            dictIterator *di = dictGetIterator(zs->dict);
            dictEntry *de;

            if ((n = rdbSaveLen(rdb,dictSize(zs->dict)) == -1)) return -1;
            nwritten += n;
            
            while ((de = dictNext(di)) != NULL) {
                robj *eleobj = dictGetKey(de);
                double *score = dictGetVal(de);

                // 写入成员
                if ((n = rdbSaveStringObject(rdb,eleobj)) == -1) return -1;
                nwritten += n;

                // 写入分值
                if ((n = rdbSaveDoubleValue(rdb,*score)) == -1) return -1;
                nwritten += n;
            }
            dictReleaseIterator(di);

        } else {
           redisPanic("Unknown sorted set encoding");
        }

    } else if (o->type == REDIS_HASH) {

        if (o->encoding == REDIS_ENCODING_ZIPLIST) {
            size_t l = ziplistBlobLen(o->ptr);

            if ((n = rdbSaveRawString(rdb,o->ptr,l)) == -1) return -1;
            nwritten += n;

        } else if (o->encoding == REDIS_ENCODING_HT) {
            dictIterator *di = dictGetIterator(d);
            dictEntry *de;

            // 计算长度
            if ((n = rdbSaveLen(rdb,dictSize((dict*)o->ptr))) == -1) return -1;
            nwritten += n;

            // 写入节点
            while ((de = dictNext(di)) != NULL) {
                robj *key = dictGetKey(de);
                robj *val = dictGetVal(de);
                // 写入键
                if ((n = rdbSaveStringObject(rdb,key)) == -1) return -1;
                nwritten += n;

                // 写入值
                if ((n = rdbSaveStringObject(rdb,val)) == -1) return -1;
                nwritten += n;
            }
            dictReleaseIterator(di);
        } else {
           redisPanic("Unknown hash encoding");
        }
    
    } else {

        redisPanic("Unknown object type");
    }
    
    return nwritten;
}

/**
 * 写入完整的键值对到 rdb
 * 写入包含：键、值、过期时间、类型
 * 写入成功返回 1, 当键已过期时, 返回 0
 * 写入出错, 返回 -1
 */
int rdbSaveKeyValuePair(rio *rdb, robj *key, robj *val,
                        long long expiretime, long long now)
{
    // 保存过期时间
    if (expiretime != -1) {

        if (expiretime < now) return 0;

        if (rdbSaveType(rdb,REDIS_RDB_OPCODE_EXPIRETIME_MS) == -1) return -1;
        if (rdbSaveMillisecondTime(rdb,expiretime) == -1) return -1;
    }

    // 保存类型，键，值
    if (rdbSaveObjectType(rdb,val) == -1) return -1;
    if (rdbSaveStringObject(rdb,key) == -1) return -1;
    if (rdbSaveObject(rdb,val) == -1) return -1;

    return 1;
}

/**
 * 将数据库保存到磁盘
 * 保存成功返回 REDIS_OK, 出错或失败返回 REDIS_ERR
 */
int rdbSave(char *filename) {
    dictIterator *di = NULL;
    dictEntry *de;
    char tmpfile[265];
    char magic[10];
    int j;
    long long now = mstime();
    FILE *fp;
    rio rdb;
    uint64_t cksum;

    // 创建临时文件
    snprintf(tmpfile,256,"temp-%d.rdb",(int)getpid());
    fp = fopen(tmpfile,"w");
    if (!fp) {
        redisLog(REDIS_WARNING, "Failed opening .rdb for saving: %s",
            strerror(errno));
        return REDIS_ERR;
    }

    // 初始化 I/O
    rioInitWithFile(&rdb,fp);

    // 设置校验和函数
    if (server.rdb_checksum)
        rdb.update_cksum = rioGenericUpdateChecksum;

    // 写入 起始位 和 RDB 版本号
    snprintf(magic,sizeof(magic),"REDIS%04d",REDIS_RDB_VERSION);
    // 遍历所有数据库
    for (j = 0; j < server.dbnum; j++) {

        // 要迭代的数据库
        redisDb *db = server.db+j;

        // 数据库的键空间
        dict *d = db->dict;

        // 跳过空数据库
        if (dictSize(d) == 0) continue;

        // 创建键空间迭代器
        di = dictGetIterator(d);
        if (!di) {
            fclose(fp);
            return REDIS_ERR;
        }
        
        // 写入数据库标识位
        if (rdbSaveType(&rdb,REDIS_RDB_OPCODE_SELECTDB) == -1) goto werr;

        // 写入数据库索引号
        if (rdbSaveLen(&rdb,j) == -1) goto werr;

        while ((de = dictNext(di)) != NULL) {
            sds keystr = dictGetKey(de);
            robj *key, *o = dictGetVal(de);
            long long expire;

            // 在栈中创建一个 key 对象
            initStaticStringObject(key,keystr);

            // 获取过期时间
            expire = getExpire(db,&key);

            // 添加键值对
            if (rdbSaveKeyValuePair(&rdb,&key,o,expire,now) == -1) goto werr;
        }
        dictReleaseIterator(di);
    }
    di = NULL;

    // 添加结束位
    if (rdbSaveType(&rdb,REDIS_RDB_OPCODE_EOF) == -1) goto werr;

    // 添加校验和
    cksum = rdb.cksum;
    memrev64ifbe(&cksum);
    rioWrite(&rdb,&cksum,8);
    
    // 刷缓存, 确保数据写入磁盘
    if (fflush(fp) == EOF) goto werr;
    if (fsync(fileno(fp)) == -1) goto werr;
    if (fclose(fp) == EOF) goto werr;

    // 更新文件名
    if (rename(tmpfile,filename) == -1) {
        redisLog(REDIS_WARNING,"Error moving temp DB file on the final destination: %s", strerror(errno));
        unlink(tmpfile);
        return REDIS_ERR;
    }

    // RDB 写入完成的日志
    redisLog(REDIS_NOTICE, "DB saved on disk");

    // 清零数据库脏状态
    server.dirty = 0;

    // 记录最后一次完成 save 时间
    server.lastsave = time(NULL);

    // 记录最后一次执行 save 的状态
    server.lastbgsave_status = REDIS_OK;

    return REDIS_OK;

werr:
    // 关闭文件
    fclose(fp);
    // 删除文件
    unlink(tmpfile);

    redisLog(REDIS_WARNING,"Write error saving DB on disk: %s", strerror(errno));

    if (di) dictReleaseIterator(di);

    return REDIS_ERR;
}

// bgsave
int rdbSaveBackground(char *filename) {
    pid_t childpid;
    long long start;

    // bgsave 正在执行, 直接 返回
    if (server.rdb_child_pid != -1) return REDIS_ERR;

    // 记录 bgsave 执行前数据库键改次数
    server.dirty_before_bgsave = server.dirty;

    // 记录最近一次尝试执行 bgsave 的时间
    server.lastbgsave_try = time(NULL);

    // fork 开始时间, 记录 fork 耗时
    start = ustime();

    if ((childpid = fork()) == 0) {
        // Child
        int retval;

        // 关闭网络连接 fd
        clostListeningSockets(0);

        // 设置进程的标题, 方便识别
        redisSetProcTitle("redis-rdb-bgsave");

        // 执行保存操作
        retval = rdbSave(filename);

        // 打印 copy-on-write 时使用的内存数
        if (retval == REDIS_OK) {
            size_t private_dirty = zmalloc_get_private_dirty();

            if (private_dirty) {
                redisLog(REDIS_NOTICE,
                    "RDB: %zu MB of memory used by copy-on-write",
                    private_dirty/(1024*1024));
            }
        }

        // 向父进程发送信号
        exitFromChild((retval == REDIS_OK) ? 0 : 1);

    } else {
        // Parent
        // 计算 fork 耗时
        server.stat_fork_time = ustime()-start;

        // fork 出错, 退出
        if (childpid == -1) {
            server.lastbgsave_status = REDIS_ERR;
            redisLog(REDIS_WARNING,"Can't save in background: fork: %s",
                strerror(errno));
            return REDIS_ERR;
        }

        // 打印 bgsave 开始日志
        redisLog(REDIS_NOTICE,"Background saving started by pid %d",childpid);

        // 记录数据库开始 bgsave 的时间
        server.rdb_save_time_start = time(NULL);

        // 记录负责执行 bgsave 的子进程 ID
        server.rdb_child_pid = childpid;

        // 关闭自动 rehash
        updateDictResizePolicy();

        return REDIS_OK;
    }

    return REDIS_OK;
}

/**
 * 溢出 bgsave 产生的临时文件
 * bgsave 执行被中断时使用
 */
void rdbRemoveTempFile(pid_t childpid) {
    char tmpfile[256];

    snprintf(tmpfile,256,"temp-%d.rdb",(int)childpid);
    unlink(tmpfile);
}

/**
 * 从 rdb 文件中载入指定类型的对象
 * 读取成功返回对象, 否则返回 NULL
 */
robj *rdbLoadObject(int rdbtype, rio *rdb) {
    robj *o, *ele, *dec;
    size_t len;
    unsigned int i;

    if (rdbtype == REDIS_RDB_TYPE_STRING) {
        if ((o = rdbLoadEncodedStringObject(rdb)) == NULL) return NULL;
        o = tryObjectEncoding(o);

    } else if (rdbtype == REDIS_RDB_TYPE_LIST) {
        // 读取列表节点数
        if ((len = rdbLoadLen(rdb,NULL)) == REDIS_RDB_LENERR) return NULL;

        // 创建对象
        if (len > server.list_max_ziplist_entries)
            o = createListObject();
        else
            o = createZiplistObject();

        // 添加节点
        while (len--) {
            // 载入字符串对象
            if ((ele = rdbLoadEncodedStringObject(rdb)) == NULL) return NULL;

            // 对象转码
            if (o->encoding == REDIS_ENCODING_ZIPLIST &&
                sdsEncodedObject(ele) &&
                sdslen(ele) > server.list_max_ziplist_value)
                    listTypeConvert(o,REDIS_ENCODING_LINKEDLIST);

            // 添加节点
            if (o->encoding == REDIS_ENCODING_ZIPLIST) {
                dec = getDecodedObject(ele);

                o->ptr = ziplistPush(o->ptr,dec->ptr,sdslen(dec->ptr),ZIPLIST_TAIL);

                decrRefCount(dec);
                decrRefCount(ele);

            } else {
                ele = tryObjectEncoding(ele);
                listAddNodeTail(o->ptr,ele);
            }
        }

    } else if (rdbtype == REDIS_RDB_TYPE_SET) {
        // 获取节点数量
        if ((len = rdbLoadLen(rdb,NULL)) == REDIS_RDB_LENERR) return NULL;

        // 创建集合对象
        if (len > server.set_max_intset_entries) {
            o = createSetObject();

            if (len > DICT_HT_INITIAL_SIZE)
                dictExpand(o->ptr,len);
        } else {
            o = createIntsetObject();
        }

        // 添加节点
        for (i = 0; i < len; i++) {
            long long llval;

            // 载入节点
            if ((ele = rdbLoadEncodedStringObject(rdb)) == NULL) return NULL;
            ele = tryObjectEncoding(ele);

            if (o->encoding == REDIS_ENCODING_INTSET) {
                if (isObjectRepresentableAsLongLong(ele,&llval) == REDIS_OK) {
                    intsetAdd(o->ptr,llval,NULL);
                } else {
                    setTypeConvert(o,REDIS_ENCODING_HT);
                    dictExpand(o->ptr,len);
                }
            } 

            if (o->encoding == REDIS_ENCODING_HT) {
                dictAdd((dict*)o->ptr,ele,NULL);
            } else {
                decrRefCount(ele);
            }
        }
        
    } else if (rdbtype == REDIS_RDB_TYPE_ZSET) {
        size_t zsetlen;
        size_t maxelelen = 0;
        zset *zs;

        // 载入节点数量
        if ((zsetlen = rdbLoadLen(rdb,NULL)) == REDIS_RDB_LENERR) return NULL;

        // 创建有序集合
        o = createZsetObject();
        zs = o->ptr;

        // 添加节点
        while (zsetlen--) {
            robj *ele;
            double score;
            zskiplistNode *znode;

            // 载入元素成员
            if ((ele = rdbLoadEncodedStringObject(rdb)) == NULL) return NULL;
            ele = tryObjectEncoding(ele);

            // 载入元素分值
            if (rdbLoadDoubleValue(rdb,&score) == -1) return NULL;

            // 记录成员的最大长度
            if (sdsEncodedObject(ele) && sdslen(ele->ptr) > maxelelen)
                maxelelen = sdslen(ele->ptr);

            // 将元素插入到跳跃表
            znode = zslInsert(zs->zsl,score,ele);

            // 元素添加到字典
            dictAdd(zs->dict,ele,&znode->score);

            incrRefCount(ele);

            if (zsetLength(o) <= server.zset_max_ziplist_entries &&
                maxelelen <= server.zset_max_ziplist_value)
                    zsetConvert(o,REDIS_ENCODING_ZIPLIST);
        }

    } else if (rdbtype == REDIS_RDB_TYPE_HASH) {
        size_t len;
        int ret;

        // 节点数量
        if ((len = rdbLoadLen(rdb,NULL)) == REDIS_RDB_LENERR) return NULL;

        // 创建哈希表
        o = createHashObject();

        if (len > server.list_max_ziplist_entries)
            hashTypeConvert(o,REDIS_ENCODING_HT);

        // 添加节点
        while (o->encoding == REDIS_ENCODING_ZIPLIST && len > 0) {
            robj *field, *value;
            len--;

            // 载入域
            field = rdbLoadStringObject(rdb);
            if (field == NULL) return NULL;
            redisAssert(sdsEncodedObject(field));

            // 载入值
            value = rdbLoadStringObject(rdb);
            if (value == NULL) return NULL;
            redisAssert(sdsEncodedObject(value));

            // 添加域和值
            o->ptr = ziplistPush(o->ptr,field->ptr,sdslen(field->ptr),ZIPLIST_TAIL);
            o->ptr = ziplistPush(o->ptr,value->ptr,sdslen(value->ptr),ZIPLIST_TAIL);

            // 是否转码
            if (sdslen(field->ptr) > server.hash_max_ziplist_value ||
                sdslen(value->ptr) > server.hash_max_ziplist_value)
            {
                decrRefCount(field);
                decrRefCount(value);
                hashTypeConvert(o, REDIS_ENCODING_HT);
                break;
            }
            decrRefCount(field);
            decrRefCount(value);
        }

        while (o->encoding == REDIS_ENCODING_HT && len > 0) {
            robj *field, *value;
            len--;

            field = rdbLoadEncodedStringObject(rdb);
            if (field == NULL) return NULL;
            value = rdbLoadEncodedStringObject(rdb);
            if (value == NULL) return NULL;

            field = tryObjectEncoding(field);
            value = tryObjectEncoding(value);

            ret = dictAdd((dict*)o->ptr,field, value);
            redisAssert(ret == REDIS_OK);
        }

        redisAssert(len == 0);

    } else if (rdbtype == REDIS_RDB_TYPE_HASH_ZIPMAP  ||
               rdbtype == REDIS_RDB_TYPE_LIST_ZIPLIST ||
               rdbtype == REDIS_RDB_TYPE_SET_INTSET   ||
               rdbtype == REDIS_RDB_TYPE_ZSET_ZIPLIST ||
               rdbtype == REDIS_RDB_TYPE_HASH_ZIPLIST)) 
    {

        // 载入字符串对象
        robj *aux = rdbLoadStringObject(rdb);
        if (aux == NULL) return NULL;
        o = createObject(REDIS_STRING,NULL);
        o->ptr = zmalloc(sdslen(aux->ptr));
        memcpy(o->ptr,aux->ptr,sdslen(aux->ptr));
        decrRefCount(aux);
        
        // 载入值对象
        switch(rdbtype) {
        case REDIS_RDB_TYPE_HASH_ZIPMAP:
            {
                // 创建 ZIPLIST
                unsigned char *zl = ziplistNew();
                unsigned char *zi = zipmapRewind(o->ptr);
                unsigned char *fstr, *vstr;
                unsigned int flen, vlen;
                unsigned int maxlen = 0;

                // 从 2.6 开始， HASH 不再使用 ZIPMAP 来进行编码
                // 所以遇到 ZIPMAP 编码的值时，要将它转换为 ZIPLIST

                // 从字符串中取出 ZIPMAP 的域和值，然后推入到 ZIPLIST 中
                while ((zi = zipmapNext(zi, &fstr, &flen, &vstr, &vlen)) != NULL) {
                    if (flen > maxlen) maxlen = flen;
                    if (vlen > maxlen) maxlen = vlen;
                    zl = ziplistPush(zl, fstr, flen, ZIPLIST_TAIL);
                    zl = ziplistPush(zl, vstr, vlen, ZIPLIST_TAIL);
                }

                zfree(o->ptr);

                // 设置类型、编码和值指针
                o->ptr = zl;
                o->type = REDIS_HASH;
                o->encoding = REDIS_ENCODING_ZIPLIST;

                // 是否需要从 ZIPLIST 编码转换为 HT 编码
                if (hashTypeLength(o) > server.hash_max_ziplist_entries ||
                    maxlen > server.hash_max_ziplist_value)
                {
                    hashTypeConvert(o, REDIS_ENCODING_HT);
                }
            }
            break;
        
        // ziplist 编码的列表
        case REDIS_RDB_TYPE_LIST_ZIPLIST:
            o->type = REDIS_LIST;
            o->encoding = REDIS_ENCODING_ZIPLIST;
            // 检查是否需要转换编码
            if (ziplistLen(o->ptr) > server.list_max_ziplist_entries)
                listTypeConvert(o,REDIS_ENCODING_LINKEDLIST);
            break;

        // intset 编码的集合
        case REDIS_RDB_TYPE_SET_INTSET:
            o->type = REDIS_SET;
            o->encoding = REDIS_ENCODING_INTSET;
            // 转换编码
            if (intsetLen(o->ptr) > server.set_max_intset_entries)
                setTypeConvert(o,REDIS_ENCODING_HT);
            break;

        // ziplist 编码的有序集合
        case REDIS_RDB_TYPE_ZSET_ZIPLIST:
            o->type = REDIS_ZSET;
            o->encoding = REDIS_ENCODING_ZIPLIST;
            // 检查是否需要转换编码
            if (ziplistLen(o->ptr) > server.zset_max_ziplist_entries)
                zsetConvert(o,REDIS_ENCODING_SKIPLIST);
            break;

        // ziplist 编码的哈希表
        case REDIS_RDB_TYPE_HASH_ZIPLIST:
            o->type = REDIS_HASH;
            o->encoding = REDIS_ENCODING_ZIPLIST;
            // 检查是否需要转换编码
            if (hashTypeLength(o) > server.hash_max_ziplist_entries)
                listTypeConvert(o,REDIS_ENCODING_HT);
            break;

        default:
            redisPanic("Unknown encoding");
            break;
        }

    } else {
        redisPanic("Unknown object type");
    }

    return o;
}

/**
 * 在全局状态中标记程序正在进行载入
 * 并设置相应的载入状态
 */
void startLoading(FILE *fp) {
    struct stat sb;

    server.loading = 1;

    // 开始进行载入的时间
    server.loading_start_time = time(NULL);

    // 文件的大小
    if (fstat(fileno(fp),&sb) == -1) {
        server.loading_total_bytes = 1;
    } else {
        server.loading_total_bytes = sb.st_size;
    }
}

/**
 * 刷新载入进度信息
 */
void loadingProgress(off_t pos) {
    server.loading_loaded_bytes = pos;
    if (server.stat_peak_memory < zmalloc_used_memory())
        server.stat_peak_memory = zmalloc_used_memory();
}

/**
 * 关闭服务器载入状态
 */
void stopLoading(void) {
    server.loading = 0;
}