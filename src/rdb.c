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