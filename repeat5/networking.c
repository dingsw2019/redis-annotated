#include "redis.h"
#include <math.h>

/*------------------------- 暂未看到这里, 看过后再写 --------------------------*/

void addReply(redisClient *c, robj *obj) {

}

void addReplyString(redisClient *c, char *s, size_t len) {

}

/**
 * 向客户端回复一个错误
 * 
 * 例如 -ERR unknown command 'foobar'
 */
void addReplyError(redisClient *c, char *err) {
    // 暂未看到这里, 看过后再写
}

/**
 * Add a Redis Object as a bulk reply
 * 
 * 返回一个 Redis 对象作为回复
 */
void addReplyBulk(redisClient *c, robj *obj) {

}

/**
 * 返回一个 C 缓冲区作为回复
 */
void addReplyBulkCBuffer(redisClient *c, void *p, size_t len) {

}

/**
 * 返回一个 C 字符串作为回复
 */
void addReplyBulkCString(redisClient *c, char *s) {

}

/**
 * 回复客户端一个整数
 * 格式：10086\r\n
 */
void addReplyLongLong(redisClient *c, long long ll) {

}

/**
 * 创建回复客户端的多个块
 */
void addReplyMultiBulkLen(redisClient *c, long length) {

}

// 修改单个参数
void rewriteClientCommandArgument(redisClient *c, int i, robj *newval) {

}

// 修改客户端的参数数组
void rewriteClientCommandVector(redisClient *c, int argc, ...) {

}