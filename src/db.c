/**
 * redis 默认有 16个数据库
 * 
 * db.c 负责存储 key-value的关系
 * key-value 的关系存储在 dict 数据结构中
 * 
 * expire 也是按 key-ttl 存储在 dict 数据结构中
 *
 */
 

#include "redis.h"

#include <signal.h>
#include <ctype.h>

/*------------------------- 数据库相关函数(增删改查,过期时间) --------------------------*/

/**
 * 从数据库 db 中取出并返回键 key 的值对象
 * 如果 key 不存在, 返回 NULL
 */
robj *lookupKey(redisDb *db, robj *key) {

    // 从 db 查找 key 的值对象
    dictEntry *de = dictFind(db->dict,key->ptr);

    // 值对象存在
    if (de) {

        // 取出值对象, 它是个 robj
        robj *val = dictGetVal(de);

        // 更新其 LRU 时间, 防止空转时间长导致被清理
        // 在无子进程进行 rdb 或 aof 时执行, 防止破坏 COW 机制
        if (server.rdb_child_pid == -1 && server.aof_child_pid == -1)
            val->lru = LRU_CLOCK();

        // 返回值对象
        return val;

    // 值对象不存在, 返回 NULL
    } else {
        return NULL;
    }
}

/**
 * 在 db 中获取 key 的值对象
 * - 找到值对象, 更新命令信息, 返回值对象
 * - 未找到值对象, 更新未命中信息, 返回 NULL
 */
robj *lookupKeyRead(redisDb *db, robj *key) {
    robj *val;

    // 检查 key 是否过期, 符合惰性删除
    expireIfNeeded(db,key);

    // 取出 key 的值对象
    val = lookupKey(db,key);

    // 更新命中/ 不命中信息
    if (val == NULL) {
        server.stat_keyspace_misses++;
    } else {
        server.stat_keyspace_hits++;
    }

    // 返回值对象
    return val;
}

/**
 * 为执行写入操作而取出键 key 在数据库 db 中的值
 * 
 * 和 lookupKeyRead 不同, 这个函数不会更新服务器的命中/不命中信息
 * 
 * 找到时返回值对象, 没找到返回 NULL
 */
robj *lookupKeyWrite(redisDb *db, robj *key) {

    // 删除过期键
    expireIfNeeded(db,key);

    // 返回值对象
    return lookupKey(db,key);
}

/**
 * 查找 key 的值对象
 * - key 存在, 返回 key 的值对象
 * - key 不存在, 向客户端发送 reply 信息, 返回 NULL
 */
robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply) {

    robj *o = lookupKeyRead(c->db,key);

    if (!o) addReply(c,reply);

    return o;
}

/**
 * 为执行写入操作而取出键 key 在数据库 db 中的值
 * 
 * key 存在, 返回 key 的值对象
 * key 不存在, 向客户端发送 reply 信息, 并返回 NULL
 */
robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply) {
    robj *o = lookupKeyWrite(c->db,key);

    if (!o) addReply(c,reply);

    return o;
}



/**
 * 向数据库添加键值对
 * 调用者负责增加 key 和 val 的引用计数
 * 如果键存在, 程序不处理
 */
void dbAdd(redisDb *db, robj *key, robj *val) {

    // 复制键名
    sds copy = sdsdup(key->ptr);

    // 尝试添加键值对
    int retval = dictAdd(db->dict,key,val);

    // 如果键已经存在, 终止程序
    redisAssertWithInfo(NULL,key,retval == REDIS_OK);

    // 如果开启了集群模式, 那么将键保存在槽里面
    if (server.cluster_enabled) slotToKeyAdd(key);
}

/**
 * 为已存在的键关联一个新的值对象
 * 如果键不存在, 中止程序
 * 
 * 调用者负责值对象的 引用计数的增加
 * 此函数不修改键的过期时间
 */
void dbOverwrite(redisDb *db, robj *key, robj *val) {
    // 查找值对象
    dictEntry *de = dictFind(db->dict,key);

    // 值对象存在, 中止程序
    redisAssertWithInfo(NULL,key,de != NULL);

    // 修改值对象
    dictReplace(db->dict,key->ptr,val);
}

/**
 * 关联 key-value 到数据库的组合函数
 * 如果 key 已存在, 会更新其 value
 * 如果 key 不存在, 将 kv 添加到数据库
 * 
 * 同时执行以下操作
 * 1. 增加值对象的引用计数
 * 2. 监视键 key 的客户端会收到键改通知
 * 3. 移除键的过期时间（变为永久键）
 */
void setKey(redisDb *db, robj *key, robj *val) {
    // 添加或覆盖值对象
    if (lookupKeyWrite(db,key) == NULL) {
        dbAdd(db,key,val);
    } else {
        dbOverwrite(db,key,val);
    }

    // 引用计数
    incrRefCount(val);

    // 移除过期时间
    removeExpire(db,key);

    // 发送键改通知
    signalModifiedKey(db,key);
}

/**
 * 检查键 key 是否存在于数据库中, 存在返回 1, 不存在返回 0
 */
int dbExists(redisDb *db, robj *key) {
    return dictFind(db->dict,key->ptr) != NULL;
}

/**
 * 随机从数据库中取出一个键并返回
 * 
 * 如果数据库为空, 返回 NULL
 * 
 * 函数保证返回的键都是未过期的
 */
robj *dbRandomKey(redisDb *db) {
    dictEntry *de;

    while (1) {

        sds key;
        robj *keyobj;

        // 随机节点
        de = dictGetRandomKey(db->dict);

        // 没有节点啊, 只能返回了
        if (de == NULL) return NULL;

        // 取出节点的键
        key = dictGetKey(de);
        keyobj = createStringObject(key,sdslen(key));

        // 判断键是否过期
        if (dictFind(db->expires,key)) {
            // 键已过期
            if (expireIfNeeded(db,keyobj)) {
                decrRefCount(keyobj);
                continue;
            }
        }

        // 返回随机键名
        return keyobj;
    }
}

/**
 * 从数据库删除 键值对, 并删除其过期时间
 * 删除成功返回 1, 否则返回 0
 */
int dbDelete(redisDb *db, robj *key) {

    // 删除过期时间
    if (dictSize(db->expires) > 0) dictDelete(db->expires,key->ptr);

    // 删除键值对
    if (dictDelete(db->dict,key->ptr) == DICT_OK) {
        // 如果开启了集群模式, 从槽中删除给定的键
        if (server.cluster_enabled) slotToKeyDel(key);
        // 删除成功
        return 1;

    } else {
        // 删除失败
        return 0;
    }
}

/**
 * 将字符串键非 RAW 编码的值对象转换成 RAW 编码后存入数据库
 */
robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o) {
    redisAssert(o->type == REDIS_STRING);

    if (o->refcount != 1 || o->encoding != REDIS_ENCODING_RAW) {
        robj *decoded = getDecodedObject(o);
        o = createRawStringObject(decoded->ptr,sdslen(decoded->ptr));
        decrRefCount(decoded);
        dbOverwrite(db,key,o);
    }

    return o;
}

/**
 * 清空服务器的所有数据库数据
 * 返回删除的节点数量
 */
long long emptyDb(void(callback)(void*)) {
    int j;
    long long removed = 0;

    // 遍历清空数据, 统计删除节点数
    for (j = 0; j < server.dbnum; j++) {

        // 统计删除节点数
        removed += dictSize(server.db[j].dict);

        // 删除 kv 数据库
        dictEmpty(server.db[j].dict,callback);

        // 删除 expire 数据库
        dictEmpty(server.db[j].expires,callback);
    }

    // 如果开启集群模式, 那么移除槽记录
    if (server.cluster_enabled) slotToKeyFlush();

    return removed;
}

/**
 * 将客户端的目标数据库切换为 id 所指定的数据库
 * 切换成功, 返回 REDIS_OK
 * 切换失败, 返回 REDIS_ERR
 */
int selectDb(redisClient *c, int id) {

    // 确保 id 在正确范围内
    if (id < 0 || id >= server.dbnum)
        return REDIS_ERR;

    // 切换数据库
    c->db = &server.db[id];

    return REDIS_OK;
}

/**
 * 键空间改动的钩子
 * 
 * 当数据库的键改动时, 调用 signalModifiedKey()
 * 
 * 当数据库被清空时, 调用 signalFlushDb()
 */
void signalModifiedKey(redisDb *db, robj *key) {
    touchWatchedKey(db,key);
}

void signalFlushDb(int dbid) {
    touchWatchedKeysOnFlush(dbid);
}


/*------------------------- 数据库命令(与类型无关) --------------------------*/

/**
 * 清空客户端指定的数据库
 */
void flushdbCommand(redisClient *c) {
    // 更新键改次数
    server.dirty += dictSize(c->db->dict);

    // 发送数据库清空通知
    signalFlushDb(c->db->id);

    // 清空指定数据库
    dictEmpty(c->db->dict,NULL);
    dictEmpty(c->db->expires,NULL);

    // 如果开启了集群模式, 要移除槽记录
    if (server.cluster_enabled) slotToKeyFlush();

    // 回复客户端
    addReply(c,shared.ok);
}

/**
 * 清空服务器中的所有数据库
 */
void flushallCommand(redisClient *c) {
    // 发送通知
    signalFlushDb(-1);

    // 清空所有数据库, 更新键改次数
    // 回复客户端
    server.dirty += emptyDb(NULL);
    addReply(c,shared.ok);

    // 如果正在保存新的 RDB, 那么取消保存操作
    if (server.rdb_child_pid != -1) {
        kill(server.rdb_child_pid,SIGUSR1);
        rdbRemoveTempFile(server.rdb_child_pid);
    }

    // 更新 RDB 文件
    if (server.saveparamslen > 0) {
        // rdbSave() 会清空服务器的 dirty 属性
        // 但为了确保 FLUSHALL 命令会被正常传播
        // 程序需要保存并在 rdbSave() 调用之后还原服务器的 dirty 属性
        int saved_dirty = server.dirty;

        rdbSave(server.rdb_filename);

        server.dirty = saved_dirty;
    }

    server.dirty++;
}

// DEL key [key ...]
void delCommand(redisClient *c) {
    int deleted = 0, j;

    // 遍历所有输入键
    for (j = 1; j < c->argc; j++) {

        // 删除过期键
        expireIfNeeded(c->db,c->argv[j]);

        // 尝试删除键
        if (dbDelete(c->db,c->argv[j])) {

            // 删除成功, 发送通知
            signalModifiedKey(c->db,c->argv[j]);
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,
                "del",c->argv[j],c->db->id);

            server.dirty++;

            // 增加 deleted 计数器的值
            deleted++;
        }
    }

    // 返回删除键的数量
    addReplyLongLong(c,deleted);
}

// EXISTS key [key ...]
void existsCommand(redisClient *c) {

    // 检查键是否已经过期, 如果过期就将它删除
    // 这样可避免已过期的键被误认为存在
    expireIfNeeded(c->db,c->argv[1]);

    // 在数据库中查找 key
    if (dbExists(c->db,c->argv[1])) {
        addReply(c,shared.cone);
    } else {
        addReply(c,shared.czero);
    }
}

// SELECT index
void selectCommand(redisClient *c) {
    long id;

    // 不合法的数据库号码
    if (getLongFromObjectOrReply(c,c->argv[1],&id,
            "invalid DB index") != REDIS_OK)
            return;

    // 集群不可以使用
    if (server.cluster_enabled && id != 0) {
        addReplyError(c,"SELECT is not allowed in cluster mode");
        return;
    }

    // 切换数据库
    if (selectDb(c,id) == REDIS_ERR) {
        addReplyError(c,"invalid DB index");
    } else {
        addReply(c,shared.ok);
    }
}

// RANDOMKEY
void randomkeyCommand(redisClient *c) {
    robj *key;

    // 随机返回键
    if ((key = dbRandomKey(c->db)) == NULL) {
        addReply(c,shared.nullbulk);
        return;
    }

    addReplyBulk(c,key);
    decrRefCount(key);
}

// KEYS pattern
void keysCommand(redisClient *c) {
    dictIterator *di;
    dictEntry *de;

    // 取出模式字符串
    sds pattern = c->argv[1]->ptr;
    int plen = sdslen(pattern), allkeys;
    unsigned long numkeys = 0;

    // 申请回复客户端的 buffer
    void *replylen = addDeferredMultiBulkLength(c);

    allkeys = (pattern[0] == '*' && pattern[1] == '\0');
    
    // 遍历数据库, 返回名字和模式匹配的键
    di = dictGetSafeIterator(c->db->dict);
    while ((de = dictNext(di)) != NULL) {
        sds key = dictGetKey(de);
        robj *keyobj;

        if (allkeys || stringmatchlen(pattern,plen,key,sdslen(key),0)) {
            // 匹配成功, 向客户端发送键
            keyobj = createStringObject(key,sdslen(key));

            // 未过期的键才能返回客户端
            if (expireIfNeeded(c->db,keyobj) == 0) {
                addReply(c,keyobj);
                numkeys++;
            }

            decrRefCount(keyobj);
        }
    }

    // 释放迭代器
    dictReleaseIterator(di);

    // 回复客户端
    setDeferredMultiBulkLength(c,replylen,numkeys);
}

int parseScanCursorOrReply(redisClient *c, robj *o, unsigned long *cursor) {

    return REDIS_OK;
}

void scanGenericCommand(redisClient *c, robj *o, unsigned long cursor) {
    
}

// 当前数据库节点数量
void dbsizeCommand(redisClient *c) {
    addReplyLongLong(c,dictSize(c->db->dict));
}

// 返回最后一次同步磁盘的时间戳
void lastsaveCommand(redisClient *c) {
    addReplyLongLong(c,server.lastsave);
}

// 获取 key 的存储类型
void typeCommand(redisClient *c) {
    robj *o;
    char *type;

    o = lookupKeyRead(c->db,c->argv[1]);

    if (o == NULL) {
        type = "none";
    } else {
        switch(o->type){
        case REDIS_STRING: type = "string"; break;
        case REDIS_LIST: type = "list"; break;
        case REDIS_SET: type = "set"; break;
        case REDIS_ZSET: type = "zset"; break;
        case REDIS_HASH: type = "hash"; break;
        default: type = "unknown"; break;
        }
    }

    addReplyStatus(c,type);
}


/*------------------------- Expires API --------------------------*/

/**
 * 移除键的过期时间
 * 删除成功, 返回 1
 * 删除失败, 返回 0
 */
int removeExpire(redisDb *db, robj *key) {

    // 确保键带有过期时间
    redisAssertWithInfo(NULL,key,dictFind(db->dict,key->ptr) != NULL);

    // 删除过期时间
    return dictDelete(db->expires,key->ptr) == DICT_OK;
}

/**
 * 将键 key 的过期时间设为 when
 */
void setExpire(redisDb *db, robj *key, long long when) {
    dictEntry *kde, *de;
    // 取出值对象
    kde = dictFind(db->dict,key->ptr);

    // 不存在值对象, 中止程序
    redisAssertWithInfo(NULL,key,kde != NULL);

    // 查找或新增键 key 到 expire
    de = dictReplaceRaw(db->expires,dictGetKey(kde));

    // 将过期时间添加到 expire
    dictSetSignedIntegerVal(de,when);
}

/**
 * 返回给定 key 的过期时间
 * 如果键没有过期时间, 返回 -1
 */
long long getExpire(redisDb *db, robj *key) {
    dictEntry *de;

    // 键不存在过期时间, 直接返回
    if (dictSize(db->expires) == 0 ||
        (de = dictFind(db->expires,key->ptr)) == NULL) return -1;

    // 数据库存在指定 key, 中止程序
    redisAssertWithInfo(NULL,key,dictFind(db->dict,key->ptr) != NULL);

    // 返回过期时间
    dictGetSignedIntegerVal(de);
}

/**
 * 将过期时间传播到附属节点和 AOF 文件
 * 
 * 当一个键在主节点中过期时,
 * 主节点会向所有附属节点和 AOF 文件传播一个显式的 DEL 命令
 * 
 * 这种做法可以保证主副节点之间的操作是顺序执行的,
 * 所以即使有写操作对过期键执行, 所有数据都还是一致的
 */
void propagateExpire(redisDb *db, robj *key) {

}

/**
 * 检查 key 是否过期, 如果过期, 将它从数据库中删除
 * 返回 0, 表示键不存在过期时间 或 未过期
 * 返回 1, 表示键已过期并删除
 */
int expireIfNeeded(redisDb *db, robj *key) {

    // 取出键的过期时间
    mstime_t when = getExpire(db,key);
    mstime_t now;

    // 没有过期时间
    if (when < 0) return 0;

    // 服务器正在进行载入, 不进行过期检查
    if (server.loading) return 0;

    // lua script
    now = server.lua_caller ? server.lua_time_start : mstime();

    // 当服务器运行在 replication 模式时
    // 附属节点不主动删除 key, 它只返回逻辑上正确的返回值
    // 真正的删除操作要等待主节点发来删除命令时才执行
    // 从而保证数据的同步
    if (server.masterhost != NULL) return now > when;

    // 未过期, 返回 0
    if (now <= when) return 0;

    // 更新过期键的数量
    server.stat_expiredkeys++;

    // 向 AOF 文件和附属节点传播过期信息
    propagateExpire(db,key);

    // 发送事件通知
    notifyKeyspaceEvent(REDIS_NOTIFY_EXPIRED,
        "expired",key,db->id);

    // 从数据库删除过期键
    return dbDelete(db,key);
}

/*------------------------- Expires Commands --------------------------*/

/**
 * 处理各种过期时间
 * basetime 在 *AT命令下给 0, 其他情况给 UINX 时间戳, 总是毫秒
 * unit 为时间单位 SECONDS or MILLISECONDS
 */
void expireGenericCommand(redisClient *c, long long basetime, int unit) {
    robj *key = c->argv[1], *param = c->argv[2];
    long long when;

    // 取出时间
    if (getLongLongFromObjectOrReply(c,param,&when,NULL) != REDIS_OK)
        return;

    // 程序已毫秒形式存储时间戳, 
    // 如果设置为秒, 将其转为毫秒
    if (unit == UNIT_SECONDS) when *= 1000;

    // 计算时间戳
    when += basetime;

    // 取出键
    if (lookupKeyRead(c->db,key) == NULL) {
        addReply(c,shared.czero);
        return;
    }

    if (when <= mstime() && !server.loading && !server.masterhost) {

        // 时间已过期, 服务器为主节点, 并且未加载数据
        robj *aux;

        redisAssertWithInfo(c,key,dbDelete(c->db,key));
        server.dirty++;

        // 传播 DEL 命令
        aux = createStringObject("DEL",3);

        rewriteClientCommandVector(c,2,aux,key);
        decrRefCount(aux);

        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",key,c->db->id);

        addReply(c,shared.cone);

        return;

    } else {
        // 存储过期时间
        setExpire(c->db,key,when);

        // 回复客户端
        addReply(c,shared.cone);

        // 发送通知
        signalModifiedKey(c->db,key);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"expire",key,c->db->id);

        server.dirty++;

        return;
    }


}

// EXPIRE key seconds
void expireCommand(redisClient *c) {
    expireGenericCommand(c,mstime(),UNIT_SECONDS);
}

// EXPIREAT key timestamp
void expireatCommand(redisClient *c) {
    expireGenericCommand(c,0,UNIT_SECONDS);
}

// PEXPIRE key milliseconds
void pexpireCommand(redisClient *c) {
    expireGenericCommand(c,mstime(),UNIT_MILLISECONDS);
}

// PEXPIREAT key milliseconds-timestamp
void pexpireatCommand(redisClient *c) {
    expireGenericCommand(c,0,UNIT_MILLISECONDS);
}