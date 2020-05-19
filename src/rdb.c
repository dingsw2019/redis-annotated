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
