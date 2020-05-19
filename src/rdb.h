#ifndef __REDIS_RDB_H
#define __REDIS_RDB_H

#include <stdio.h>
#include "rio.h"
#include "redis.h"

// RDB 版本号, 当新版本不能兼容旧版本时, 版本增1
#define REDIS_RDB_VERSION 6

/**
 * 通过读取第一字节的最高 2 位来判断长度
 * 
 * 00|000000 长度在后 6 位
 * 01|000000 00000000 长度为14位
 * 10|000000 00000000 00000000 00000000 00000000 长度为后4字节(32位)保存
 */
#define REDIS_RDB_6BITLEN 0
#define REDIS_RDB_14BITLEN 1
#define REDIS_RDB_32BITLEN 2
// 11|000000 一个特殊编码的对象, 后 6 位指定对象的类型
// 类型详见 REDIS_RDB_ENC_*
#define REDIS_RDB_ENCVAL 3

// 表示读取/写入错误
#define REDIS_RDB_LENERR UINT_MAX

/**
 * 字符串对象的特殊编码
 * 最高两位之后的两位(第3、4位)指定对象的特殊编码
 */
#define REDIS_RDB_ENC_INT8 0
#define REDIS_RDB_ENC_INT16 1
#define REDIS_RDB_ENC_INT32 2
#define REDIS_RDB_ENC_LZF 3

/**
 * 对象类型在 RDB 文件的类型编码
 */
#define REDIS_RDB_TYPE_STRING 0
#define REDIS_RDB_TYPE_LIST 1
#define REDIS_RDB_TYPE_SET 2
#define REDIS_RDB_TYPE_ZSET 3
#define REDIS_RDB_TYPE_HASH 4

/**
 * 对象的编码方式
 */
#define REDIS_RDB_TYPE_HASH_ZIPMAP 9
#define REDIS_RDB_TYPE_LIST_ZIPLIST 10
#define REDIS_RDB_TYPE_SET_INTSET 11
#define REDIS_RDB_TYPE_ZSET_ZIPLIST 12
#define REDIS_RDB_TYPE_HASH_ZIPLIST 13

/**
 * 检查给定类型是否对象
 */
#define rdbIsObjectType(t) ((t >= 0 && t <= 4) || (t >= 9 && t <= 13))

/**
 * 数据库特殊操作标识符
 */
// 以 MS 计算的过期时间
#define REDIS_RDB_OPCODE_EXPIRETIME_MS 252
// 以秒计算的过期时间
#define REDIS_RDB_OPCODE_EXPIRETIME 253
// 选择数据库
#define REDIS_RDB_OPCODE_SELECTDB 254
// 数据库的结尾(不是 RDB 文件的结尾)
#define REDIS_RDB_OPCODE_EOF 255

int rdbSaveType(rio *rdb, unsigned char type);
int rdbLoadType(rio *rdb);
int rdbSaveTime(rio *rdb, time_t t);
time_t rdbLoadTime(rio *rdb);
int rdbSaveLen(rio *rdb, uint32_t len);
uint32_t rdbLoadLen(rio *rdb, int *isencoded);
int rdbSaveObjectType(rio *rdb, robj *o);
int rdbLoadObjectType(rio *rdb);
int rdbLoad(char *filename);
int rdbSaveBackground(char *filename);
void rdbRemoveTempFile(pid_t childpid);
int rdbSave(char *filename);
int rdbSaveObject(rio *rdb, robj *o);
off_t rdbSavedObjectLen(robj *o);
off_t rdbSavedObjectPages(robj *o);
robj *rdbLoadObject(int type, rio *rdb);
void backgroundSaveDoneHandler(int exitcode, int bysignal);
int rdbSaveKeyValuePair(rio *rdb, robj *key, robj *val, long long expiretime, long long now);
robj *rdbLoadStringObject(rio *rdb);

#endif