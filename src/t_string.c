/**
 * 
 * 1. 整体结构
 *     string 命令 -> t_string.c <=> db
 *                       |
 *                       |=> network
 * 
 *   1. 用户输入字符串的命令 (每个字段都是 RedisObject 结构),
 *   2. t_string.c 根据各命令的规则, 从 db 获取 key 的 val, 然后处理修改 val
 *      (db 中的 val 是以 RedisObject 结构存储的)
 *   3. 最后通过 networking.c 将处理后的结果发送给客户端
 * 
 * 2. 命令以空格拆分, 存放在 c->argv 数组中
 *      例如：SET key1 "Hello"
 *          - argv[0] SET
 *          - argv[1] key1
 *          - argb[2] Hello
 * 
 * 3. 功能模块
 *    - t_string.c, 处理字符串处理的逻辑
 *    - networking.c, 通信模块, 负责给客户端发送数据
 *    - db.c, 将 key 与 val 关联起来, 负责读取 val
 *    - object.c, 负责Redis对象的存取
 */

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

    // 将数据库设为脏
    server.dirty++;

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

/**
 * 获取值对象的标准通用方法
 * - 获取成功, 向客户端发送值对象, 返回 REDIS_OK
 * - 获取失败, 返回 REDIS_ERR
 */
int getGenericCommand(redisClient *c) {
    robj *o;

    // 尝试获取值对象, 获取失败向客户端发送nil
    if ((o = lookupKeyReadOrReply(c, c->argv[1],shared.nullbulk)) == NULL)
        return REDIS_OK;

    // 值对象的类型是否为字符串
    if (o->type != REDIS_STRING) {
        addReply(c, shared.wrongtypeerr);
        return REDIS_ERR;
    } else {
        // 向客户端发送值对象
        addReplyBulk(c, o);
        return REDIS_OK;
    }
}

void getCommand(redisClient *c) {
    getGenericCommand(c);
}

/**
 * GETSET mycounter "0"
 * 
 * 取出旧的值对象并发送给客户端
 * 将新值对象写入
 */
void getsetCommand(redisClient *c) {

    // 获取旧的值对象, 并发送给客户端
    if (getGenericCommand(c) == REDIS_ERR) return;

    // 压缩新的值对象
    c->argv[2] = tryObjectEncoding(c->argv[2]);

    // 设置新的值对象
    setKey(c->db, c->argv[1], c->argv[2]);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", c->argv[1], c->db->id);

    // 将服务器设为脏
    server.dirty++;
}

/**
 * SETRANGE key offset value
 */
void setrangeCommand(redisClient *c) {
    long offset;
    robj *o;

    sds value = c->argv[3]->ptr;

    // 获取 offset 值
    if (getLongFromObjectOrReply(c, c->argv[2], &offset, NULL) != REDIS_OK)
        return;

    // 校验 offset 值
    if (offset < 0) {
        addReplyError(c, "offset is out of range");
        return;
    }

    // 获取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);
    
    // 值对象不存在
    if (o == NULL) {
        
        // value 为空, 无可设置内容, 向客户端返回 0
        if (sdslen(value) == 0) {
            addReply(c, shared.czero);
            return;
        }

        // 超过字符串最大长度限制, 向客户端发送错误回复
        if (checkStringLength(c, offset+sdslen(value)) != REDIS_OK)
            return;

        // 创建一个空字符串的值对象
        o = createObject(REDIS_STRING, sdsempty());
        // 在数据库中将键值关联
        dbAdd(c->db, c->argv[1], o);

    // 值对象存在
    } else {
        size_t olen;

        // 验证值对象是字符串类型
        if (checkType(c, o, REDIS_STRING))
            return ;

        // value 为空, 无可设置内容, 向客户端返回 0
        olen = stringObjectLen(o);
        if (sdslen(value) == 0) {
            addReplyLongLong(c, olen);
            return;
        }

        // 超过字符串最大长度限制, 向客户端发送错误回复
        if (checkStringLength(c, offset+sdslen(value)) != REDIS_OK)
            return;

        // 更新数据库的值对象
        o = dbUnshareStringValue(c->db, c->argv[1], o);
    }

    // 判断可删, 前面做过 value > 0 的判断了
    if (sdslen(value) > 0) {

        // 用 0 填充字符串到指定长度
        o->ptr = sdsgrowzero(o->ptr,offset+sdslen(value));

        // 将 value 复制到字符串的 offset 位置
        memcpy((char*)o->ptr+offset, value, sdslen(value));

        // 向数据库发送键被修改的信号
        signalModifiedKey(c->db, c->argv[1]);

        // 发送事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "setrange", c->argv[1], c->db->id);

        // 将服务器设为脏
        server.dirty++;
    }

    // 设置成功, 向客户端发送新的字符串长度
    addReplyLongLong(c, sdslen(o->ptr));
}

void getrangeCommand(redisClient *c) {
    long start, end;
    robj *o;
    char *str, llbuf[32];
    size_t strlen;

    // 取出 start, end
    if (getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != REDIS_OK)
        return;
    if (getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != REDIS_OK)
        return;

    // key 的值对象不存在或类型错误, 回复客户端错误
    if ((o =lookupKeyReadOrReply(c, c->argv[1], shared.emptybulk)) == NULL || 
         checkType(c, o, REDIS_STRING)) return;


    // 读取字符串, 字符串长度
    // 整数编码, 将整数转为字符串
    if (o->encoding == REDIS_ENCODING_INT) {
        str = llbuf;
        strlen = ll2string(llbuf,sizeof(llbuf),(long)o->ptr);
    } else {
        str = o->ptr;
        strlen = sdslen(o->ptr);
    }

    // 负数索引转换成正数索引
    // 如果 start, end 超范围, 设置成各自的最大值
    if (start < 0) start = strlen+start;
    if (end < 0) end = strlen+end;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((unsigned)end >= strlen) end = strlen-1;

    // 回复客户端截取的字符串
    if (start > end) {
        // 无交集, 返回空字符串
        addReply(c, shared.emptybulk);
    } else {
        // 返回截取的字符串
        addReplyBulkCBuffer(c, (char*)str+strlen, end-start+1);
    }
}

/**
 * 获取多个 key 的值对象
 */
void mgetCommand(redisClient *c) {

    int j;

    // 要返回的值对象数量
    addReplyMultiBulkLen(c, c->argc-1);

    // 添加值对象
    for (j=1; j<c->argc; j++) {
        // 获取 key 的值对象
        robj *o = lookupKeyRead(c->db, c->argv[j]);
        // 值对象不存在, 向客户端发送空回复
        if (o == NULL) {
            addReply(c, shared.nullbulk);

        // 值对象存在
        } else {

            // 值对象不是字符串对象, 向客户端发送"类型错误"的回复
            if (o->type != REDIS_STRING) {
                addReply(c, shared.nullbulk);

            // 向客户端发送值对象
            } else {
                addReplyBulk(c, o);
            }
        }
    }
}

/**
 * 批量添加字符串的通用统一函数
 * nx 为 1, 是批量 setnx 操作
 */
void msetGenericCommand(redisClient *c, int nx) {
    int j, busykeys = 0;

    // 如果参数数量能被 2 整除, 说明参数缺失
    if ((c->argc % 2) == 0) {
        addReplyError(c, "wrong number of arguments for MSET");
        return;
    }

    // 批量 setnx 操作, 只要有一个 key 存在, 不继续执行
    for (j = 1; j < c->argc; j += 2) {

        // 所有 key 查找是否存在值对象
        // 记录存在的值对象数量
        if (lookupKeyWrite(c->db, c->argv[j]) != NULL) {
            busykeys++;
        }

        // 存在值对象, 发送空白回复, 并放弃执行
        if (busykeys) {
            addReply(c, shared.czero);
            return;
        }
    }


    // 设置键值对
    for (j=1; j<c->argc; j+=2) {

        // 压缩值的空间
        c->argv[j+1] = tryObjectEncoding(c->argv[j+1]);

        // 写入值
        // c->argv[j] 为键, c->argv[j+1] 为值
        setKey(c->db, c->argv[j], c->argv[j+1]);

        // 发送事件通知
        notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "set", c->argv[j], c->db->id);
    }

    // 服务器设为脏, 更新变更 key 数量
    server.dirty += (c->argc-1)/2;

    // 设置成功
    // MSET 返回 OK, 而 MSETNX 返回 1
    addReply(c, nx ? shared.cone : shared.ok);
}

void msetCommand(redisClient *c) {
    return msetGenericCommand(c, 0);
}

void msetnxCommand(redisClient *c) {
    return msetGenericCommand(c, 1);
}

/**
 * 增加减少值的通用方法
 */
void incrDecrCommand(redisClient *c, long long incr) {
    robj *o, *new;
    long long value, oldvalue;

    // 获取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);

    // 检查值对象的类型是否为 字符串
    if (o != NULL && checkType(c, o, REDIS_STRING)) return;

    // 取出值对象的数值, 保存到 value 中
    if (getLongLongFromObjectOrReply(c, o, &value, NULL) != REDIS_OK) return;

    oldvalue = value;
    // 检查加减操作是否会溢出
    // 溢出向客户端回复错误, 并终止操作
    if ((incr < 0 && oldvalue < 0 && incr < (LLONG_MIN-oldvalue)) ||
         incr > 0 && oldvalue > 0 && incr > (LLONG_MAX-oldvalue)) {
        addReplyError(c, "increment or decrement would overflow");
        return;
    }

    // 更新数值
    value += incr;
    // 创建一个新的值对象
    new = createStringObjectFromLongLong(value);
    // 添加或覆盖 key 的值对象
    if (o) {
        dbOverwrite(c->db, c->argv[1], new);
    } else {
        dbAdd(c->db, c->argv[1], new);
    }

    // 向数据库发送键被修改的信号
    signalModifiedKey(c->db, c->argv[1]);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "incrby", c->argv[1], c->db->id);

    // 将服务器设为脏
    server.dirty++;

    // 回复客户端
    addReply(c, shared.colon);
    addReply(c, new);
    addReply(c, shared.crlf);
}

void incrCommand(redisClient *c) {
    incrDecrCommand(c, 1);
}

void decrCommand(redisClient *c) {
    incrDecrCommand(c, -1);
}

void incrbyCommand(redisClient *c) {
    long long incr;

    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK) return;
    incrDecrCommand(c, incr);
}

void decrbyCommand(redisClient *c) {
    long long incr;

    if (getLongLongFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK) return;
    incrDecrCommand(c, -incr);
}

/**
 * 浮点类型值增加
 */
void incrbyfloatCommand(redisClient *c) {
    long double value, incr;
    robj *o, *new, *aux;

    // 获取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);

    // 判断值对象是否为字符串类型
    if (o != NULL && !checkType(c, o, REDIS_STRING)) return;

    // 提取值对象的浮点值, 存入 value 中
    if (getLongDoubleFromObjectOrReply(c, o, &value, NULL) != REDIS_OK ||
        getLongDoubleFromObjectOrReply(c, c->argv[2], &incr, NULL) != REDIS_OK)
        return;

    // 添加增量值
    value += incr;
    // incr 是数值同时未溢出
    if (isnan(value) || isinf(value)) {
        addReplyError(c, "increment would produce NaN or Infinity");
        return;
    }

    // 写入或覆盖值对象的值
    new = createStringObjectFromLongDouble(value);
    if (o) {
        dbOverwrite(c->db, c->argv[1], new);
    } else {
        dbAdd(c->db, c->argv[1], new);
    }

    // 通知键修改
    signalModifiedKey(c->db, c->argv[1]);

    // 发出事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "incrbyfloat", c->argv[1], c->db->id);

    // 服务器设为脏
    server.dirty++;

    // 回复客户端
    addReplyBulk(c, new);

    // todo 暂时不知道干啥的
    // 在传播 INCRBYFLOAT 命令时, 总是用 SET 命令来替换 INCRBYFLOAT 命令
    // 从而防止因为不同的浮点精度和格式化造成 AOF 重启时的数据不一致
    aux = createStringObject("SET", 3);
    rewriteClientCommandArgument(c, 0, aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c, 2, aux);
}

void appendCommand(redisClient *c) {
    robj *o, *append;
    size_t totlen;

    // 获取 key 的值对象
    o = lookupKeyWrite(c->db, c->argv[1]);

    // 值对象不存在
    if (o == NULL) {

        // 压缩值的空闲内存
        c->argv[2] = tryObjectEncoding(c->argv[2]);
        // 将值对象添加到数据库
        dbAdd(c->db, c->argv[1], c->argv[2]);
        // 更新值对象的引用计数
        incrRefCount(c->argv[2]);
        // 记录值的长度
        totlen = stringObjectLen(c->argv[2]);

    // 值对象存在
    } else {

        // 检查值对象的类型是否为字符串
        if (checkType(c, o, REDIS_STRING)) return;

        append = c->argv[2];
        // 计算追加字符串后的长度
        totlen = stringObjectLen(o) + sdslen(append->ptr);
        // 新字符串长度是否超过最大限制
        if (checkStringLength(c, totlen) != REDIS_OK) 
            return;

        // 执行数据库层的追加
        o = dbUnshareStringValue(c->db, c->argv[1], o);

        // 值对象的内容 sds 追加
        sdscatlen(o->ptr, append->ptr, sdslen(append->ptr));

        // 计算新字符串长度
        totlen = sdslen(o->ptr);
    }

    // 向数据库发送键修改的信号
    signalModifiedKey(c->db, c->argv[1]);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_STRING, "append", c->argv[1], c->db->id);

    // 将服务器设置为脏
    server.dirty++;

    // 回复客户端, 追加字符串后的长度
    addReplyLongLong(c, totlen);
}

// 返回值的长度
void strlenCommand(redisClient *c) {
    robj *o;

    // 获取 key 的值对象
    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.czero)) == NULL ||
        checkType(c, o, REDIS_STRING)) return;

    // 将字符串的长度, 返回给客户端
    addReplyLongLong(c, stringObjectLen(o));
}