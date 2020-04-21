#include "redis.h"

/*------------------------- 暂未看到这里, 看过后再写 --------------------------*/

int keyspaceEventsStringToFlags(char *classes) {

    return 0;
}

sds keyspaceEventsFlagsToString(int flags) {
    sds res;

    return res;
}

void notifyKeyspaceEvent(int type, char *event, robj *key, int dbid) {

}