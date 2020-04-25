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

// GETSET key value
void getsetCommand(redisClient *c) {
    robj *o;

    // 获取 key 的值并返回给客户端
    if (getGenericCommand(c) == REDIS_ERR) return;

    // 压缩 value
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    // 写入 value
    setKey(c->db,c->argv[1],c->argv[2]);

    // 更新键修改次数
    server.dirty++;

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", c->argv[1], c->db->id);
}

// SETRANGE key offset value
void setrangeCommand(redisClient *c) {
    long offset;
    robj *o;
    sds value = c->argv[3]->ptr;

    // 提取 offset
    if (getLongFromObjectOrReply(c, c->argv[2], &offset, NULL) != REDIS_OK)
        return;
    
    if (offset <= 0) {
        addReplyError(c, "offset is out of range");
        return;
    }

    // 获取 key 的值
    o = lookupKeyWrite(c->db, c->argv[1]);
    // 新增值
    if (o == NULL) {

        // 确定 value 长度
        if (sdslen(value) == 0) {
            addReply(c,shared.czero);
            return;
        }

        // 确定 value 填充的字符串长度不会超过限制
        if (checkStringLength(c, offset+sdslen(value)) != REDIS_OK) {
            return;
        }

        // 创建空sdshdr, 创建字符串对象
        o = createObject(REDIS_STRING, sdsempty());
        dbAdd(c->db, c->argv[1], o);
    // 编辑值
    } else {
        size_t olen;

        // 原 value 对象类型判断
        if (checkType(c, o, REDIS_STRING))
            return;

        // 获取原 value 的长度
        olen = stringObjectLen(o);

        // 检查 value 长度是否大于 0
        if (sdslen(value) == 0) {
            addReplyLongLong(c, olen);
            return;
        }

        // 检查 value 填充字符串是否会超过长度限制
        if (checkStringLength(c, offset+sdslen(value)) != REDIS_OK) {
            return;
        }

        // 修改 kv 映射关系
        o = dbUnshareStringValue(c->db, c->argv[1], o);
    }

    // 将 value 写入值对象
    if (sdslen(value) > 0) {

        // 空白符填充长度
        o->ptr = sdsgrowzero(o->ptr, offset+sdslen(value));
        
        // value 拷贝
        memcpy((char*)o->ptr+offset, value, sdslen(value));

        // 更新键变更次数
        server.dirty++;

        // 值变更通知
        signalModifiedKey(c->db, c->argv[1]);

        // 发出事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "setrange", c->argv[1], c->db->id);
    }

    // 回复客户端值长度
    addReplyLongLong(c, sdslen(o->ptr));
}

// GETRANGE key start end
void getrangeCommand(redisClient *c) {
    long start, end;
    char *str, llbuf[32];
    size_t strlen;
    robj *o;

    // 提取 start, end 值
    if (getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != REDIS_OK)
        return;
    if (getLongFromObjectOrReply(c,c->argv[3],&end,NULL) != REDIS_OK)
        return;

    // 提取 key 的值
    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.emptybulk)) == NULL ||
        checkType(c, o, REDIS_STRING)) {
        return;
    }

    // 值的长度和内容
    if (o->encoding == REDIS_ENCODING_INT) {
        str = llbuf;
        strlen = ll2string(llbuf, sizeof(llbuf), (long)o->ptr);
    } else {
        str = o->ptr;
        strlen = sdslen(o->ptr);
    }

    // 转换 start, end
    if (start < 0) start += strlen;
    if (end < 0) end += strlen;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((unsigned)end >= strlen) end = strlen-1;

    // 回复客户端
    if (start > end) {
        addReply(c,shared.emptybulk);
    } else {
        addReplyBulkCBuffer(c, (char*)str+start, end-start+1);
    }
}

// MGET key [key ...]
void mgetCommand(redisClient *c) {
    int j;

    addReplyMultiBulkLen(c, c->argc-1);
    // 遍历所有 key
    for (j=1; j<c->argc; j++) {
        
        // 获取 key 的值
        robj *o = lookupKeyRead(c->db, c->argv[j]);

        // 不存在值, 返回 null 给客户端
        if (o == NULL) {
            addReply(c, shared.nullbulk);
        } else {

            // 不是字符串类型, 返回类型错误给客户端 
            if (o->type != REDIS_STRING) {
                addReply(c, shared.wrongtypeerr);
            } else {
                addReplyBulk(c, o);
            }
        }
    }
}

// mset msetnx 的通用函数
void msetGenericCommand(redisClient *c, int nx) {
    int j, busykey = 0;
     
    // 参数不是奇数, 说明缺少参数
    if (c->argc % 2 == 0) {
        addReply(c, "wrong number of arguments for MSET");
        return;
    }

    // 设置 nx
    if (nx) {
   
        // 遍历 key, 记录存在值的 key 的数量
        for (j=1; j<c->argc; j+=2) {
            if (lookupKeyRead(c->db, c->argv[j]) != NULL) {
                busykey++;
            }
        }

        // 如果存在值, 回复错误
        if (busykey) {
            addReply(c, shared.czero);
            return;
        }
    }

    
    // 遍历 key, 存入值
    for (j=1; j<c->argc; j+=2) {

        // 压缩值空间
        c->argv[j+1] = tryObjectEncoding(c->argv[j+1]);

        // 存入值
        setKey(c->db, c->argv[j], c->argv[j+1]);

        // 发送事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", c->argv[j], c->db->id);
    }

    
    // 更新键变更数量
    server.dirty += (c->argc-1) / 2;

    // 回复客户端
    addReply(c, nx ? shared.cone : shared.ok);
}

void msetCommand(redisClient *c) {
    msetGenericCommand(c, 0);
}

void msetnxCommand(redisClient *c) {
    msetGenericCommand(c, 1);
}

// incr decr incrby decrby 的通用方法
void incrDecrCommand(redisClient *c, long long incr) {
    long long value, oldvalue;
    robj *o, *new;

    // 取出 key 的值对象的整数
    o = lookupKeyWrite(c->db, c->argv[1]);
    if (o != NULL && checkType(c, o, REDIS_STRING)) return;

    // 整数溢出检查
    if (getLongLongFromObjectOrReply(c, o, &value,NULL) != REDIS_OK) return;

    oldvalue = value;
    if ((incr < 0 && oldvalue < 0 && incr < (LLONG_MIN-oldvalue)) ||
        (incr > 0 && oldvalue > 0 && incr > (LLONG_MAX-oldvalue))) 
    {
        addReplyError(c,"increment or decrement would overflow");
        return;
    }

    // 新增或修改 key 的值
    value += incr;
    new = createStringObjectFromLongLong(value);
    if (o) {
        dbOverwrite(c->db, c->argv[1], new);
    } else {
        dbAdd(c->db, c->argv[1], new);
    }

    // 发出信号, 回复客户端
    signalModifiedKey(c->db,c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "incrby", c->argv[1], c->db->id);

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
    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr,NULL) != REDIS_OK) return;
    incrDecrCommand(c,incr);
}

void decrbyCommand(redisClient *c) {
    long long incr;
    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr,NULL) != REDIS_OK) return;
    incrDecrCommand(c,-incr);
}

// INCRBYFLOAT key increment
void incrbyfloatCommand(redisClient *c) {
    long double value,incr;
    robj *o, *new, *aux;

    // 提取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);
    if (o != NULL && checkType(c, o, REDIS_STRING)) return;

    // 提取值对象的浮点数, 提取 float
    if (getLongDoubleFromObjectOrReply(c, o, &value,NULL) != REDIS_OK ||
        getLongDoubleFromObjectOrReply(c, c->argv[2], &incr,NULL) != REDIS_OK)
    {
        return;
    }

    // 溢出检查
    value += incr;
    if (isnan(value) || isinf(value)) {
        addReplyError(c,"increment would produce NaN or Infinity");
        return;
    }

    // 新增或编辑浮点值
    new = createStringObjectFromLongDouble(value);
    if (o) {
        dbOverwrite(c->db, c->argv[1], new);
    } else {
        dbAdd(c->db, c->argv[1], new);
    }

    // 发送信号
    signalModifiedKey(c->db, c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "incrbyfloat", c->argv[1], c->db->id);

    server.dirty++;

    addReplyBulk(c,new);

    // todo 暂时不了解AOF代码, 未知部分
    aux = createStringObject("SET",3);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,2,new);
}

// APPEND key value
void appendCommand(redisClient *c) {
    robj *o, *append;
    size_t totlen;

    // 获取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);

    // 值对象不存在, 新增值对象
    if (o == NULL) {
        c->argv[2] = tryObjectEncoding(c->argv[2]);
        incrRefCount(c->argv[2]);
        dbAdd(c->db, c->argv[1], c->argv[2]);
        totlen = stringObjectLen(c->argv[2]);

    // 值对象存在, 追加字符串
    } else {
        if (checkType(c, o, REDIS_STRING))
            return;
        
        // 追加字符串
        append = tryObjectEncoding(c->argv[2]);
        // 溢出检查
        totlen = stringObjectLen(o) + sdslen(append->ptr);
        if (checkStringLength(c, totlen) != REDIS_OK)
            return;

        // 写入
        dbUnshareStringValue(c->db,c->argv[1],append);
        o->ptr = sdscatlen(o->ptr, append->ptr, sdslen(append->ptr));
        totlen = sdslen(o->ptr);
    }

    // 发出信号
    signalModifiedKey(c->db,c->argv[1]);

    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "append", c->argv[1], c->db->id);

    server.dirty++;

    addReplyLongLong(c,totlen);
}

void strlenCommand(redisClient *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c, c->argv[1], NULL)) == NULL ||
        checkType(c,o,REDIS_STRING))
        return;

    addReplyLongLong(c,stringObjectLen(o));
}