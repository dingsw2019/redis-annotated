#include "redis.h"
#include <math.h>

// 检查size 是否超过字符串最大长度限制 512M
// 如果超过, 回复客户端, 返回 REDIS_ERR。 否则返回 REDIS_OK
static int checkStringLength(redisClient *c, long long size) {
    if (size >= 512*1024*1024) {
        addReplyError(c,"string exceeds maximum allowed size (512MB)");
        return REDIS_ERR;
    }

    return REDIS_OK;
}

#define REDIS_SET_NO_FLAGS 0
#define REDIS_SET_NX (1<<0)
#define REDIS_SET_XX (1<<1)


void setGenericCommand(redisClient *c, int flags, robj *key, robj *val, 
                        robj *expire, int unit, robj *ok_reply, robj *abort_reply) {
    long long milliseconds = 0;
    
    // 提取过期时间
    if (expire) {
        
        if (getLongLongFromObjectOrReply(c, expire, &milliseconds, NULL) != REDIS_OK)
            return;

        if (milliseconds <= 0) {
            addReplyError(c,"invalid expire time in SETEX");
            return;
        }

        if (unit == UNIT_SECONDS) milliseconds *= 1000;
    }

    // 检查当前 kv 是否满足 flags 要求
    if (flags & REDIS_SET_NX && lookupKeyWrite(c->db,key) != NULL ||
        flags & REDIS_SET_XX && lookupKeyWrite(c->db,key) == NULL) {
        
        addReply(c, abort_reply ? abort_reply : shared.nullbulk);
        return;
    }

    // 写入 kv, expire
    setKey(c->db, key, val);
    if (expire) setExpire(c->db,key,mstime()+milliseconds);

    // 发送通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"set",key,c->db->id);
    if (expire) notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"expire",key,c->db->id);
    server.dirty++;

    addReply(c, ok_reply ? ok_reply : shared.ok);
}

// SET key value [EX seconds] [PX milliseconds] [NX|XX]
void setCommand(redisClient *c) {
    int flags = REDIS_SET_NO_FLAGS;
    int j,unit = UNIT_SECONDS;
    robj *expire;

    for (j=3; j<c->argc; j++) {
        char *a = c->argv[j]->ptr;
        robj *next = (j == c->argc-1) ? NULL : c->argv[j+1];

        if ((a[0] == 'n' || a[0]=='N') && 
            (a[1]=='x' || a[1]=='X') && a[2]=='\0') {
            flags |= REDIS_SET_NX;

        } else if ((a[0] == 'x' || a[0]=='X') && 
                   (a[1]=='x' || a[1]=='X') && a[2]=='\0') {
            flags |= REDIS_SET_XX;

        } else if ((a[0] == 'e' || a[0]=='E') && 
                   (a[1]=='x' || a[1]=='X') && a[2]=='\0' && next) {
            expire = next;
            unit = UNIT_SECONDS;
            j++;

        } else if ((a[0] == 'p' || a[0]=='P') && 
                   (a[1]=='x' || a[1]=='X') && a[2]=='\0' && next) {
            expire = next;
            unit = UNIT_MILLISECONDS;
            j++;

        } else {
            addReply(c,shared.syntaxerr);
            return;
        }
    }

    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c, flags, c->argv[1], c->argv[2], expire, unit, NULL, NULL);
}

void setnxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c, REDIS_SET_NX, c->argv[1], c->argv[2], NULL, 0, shared.cone, shared.czero);
}

void setexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, REDIS_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_SECONDS, NULL, NULL);
}

void psetexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, REDIS_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_MILLISECONDS, NULL, NULL);
}

// 获取 key 的值对象, 获取成功返回 REDIS_OK
// 否则回复客户端错误, 同时返回 REDIS_ERR
int getGenericCommand(redisClient *c) {
    robj *o;

    o = lookupKeyRead(c->db, c->argv[1]);

    if (o == NULL) {
        addReply(c,shared.nullbulk);
        return REDIS_ERR;

    } else {

        if (o->type != REDIS_STRING) {
            addReply(c,shared.wrongtypeerr);
            return REDIS_ERR;

        } else {
            addReplyBulk(c, o);
            return REDIS_OK;
        }
    }
}

void getCommand(redisClient *c) {
    getGenericCommand(c);
}

void getsetCommand(redisClient *c) {

    if (getGenericCommand(c) != REDIS_OK)
        return;

    // 压缩值空间
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    setKey(c->db, c->argv[1], c->argv[2]);

    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", c->argv[1], c->db->id);

    server.dirty++;
}

// SETRANGE key offset value
void setrangeCommand(redisClient *c) {
    robj *o;
    sds value = c->argv[3]->ptr;
    long offset;

    // 提取 offset
    if (getLongFromObjectOrReply(c, c->argv[2], &offset, NULL) != REDIS_OK)
        return;
    if (offset <= 0) {
        addReplyError(c,"offset is out of range");
        return;
    }

    // 查找值对象
    o = lookupKeyWrite(c, c->argv[1]);

    // 新增
    if (o == NULL) {
        if (sdslen(value) == 0) {
            addReply(c, shared.czero);
            return;
        }

        if (checkStringLength(c, offset + sdslen(value)) != REDIS_OK)
            return;

        o = createObject(REDIS_STRING,sdsempty());
        dbAdd(c->db, c->argv[1], o);
    // 编辑
    } else {
        size_t olen;

        if (checkType(c, o, REDIS_STRING))
            return;

        olen = stringObjectLen(o->ptr);

        if (sdslen(value) == 0) {
            addReplyLongLong(c,olen);
            return;
        }

        if (checkStringLength(c, offset + sdslen(value)) != REDIS_OK)
            return;

        o = dbUnshareStringValue(c->db, c->argv[1], o);
    }

    // 写入值
    if (sdslen(value)) {

        o->ptr = sdsgrowzero(o->ptr, offset+sdslen(value));

        memcpy((char*)o->ptr+offset, value, sdslen(value));

        server.dirty++;
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"setrange",c->argv[1],c->db->id);
    }

    addReplyLongLong(c, sdslen(o->ptr));
}

// GETRANGE key start end
void getrangeCommand(redisClient *c) {
    robj *o;
    long start, end;
    char *str, llbuf[32];
    size_t strlen;

    // 获取值对象
    if ((o == lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk)) == NULL ||
        checkType(c, o, REDIS_STRING))
        return;

    // 获取 start end
    if (getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != REDIS_OK ||
        getLongFromObjectOrReply(c,c->argv[3],&end,NULL) != REDIS_OK)
        return;

    if (o->encoding == REDIS_ENCODING_INT) {
        str = llbuf;
        strlen = ll2string(llbuf,sizeof(llbuf),(long)o->ptr);
    } else {
        str = o->ptr;
        strlen = sdslen(o->ptr);
    }

    // 负索引转正索引
    if (start < 0) start += strlen;
    if (end < 0) end += strlen;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((unsigned)end >= strlen) end = strlen-1;

    // 返回
    if (start > end) {
        addReply(c, shared.emptybulk);
    } else {
        addReplyBulkCBuffer(c, (char*)str+start, (end-start)+1);
    }
}

// MGET key [key ...]
void mgetCommand(redisClient *c) {
    robj *o;
    int j;

    addReplyMultiBulkLen(c, c->argc-1);
    for (j=1; j<c->argc; j++) {

        o = lookupKeyRead(c->db, c->argv[j]);

        if (o == NULL) {
            addReply(c,shared.nullbulk);
        } else {

            if (o->encoding != REDIS_STRING) {
                addReply(c,shared.wrongtypeerr);
            } else {
                addReplyBulk(c,o);
            }
        }
    }
}

// MSET key value [key value ...]
// MSETNX key value [key value ...]
void msetGenericCommand(redisClient *c, int nx) {
    int j, busykey = 0;

    // 偶数的参数数量, 参数错误, 返回
    if (c->argc % 2 == 0) {
        addReplyError(c, "wrong number of arguments for MSET");
        return;
    }

    // nx 检查, 存在一个值 , 报错返回
    if (nx) {
        for (j=1; j<c->argc; j+=2) {
            if (lookupKeyRead(c->db, c->argv[j]) != NULL) {
                busykey++;
            }

            if (busykey) {
                addReply(c, shared.czero);
                return;
            }
        }
    }

    // 写入值对象
    for (j=1; j<c->argc; j+=2) {

        c->argv[j+1] = tryObjectEncoding(c->argv[j+1]);

        setKey(c->db, c->argv[j], c->argv[j+1]);

        // 发送通知
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"set",c->argv[j],c->db->id);
    }

    // 更新键改次数
    server.dirty += (c->argc-1)/2;

    addReply(c, nx ? shared.cone : shared.ok);
}

void msetCommand(redisClient *c) {
    msetGenericCommand(c, 0);
}

void msetnxCommand(redisClient *c) {
    msetGenericCommand(c, 1);
}

void incrDecrCommand(redisClient *c, long long incr) {
    robj *o, *new;
    long long value, old_value;

    // 提取值对象和整数值
    o = lookupKeyWrite(c->db, c->argv[1]);
    if (o != NULL && checkType(c, o, REDIS_STRING)) return;

    if (getLongLongFromObjectOrReply(c, o, &value, NULL) != REDIS_OK) {
        return;
    }

    // 边界检查
    old_value = value;
    if ((incr < 0 && old_value < 0 && incr < (LLONG_MIN - old_value)) ||
        (incr > 0 && old_value > 0 && incr > (LLONG_MAX - old_value))) {
        
        addReplyError(c, "increment or decrement would overflow");
        return;
    }

    value += incr;
    new = createStringObjectFromLongLong(value);
    // 编辑
    if (o) {
        dbOverwrite(c->db, c->argv[1], new);

    // 新增
    } else {
        dbAdd(c->db, c->argv[1], new);
    }

    signalModifiedKey(c->db, c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"incrby",c->argv[1],c->db->id);

    server.dirty++;

    addReply(c,shared.colon);
    addReply(c,new);
    addReply(c,shared.crlf);
}

void incrCommand(redisClient *c) {
    incrDecrCommand(c,1);
}

void decrCommand(redisClient *c) {
    incrDecrCommand(c,-1);
}

void incrbyCommand(redisClient *c) {
    long long incr;
    if (getLongLongFromObjectOrReply(c,c->argv[2],&incr,NULL) == NULL)
        return;
    incrDecrCommand(c,incr);
}

void decrbyCommand(redisClient *c) {
    long long incr;
    if (getLongLongFromObjectOrReply(c,c->argv[2],&incr,NULL) == NULL)
        return;
    incrDecrCommand(c,-incr);
}

// INCRBYFLOAT key increment
void incrbyfloatCommand(redisClient *c) {
    robj *o, *new, *aux;
    long double value, incr;

    // 提取值对象和浮点值
    o = lookupKeyWrite(c->db,c->argv[1]);
    if (o != NULL && o->type != REDIS_STRING) return;

    // 提取 incr
    if (getLongDoubleFromObjectOrReply(c, o, &value,NULL) != REDIS_OK || 
        getLongDoubleFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK) {
        return ;
    }

    value += incr;
    if (isnan(value) || isinf(value)) {
        addReplyError(c,"increment would produce NaN or Infinity");
        return;
    }
    new = createStringObjectFromLongDouble(value);
    // 编辑
    if (o) {
        dbOverwrite(c->db, c->argv[1], new);

    // 新增
    } else {
        dbAdd(c->db, c->argv[1], new);
    }

    signalModifiedKey(c->db, c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "incrbyfloat", c->argv[1], c->db->id);

    server.dirty++;

    addReplyBulk(c,new);

    aux = createStringObject("SET",3);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,2,new);
}

void appendCommand(redisClient *c) {
    robj *o, *append;
    size_t totlen;

    // 提取值对象
    o = lookupKeyWrite(c->db, c->argv[1]);
    if (o != NULL && o->type != REDIS_STRING) return;


    // 编辑
    if (o) {
        append = tryObjectEncoding(c->argv[2]);
        totlen = sdslen(append->ptr) + stringObjectLen(o);
        if (checkStringLength(c, totlen) != REDIS_OK) 
            return;
        dbUnshareStringValue(c->db, c->argv[1],append);
        o->ptr = sdscatlen(o->ptr, append->ptr, sdslen(append->ptr));
        totlen = sdslen(o->ptr);

    // 新增
    } else {
        c->argv[2] = tryObjectEncoding(c->argv[2]);
        incrRefCount(c->argv[2]);
        dbAdd(c->db, c->argv[1], c->argv[2]);
        totlen = stringObjectLen(c->argv[2]);
    }

    signalModifiedKey(c->db, c->argv[1]);
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"append",c->argv[1],c->db->id);
    server.dirty++;

    addReplyLongLong(c,totlen);
}

void strlenCommand(redisClient *c) {
    robj *o;

    if (lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk) == NULL ||
        checkType(c, o, REDIS_STRING))
        return;
    
    addReplyLongLong(c,stringObjectLen(o));
}