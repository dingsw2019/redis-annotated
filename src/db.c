#include "redis.h"

#include <signal.h>
#include <ctype.h>

/*------------------------- 暂未看到这里, 看过后再写 --------------------------*/


robj *lookupKeyWrite(redisDb *db, robj *key) {

    return key;
}

void setKey(redisDb *db, robj *key, robj *val) {

}

void setExpire(redisDb *db, robj *key, long long when) {
    
}