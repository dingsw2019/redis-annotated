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