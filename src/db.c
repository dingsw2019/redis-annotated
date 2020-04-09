#include "redis.h"

#include <signal.h>
#include <ctype.h>

/*------------------------- 暂未看到这里, 看过后再写 --------------------------*/

/**
 * 在 db 中获取 key 的值对象
 * - 找到值对象, 更新命令信息, 返回值对象
 * - 未找到值对象, 更新未命中信息, 返回 NULL
 */
robj *lookupKeyRead(redisDb *db, robj *key) {

}

robj *lookupKeyWrite(redisDb *db, robj *key) {

    return key;
}

/**
 * 查找 key 的值对象
 * - key 存在, 返回 key 的值对象
 * - key 不存在, 向客户端发送 reply 信息, 返回 NULL
 */
robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply) {

    robj *o;

    return o;
}

/**
 * 向数据库添加键值对
 * 调用者负责增加 key 和 val 的引用计数
 * 如果键存在, 程序不处理
 */
void dbAdd(redisDb *db, robj *key, robj *val) {

}

robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o) {

    return o;
}

/**
 * 覆盖值对象, 但不修改过期时间
 * 如果键不存在, 不进行处理
 */
void dbOverwrite(redisDb *db, robj *key, robj *val) {

}

void setKey(redisDb *db, robj *key, robj *val) {

}

void setExpire(redisDb *db, robj *key, long long when) {

}


/**
 * 键对象改动的钩子
 * 
 * 当数据库的键改动是, 调用 signalModifiedKey()
 * 
 * 当数据库被清空是, 调用 signalFlushDb()
 */
void signalModifiedKey(redisDb *db, robj *key) {

}

void signalFlushDb(int dbid) {

}