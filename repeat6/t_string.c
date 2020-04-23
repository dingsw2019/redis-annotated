#include "redis.h"
#include <math.h>


// 单个字符串长度是否超过最大限制的判断
// 成功返回 REDIS_OK, 失败返回 REDIS_ERR 同时向客户端报错
static int checkStringLength(redisClient *c, long long size) {
    if (size >= 512*1024*1024) {
        addReplyError(c, "string exceeds maximum allowed size (512MB)");
        return REDIS_ERR;
    }

    return REDIS_OK;
}

#define REDIS_SET_NO_FLAGS 0
#define REDIS_SET_NX (1<<0)
#define REDIS_SET_XX (1<<1)

// set setnx setex psetex 的通用函数
// flags 决定 nx xx 参数
// unit , expire决定时间量
void setGenericCommand(redisClient *c, int flags, robj *key, robj *val, robj *expire, int unit, robj *ok_reply, robj *abort_reply) {
    long long milliseconds = 0;

    // 提取过期时间量
    if (expire) {

        if (getLongLongFromObjectOrReply(c, expire, &milliseconds, NULL) != REDIS_OK)
            return;
        
        if (milliseconds <= 0) {
            addReplyError(c, "invalid expire time in SETEX");
            return;
        }

        // 统一转换成毫秒
        if (unit == UNIT_SECONDS) milliseconds *= 1000;
    }

    // 检查nx, xx 条件是否满足
    if (((flags & REDIS_SET_NX) && lookupKeyWrite(c->db, key) != NULL) ||
        ((flags & REDIS_SET_XX) && lookupKeyWrite(c->db, key) == NULL)) {
        
        addReply(c, abort_reply ? abort_reply : shared.nullbulk);
        return;
    }

    // kv关联
    setKey(c->db,key,val);

    // 更新键变更次数
    server.dirty++;

    // 过期时间设置
    if (expire) setExpire(c->db,key,mstime()+milliseconds);


    // 设置事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", key, c->db->id);

    // 过期时间设置时间通知
    if (expire) notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC, "expire", key, c->db->id);

    // 回复客户端
    addReply(c, ok_reply ? ok_reply : shared.ok);
}

/* SET key value [NX] [XX] [EX <seconds>] [PX <milliseconds>] */
void setCommand(redisClient *c) {
    int flags = REDIS_SET_NO_FLAGS;
    int unit = UNIT_SECONDS;
    robj *expire = NULL;
    int j;

    for (j = 3; j < c->argc; j++) {
        char *a = c->argv[j]->ptr;
        robj *next = (j == c->argc-1) ? NULL : c->argv[j+1];

        // NX
        if ((a[0] == 'n' || a[0] == 'N') && 
            (a[1] == 'x' || a[1] == 'X') && a[2] == '\0') {
            flags |= REDIS_SET_NX;
        // XX
        } else if ((a[0] == 'x' || a[0] == 'X') && 
            (a[1] == 'x' || a[1] == 'X') && a[2] == '\0') {
            flags |= REDIS_SET_XX;

        // EX
        } else if ((a[0] == 'e' || a[0] == 'E') && 
            (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && next) {
            expire = next;
            unit = UNIT_SECONDS;
            j++;

        // PX
        } else if ((a[0] == 'p' || a[0] == 'P') && 
            (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && next) {
            expire = next;
            unit = UNIT_MILLISECONDS;
            j++;
        
        } else {
            addReply(c, shared.syntaxerr);
            return;
        }
    }

    // 压缩值
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    // 设置
    setGenericCommand(c, flags, c->argv[1],c->argv[2],expire,unit, NULL, NULL);
}

// SETNX key value
void setnxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c, REDIS_SET_NX, c->argv[1],c->argv[2],NULL,0,shared.cone,shared.czero);
}

// SETEX key seconds value
void setexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, REDIS_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_SECONDS,NULL,NULL);
}

// PSETEX key milliseconds value
void psetexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, REDIS_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_MILLISECONDS,NULL,NULL);
}

// GET key
int getGenericCommand(redisClient *c) {
    robj *o;

    // 获取值, 值不存在返回客户端
    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk)) == NULL)
        return;

    // 检查值类型是否为字符串
    if (o->type != REDIS_STRING) {
        addReply(c, shared.wrongtypeerr);
        return REDIS_ERR;

    } else {
        // 返回值
        addReplyBulk(c, o);
        return REDIS_OK;
    }
}

void getCommand(redisClient *c) {
    getGenericCommand(c);
}