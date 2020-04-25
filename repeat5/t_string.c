#include "redis.h"
#include <math.h>

// 字符串长度是否超过最大限制
// 如果超出向客户端发送错误, 返回 REDIS_ERR
// 未超出, 返回 REDIS_OK
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


// set setnx setex psetex通用函数
void setGenericCommand(redisClient *c, int flags, robj *key, robj *val, 
                        robj *expire, int unit, robj *ok_reply, robj *abort_reply) {
    
    long long milliseconds = 0;
    // 提取 expire 值
    if (expire) {

        if (getLongLongFromObjectOrReply(c, expire, &milliseconds, NULL) != REDIS_OK) {
            return;
        }

        if (milliseconds <= 0) {
            addReplyError(c,"invalid expire time in SETEX");
            return;
        }

        if (unit == UNIT_SECONDS) milliseconds *= 1000;
    }

    // 检查是否满足 flag
    if (flags & REDIS_SET_NX && lookupKeyWrite(c, key) != NULL ||
        flags & REDIS_SET_XX && lookupKeyWrite(c, key) == NULL) {
        
        addReply(c,abort_reply ? abort_reply : shared.nullbulk);
        return;
    }

    // 添加 kv
    setKey(c->db,key,val);

    // 设置 expire
    if (expire) setExpire(c->db,key,mstime()+milliseconds);

    // 发出事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"set",key,c->db->id);
    if (expire) notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"expire",key,c->db->id);

    // 更新键改次数
    server.dirty++;

    // 回复客户端
    addReply(c,ok_reply ? ok_reply : shared.ok);
}

void setCommand(redisClient *c) {
    int j;
    int flags = REDIS_SET_NO_FLAGS;
    robj *expire;
    int unit = UNIT_SECONDS;

    // 获取参数
    for (j=3; j<c->argc; j++) {
        char *a = c->argv[j]->ptr;
        robj *next = (j == c->argc-1) ? NULL : c->argv[j+1];

        // NX
        if ((a[0]=='n' || a[0]=='N') && 
            (a[1]=='x' || a[1]=='X') && a[2]=='\0') {
            flags |= REDIS_SET_NX;
        
        // XX
        } else if ((a[0]=='x' || a[0]=='X') && 
                   (a[1]=='x' || a[1]=='X') && a[2]=='\0') {
            flags |= REDIS_SET_XX;

        // EX seconds
        } else if ((a[0]=='e' || a[0]=='E') && 
                   (a[1]=='x' || a[1]=='X') && a[2]=='\0' && next) {
            expire = next;
            unit = UNIT_SECONDS;
            j++;

        // PX <milliseconds>
        } else if ((a[0]=='p' || a[0]=='P') && 
                   (a[1]=='x' || a[1]=='X') && a[2]=='\0' && next) {
            expire = next;
            unit = UNIT_MILLISECONDS;
            j++;

        } else {
            addReplyError(c,"");
            return;
        }
    }

    // 压缩值空间
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    // 写入
    setGenericCommand(c,flags,c->argv[1],c->argv[2],expire,unit,NULL,NULL);
}

void setnxCommand(redisClient *c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c,REDIS_SET_NX,c->argv[1],c->argv[2],NULL,0,shared.cone,shared.czero);
}

void setexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c,REDIS_SET_NO_FLAGS,c->argv[1],c->argv[3],c->argv[2],UNIT_SECONDS,NULL,NULL);
}

void psetexCommand(redisClient *c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c,REDIS_SET_NO_FLAGS,c->argv[1],c->argv[3],c->argv[2],UNIT_MILLISECONDS,NULL,NULL);
}

int getGenericCommand(redisClient *c) {
    robj *o;

    // 获取 key 的值,值不存在报错
    if((o = lookupKeyReadOrReply(c->db, c->argv[1],shared.nullbulk)) == NULL)
        return;

    // 检查类型
    if (o->type != REDIS_STRING) {
        addReply(c,shared.wrongtypeerr);
        return REDIS_ERR;

    // 返回值 
    } else {
        addReplyBulk(c,o);
        return REDIS_OK;
    }
}

void getCommand(redisClient *c) {
    getGenericCommand(c);
}

// 值不存在不新增, 只提供修改操作
void getsetCommand(redisClient *c) {
    robj *o;

    // 获取并返回 key 的值
    if (getGenericCommand(c) == REDIS_ERR) return;
    
    // 压缩新值空间
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    // 更新db中值
    setKey(c->db, c->argv[1], c->argv[2]);

    // 更新键改次数
    server.dirty++;

    // 键改通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"set",c->argv[1],c->db->id);
}

void setrangeCommand(redisClient *c) {
    robj *o;
    long offset;
    sds value = c->argv[3]->ptr;

    // 提取 offset
    if (getLongFromObjectOrReply(c, c->argv[2], &offset, NULL) != REDIS_OK)
        return;
    if (offset <= 0) {
        addReplyError(c,"offset is out of range");
        return;
    }

    // 查找 key 的值
    o = lookupKeyWrite(c->db, c->argv[1]);

    // 不存在值
    if (o == NULL) {

        if (sdslen(value) == 0) {
            addReply(c, shared.czero);
            return;
        }

        // 检查长度是否超过最大限制
        if (checkStringLength(c, offset + sdslen(value)) != REDIS_OK)
            return;

        // 创建一个空对象
        o = createObject(REDIS_STRING, sdsempty());
        dbAdd(c->db, c->argv[1], o);

    // 存在值
    } else {
        size_t olen;

        if (checkType(c, o, REDIS_STRING))
            return;

        // 原值长度获取
        olen = stringObjectLen(o);

        if (sdslen(value) == 0) {
            addReplyLongLong(c, olen);
            return;
        }
        // 检查长度是否超过最大限制
        if (checkStringLength(c, offset + sdslen(value)) != REDIS_OK) {
            return;
        }
        o = dbUnshareStringValue(c->db, c->argv[1], o);
    }

    
    // 
    if (sdslen(value) > 0) {

        // 申请 sdshdr字符串, offset 用 0 填充
        o->ptr = sdsgrowzero(o->ptr, offset+sdslen(value));
        // 写入值对象
        memcpy((char*)o->ptr+offset, value, sdslen(value));
        // 发送信号
        server.dirty++;
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"setrange",c->argv[1],c->db->id);
    }

    addReplyLongLong(c, sdslen(o->ptr));
}

void getrangeCommand(redisClient *c) {
    long start, end;
    char *str, llbuf[32];
    size_t strlen;
    robj *o;

    // 取 key 的值, 不存在报错返回
    if ((o = lookupKeyReadOrReply(c->db,c->argv[1],shared.emptybulk)) == NULL ||
        checkType(c,o,REDIS_STRING))
        return;

    // 提取 start, end 的值
    if (getLongFromObjectOrReply(c,c->argv[2],&start,NULL) != REDIS_OK ||
        getLongFromObjectOrReply(c,c->argv[3],&end, NULL) != REDIS_OK) {
        
        return;
    }

    if (o->encoding == REDIS_ENCODING_INT) {
        str = llbuf;
        strlen = ll2string(llbuf,sizeof(llbuf),(long)o->ptr);
    } else {
        str = o->ptr;
        strlen = sdslen(o->ptr);
    }

    // start,end 负索引转正索引
    if (start < 0) start += strlen;
    if (end < 0) end += strlen;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((unsigned)end >= strlen) end = strlen-1;

    // 无值
    if (start > end) {

        addReply(c,shared.emptybulk);
    // 
    } else {

        // 回复客户端值
        addReplyBulkCBuffer(c,(char*)str+start,(end-start)+1);
    }
}

void mgetCommand(redisClient *c) {
    int j;

    // 申请buf数量
    addReplyMultiBulkLen(c, c->argc-1);

    // 遍历获取
    for (j=1; j<c->argc; j++) {

        robj *o = lookupKeyRead(c->db, c->argv[j]);

        // 不存在的值
        if (o == NULL) {
            addReply(c, shared.nullbulk);

        } else {

            // 类型错误的值
            if (o->type != REDIS_STRING) {
                addReply(c, shared.wrongtypeerr);

            // 正确值
            } else {
                addReplyBulk(c, o);
            }
        }
    }
}

void msetGenericCommand(redisClient *c, int nx) {
    int busykey = 0;
    int j;

    // 偶数参数, 报错返回
    if ((c->argc % 2) == 0) {
        addReplyError(c, "wrong number of arguments for MSET");
        return ;
    }

    // nx 检查
    if (nx) {

        // 遍历key ,存在值得记录下来
        for (j=1; j<c->argc; j+=2) {
            if (lookupKeyRead(c->db, c->argv[j]) != NULL) {
                busykey++;
            }
        }

        // 有一个值存在, 返回
        if (busykey) {
            addReply(c, shared.czero);
            return;
        }
    }

    // 遍历 key
    for (j=1; j<c->argc; j+=2) {
        // 写入db
        c->argv[j+1] = tryObjectEncoding(c->argv[j+1]);
        setKey(c->db, c->argv[j], c->argv[j+1]);

        // 事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", c->argv[j], c->db->id);
    }

    // 更新键改次数
    server.dirty += (c->argc-1)/2;

    // 回复添加key数量
    addReply(c, nx ? shared.cone : shared.ok);
}

void msetCommand(redisClient *c) {
    msetGenericCommand(c, 0);
}

void msetnxCommand(redisClient *c) {
    msetGenericCommand(c, 1);
}

void incrDecrCommand(redisClient *c, long long incr) {
    robj *o,*new;;
    long long value,oldvalue;

    // 获取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);
    if (o != NULL && checkType(c, o, REDIS_STRING)) return;

    // 从值对象中提取原值
    if (getLongLongFromObjectOrReply(o, &value, NULL) != REDIS_OK)
        return;

    // 溢出检查
    oldvalue = value;
    if ((incr < 0 && oldvalue < 0 && incr < (LLONG_MIN-oldvalue)) ||
        (incr > 0 && oldvalue > 0 && incr > (LLONG_MAX-oldvalue))) 
    {
        addReplyError(c, "increment or decrement would overflow");
        return;
    } 

    // 计算新值
    value += incr;
    new = createStringObjectFromLongLong(value);
    // 新增
    if (o) {
        dbOverwrite(c->db,c->argv[1],new);
    // 编辑
    } else {
        dbAdd(c->db,c->argv[1],new);
    }

    signalModifiedKey(c->db,c->argv[1]);

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
    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK)
        return;
    incrDecrCommand(c,incr);
}

void decrbyCommand(redisClient *c) {
    long long incr;
    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK)
        return;
    incrDecrCommand(c,-incr);
}

void incrbyfloatCommand(redisClient *c) {
    long double value, incr;
    robj *o, *new, *aux;

    // 提取值对象
    o = lookupKeyWrite(c->db, c->argv[1]);
    if (o != NULL && checkType(c, o, REDIS_STRING))
        return;

    // 提取值对象的浮点数, 提取 float
    if (getLongDoubleFromObjectOrReply(c,o,&value,NULL) != REDIS_OK ||
        getLongDoubleFromObjectOrReply(c,c->argv[2],&incr,NULL) != REDIS_OK)
        return;

    // 浮点数检查
    value += incr;
    if (isnan(value) || isinf(value)) {
        addReplyError(c,"increment would produce NaN or Infinity");
        return;
    }

    // 新增/编辑
    new = createStringObjectFromLongDouble(value);
    if (o) {
        dbOverwrite(c->db,c->argv[1],new);
    } else {
        dbAdd(c->db,c->argv[1],new);
    }

    // 事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"incrbyfloat",c->argv[1],c->db->id);

    signalModifiedKey(c->db, c->argv[1]);

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

    // 获取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);
    if (o != NULL && checkType(c, o, REDIS_STRING)) return;

    // 新增
    if (o == NULL) {
        // 新值写入
        c->argv[2] = tryObjectEncoding(c->argv[2]);
        incrRefCount(c->argv[2]);
        dbAdd(c->db,c->argv[1],c->argv[2]);
        // 获取值长度
        totlen = stringObjectLen(c->argv[2]);

    // 编辑
    } else {

        // 类型检查
        if (checkType(c, o, REDIS_STRING)) return;
        // 获取原值及长度
        append = tryObjectEncoding(c->argv[2]);
        totlen = stringObjectLen(o)+sdslen(append->ptr);
        // 计算拼接后的长度不溢出
        if (checkStringLength(c,totlen) != REDIS_OK)
            return;
        dbUnshareStringValue(c->db,c->argv[1],append);
        // 追加长度
        o->ptr = sdscatlen(o->ptr, append->ptr, sdslen(append->ptr));
        // 计算拼接后长度
        totlen = sdslen(o->ptr);
    }

    // 事件通知
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING,"append",c->argv[1],c->db->id);
    // 更新键改次数
    server.dirty++;
    // 返回字符串长度
    addReplyLongLong(c,totlen);
}

void strlenCommand(redisClient *c) {
    robj *o;

    if (lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk) == NULL ||
        checkType(c,o,REDIS_STRING))
        return;

    addReplyLongLong(c,stringObjectLen(o));
}