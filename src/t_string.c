#include "redis.h"
#include <math.h>

/**
 * 检查给定字符串长度是否超过512M
 * - 超过返回 REDIS_ERR
 * - 未超过返回 REDIS_OK
 */
static int checkStringLength(redisClient *c, long long size) {

    if (size > 512*1024*1024) {
        addReplyError(c, "string exceeds maximum allowed size (512MB)");;
        return REDIS_ERR;
    }

    return REDIS_OK;
}

// SET 命令的 NX, XX 参数的常量
#define REDIS_SET_NO_FLAGS 0
#define REDIS_SET_NX (1<<0)
#define REDIS_SET_XX (1<<1)

/**
 * SET 命令统一处理函数, EX XX expire 都可以处理
 */
void setGenericCommand(redisClient *c, int flags, robj *key, robj *val, robj *expire, int unit, robj *ok_reply, robj *abort_reply) {

    // 记录过期时间
    long long milliseconds = 0;

    // 取出过期时间
    if (expire) {

        // 提取整数值失败
        if (getLongLongFromObject(expire, &milliseconds) != REDIS_OK)
            return;

        // 错误的整数
        if (milliseconds <= 0) {
            addReplyError(c,"invalid expire time in SETEX");
            return;
        }

        // 不论输入的过期时间是秒还是毫秒
        // Redis 都以毫秒来保存过期时间
        // 如果单位是秒, 将值转换为毫秒
        if (unit == UNIT_SECONDS) milliseconds *= 1000;
    }

    // 如果设置 NX 或 XX 参数, 检查字符串对象是否满足设置要求
    // 在条件不符合时报错, 报错内容由 abort_reply 参数决定
    if ((flags & REDIS_SET_NX && lookupKeyWrite(c->db, key) != NULL) ||
        (flags & REDIS_SET_XX && lookupKeyWrite(c->db, key) == NULL))
    {
        addReply(c, abort_reply ? abort_reply : shared.nullbulk);
        return;
    }

    // 写入 value
    setKey(c->db, key, val);

    // 写入过期时间
    if (expire) setExpire(c->db, key, mstime()+milliseconds);

    // 发送字符串对象事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", key, c->db->id);

    // 发送过期时间事件通知
    if (expire)
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC, "expire", key, c->db->id);

    // 设置成功, 回复客户端
    // 回复内容优先 ok_reply
    addReply(c, ok_reply ? ok_reply : shared.ok);
}

/**
 * SET key value [NX] [XX] [EX seconds [PX <milliseconds>]
 * 执行 SET 命令
 * argc 参数数量, argv 参数值
 */
void setCommand(redisClient *c) {

    int j;
    robj *expire = NULL;
    int unit = UNIT_SECONDS;
    int flags = REDIS_SET_NO_FLAGS;

    // 获取可选参数
    for (j = 3; j < c->argc; j++) {
        char *a = c->argv[j]->ptr;
        robj *next = (j == c->argc-1) ? NULL : c->argv[j+1];

        // 获取 NX
        if ((a[0] == 'n' || a[0] == 'N') && 
            (a[1] == 'x' || a[1] == 'X') && a[2] == '\0') {
            flags |= REDIS_SET_NX;

        // 获取 XX
        } else if ((a[0] == 'x' || a[0] == 'X') && 
                   (a[1] == 'x' || a[1] == 'X') && a[2] == '\0') {
            flags |= REDIS_SET_XX;

        // 获取 EX 或 PX
        } else if ((a[0] == 'e' || a[0] == 'E') && 
                   (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && next) {
            unit = UNIT_SECONDS;
            expire = next;
            j++;

        } else if ((a[0] == 'p' || a[0] == 'P') && 
                   (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && next) {
            unit = UNIT_MILLISECONDS;
            expire = next;
            j++;
        } else {
            addReply(c, shared.syntaxerr);
            return;
        }
    }

    // 尝试对值进行压缩
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    setGenericCommand(c, flags, c->argv[1], c->argv[2], expire, unit, NULL, NULL);
}

// SET if Not eXists
// key不存在就添加, 否则不添加
void setnxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c, REDIS_SET_NX, c->argv[1], c->argv[2], NULL, 0, shared.cone, shared.czero);
}

// SETEX mykey 10 "Hello"
// 设置字符串对象同时设置, 秒级的过期时间
void setexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, REDIS_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_SECONDS, NULL, NULL);
}

// PSETEX mykey 1000 "Hello"
// 设置字符串对象同时设置, 毫秒级别的过期时间
void psetexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, REDIS_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_MILLISECONDS, NULL, NULL);
}