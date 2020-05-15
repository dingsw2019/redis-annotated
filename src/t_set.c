/**
 * 
 * 集合的内部编码结构
 * 
 * - REDIS_ENCODING_HT
 *      * 存储的是 robj 包着的值
 *      * 节点存在 hashtable 的 key 中, value 不存内容
 * -----------------------------------------------------
 * |java|     |100 |     |python|     |redis|     |....|
 * -----------------------------------------------------
 * |key |value|key |value|key   |value|key  |value|    |
 * -----------------------------------------------------
 * 
 * - REDIS_ENCODING_INTSET
 *      * 存储的是整数, 所以每次要从 robj 中提取整数才能存入 intset 结构
 * ------------------------------------------------
 * |  100  |  20  |  3  |  50  | 10000 |  4  |....|
 * ------------------------------------------------
 * 
 * 
 * 
 */

#include "redis.h"

void sunionDiffGenericCommand(redisClient *c, robj **setkeys, int setnum, robj *dstkey, int op);

/*----------------------------- 迭代器 ------------------------------*/

/**
 * 创建并返回迭代器
 */
setTypeIterator *setTypeInitIterator(robj *subject) {

    // 申请内存空间
    setTypeIterator *si = zmalloc(sizeof(setTypeIterator));

    // 初始化属性
    si->subject = subject;
    si->encoding = subject->encoding;

    if (si->encoding == REDIS_ENCODING_HT) {
        si->di = dictGetIterator(subject->ptr);

    } else if (si->encoding == REDIS_ENCODING_INTSET) {
        si->ii = 0;

    } else {
        redisPanic("Unknown set encoding");
    }
}

/**
 * 释放迭代器
 */
void setTypeReleaseIterator(setTypeIterator *si) {
    // 释放字典迭代器
    if (si->encoding == REDIS_ENCODING_HT) {
        dictReleaseIterator(si->di);
    }

    // 释放迭代器
    zfree(si);
}

/**
 * 获取当前节点值, 并迭代到下一个节点
 * 获取成功返回 当前编码至, 迭代完成无节点返回 -1
 * 编码为 REDIS_ENCODING_HT, 节点值写入 objele
 * 编码为 REDIS_ENCODING_INTSET, 节点值写入 llele
 */
int setTypeNext(setTypeIterator *si, robj **objele, int64_t *llele) {
    
    if (si->encoding == REDIS_ENCODING_HT) {
        
        dictEntry *de = dictNext(si->di);

        if (de == NULL) return -1;

        *objele = dictGetKey(de);

    } else if (si->encoding == REDIS_ENCODING_INTSET) {

        if (!intsetGet(si->subject->ptr,si->ii++,llele))
            return -1;

    } else {
        redisPanic("Unknown set encoding");
    }
}

/**
 * 从迭代器获取当前节点值, 以 robj 格式返回
 * 调用者再使用完 robj 对象后, 自行调用 decrRefCount
 */
robj *setTypeNextObject(setTypeIterator *si) {
    robj *objele;
    int64_t llele;
    int encoding;
    
    encoding = setTypeNext(si,&objele,&llele);

    switch(encoding) {
    // 迭代完成, 没有节点了
    case -1:    return NULL;
    case REDIS_ENCODING_INTSET:
        return createStringObjectFromLongLong(llele);
    case REDIS_ENCODING_HT:
        incrRefCount(objele);
        return objele;
    default:
        redisPanic("Unsupported encoding");
    }

    return NULL;
}

/*----------------------------- 转码转换 ------------------------------*/

/**
 * 将集合对象 setobj 的编码转换为 REDIS_ENCODING_HT
 * 新创建的结果字典会被预先分配为和原来的集合一样大
 */
void setTypeConvert(robj *setobj, int enc) {
    
    setTypeIterator *si;

    // 确认类型和编码正确
    redisAssertWithInfo(NULL, setobj, setobj->type == REDIS_SET &&
                            setobj->encoding == REDIS_ENCODING_INTSET);

    // 迁移节点
    if (enc == REDIS_ENCODING_HT) {
        int64_t intele;
        robj *element;
        dict *d;

        // 创建空字典
        d = dictCreate(&setDictType,NULL);

        // 预分配字典内存
        dictExpand(d,intsetLen(setobj->ptr));

        // 生成迭代器
        si = setTypeInitIterator(setobj);

        // 迭代集合, 迁移
        while (setTypeNext(si,NULL,&intele) != -1) {

            element = createStringObjectFromLongLong(intele);
            redisAssertWithInfo(NULL,element,dictAdd(d,element,NULL) == DICT_OK);
        }

        // 释放迭代器
        setTypeReleaseIterator(si);

        // 释放原集合结构
        zfree(setobj->ptr);

        // 变更编码
        setobj->encoding = REDIS_ENCODING_HT;

        // 绑定新集合结构
        setobj->ptr = d;

    } else {
        redisPanic("Unsupported set conversion");
    }
}



/*----------------------------- 基础函数 -------------------------------*/

/**
 * 创建并返回一个空集合
 * 当 value 是整数或者可以转换为整数时, 返回 intset
 * 否则, 返回 hashtable 编码的集合
 */
robj *setTypeCreate(robj *value) {

    if (isObjectRepresentableAsLongLong(value,NULL) == REDIS_OK)
        return createIntsetObject();
    
    return createSetObject();
}

/**
 * 多态操作, 添加节点到集合
 * 添加成功返回 1, 节点已存在返回 0
 */
int setTypeAdd(robj *subject, robj *value) {
    long long llval;

    // hashtable
    if (subject->encoding == REDIS_ENCODING_HT) {
        if (dictAdd(subject->ptr,value,NULL) == DICT_OK) {
            incrRefCount(value);
            return 1;
        }

    // intset
    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        

        // value 是整数, 添加到 intset
        if (isObjectRepresentableAsLongLong(value, &llval) == REDIS_OK) {
            uint8_t success = 0;
            // 添加新节点
            subject->ptr = intsetAdd(subject->ptr,llval,&success);
            if (success) {

                // 检查节点数量是否超过转码限制
                if (intsetLen(subject->ptr) >= server.set_max_intset_entries)
                    setTypeConvert(subject,REDIS_ENCODING_HT);

                return 1;
            }


        // value 不是整数
        } else {

            // 转换编码
            setTypeConvert(subject,REDIS_ENCODING_HT);

            // 添加新节点
            redisAssertWithInfo(NULL,value,dictAdd(subject->ptr,value,NULL) == DICT_OK);
            incrRefCount(value);
            return 1;
        }

    } else {
        redisPanic("Unknown set encoding");
    }

    return 0;
}

/**
 * 多态操作, 删除节点
 * 删除成功返回 1, 否则返回 0
 */
int setTypeRemove(robj *setobj, robj *value) {
    long long llval;
    
    if (setobj->encoding == REDIS_ENCODING_HT) {
        if (dictDelete(setobj->ptr,value) == DICT_OK) {
            if (htNeedsResize(setobj->ptr)) dictResize(setobj->ptr);
            return 1;
        }

    } else if (setobj->encoding == REDIS_ENCODING_INTSET) {
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK) {
            int success;
            setobj->ptr = intsetRemove(setobj->ptr,llval,&success);
            if (success) return 1;
        }

    } else {

        redisPanic("Unknown set encoding");
    }

    return 0;
}

/**
 * 多态操作, 检查节点是否存在于集合中
 * 存在返回 1, 否则返回 0
 */
int setTypeIsMember(robj *subject, robj *value) {
    long long llval;
    
    if (subject->encoding == REDIS_ENCODING_HT) {
        return dictFind((dict*)subject->ptr,value) != NULL;

    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK)
            return intsetFind((intset*)subject->ptr,llval);

    } else {

        redisPanic("Unknown set encoding");
    }

    return 0;
}

/**
 * 多态操作, 随机获取一个节点值
 * 获取成功返回集合的编码
 * 编码为 REDIS_ENCODING_HT, 节点值写入 objele
 * 编码为 REDIS_ENCODING_INTSET, 节点值写入 llele
 */
int setTypeRandomElement(robj *setobj, robj **objele, int64_t *llele) {

    if (setobj->encoding == REDIS_ENCODING_HT) {
        dictEntry *de = dictGetRandomKey(setobj->ptr);
        *objele = dictGetKey(de);

    } else if (setobj->encoding == REDIS_ENCODING_INTSET) {
        llele = intsetRandom(setobj->ptr);

    } else {

        redisPanic("Unknown set encoding");
    }

    return setobj->encoding;
}

/**
 * 多态操作, 获取节点数量
 */
unsigned long setTypeSize(robj *subject) {

    if (subject->encoding == REDIS_ENCODING_HT) {
        return dictSize((dict*)subject->ptr);

    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        return intsetLen((intset*)subject->ptr);

    } else {

        redisPanic("Unknown set encoding");
    }
}

/*----------------------------- 命令 ------------------------------*/

// SADD key member [member ...]
void saddCommand(redisClient *c) {
    int j, added = 0;
    robj *set;

    // 取出集合对象
    set = lookupKeyWrite(c->db,c->argv[1]);

    // 如果不存在, 就创建一个空的集合对象
    if (set == NULL) {
        set = setTypeCreate(c->argv[2]);
        dbAdd(c->db,c->argv[1],set);

    // 存在, 检查类型是否对
    } else {
        if (set->type != REDIS_SET) {
            addReply(c,shared.wrongtypeerr);
            return;
        }
    }

    // 添加元素
    for (j = 2; j < c->argc; j++) {
        c->argv[j] = tryObjectEncoding(c->argv[j]);
        
        if (setTypeAdd(set,c->argv[j]))
            added++;
    }

    // 发送通知
    if (added) {
        signalModifiedKey(c->db,c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sadd",c->argv[1],c->db->id);
    }

    server.dirty += added;

    addReplyLongLong(c,added);
}

// SREM key member [member ...]
void sremCommand(redisClient *c) {
    int j, deleted = 0, keyremoved = 0;
    robj *set;

    // 取出集合对象
    if ((set = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // 遍历删除节点
    for (j = 2; j < c->argc; j++) {

        c->argv[j] = tryObjectEncoding(c->argv[j]);

        // 删除成功
        if (setTypeRemove(set,c->argv[j])) {
            deleted++;
            // 如果集合空了
            if (setTypeSize(set) == 0) {
                dbDelete(c->db,c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }

    // 发送通知
    if (deleted) {
        signalModifiedKey(c->db, c->argv[1]);

        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"srem",c->argv[1],c->db->id);

        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
        
        server.dirty += deleted;
    }

    // 回复客户端
    addReplyLongLong(c,deleted);
}

// SMOVE source destination member
// 将元素值从 source 移动到 dest
void smoveCommand(redisClient *c) {
    robj *srcset, *dstset, *ele;

    // 取出 source 的集合对象
    srcset = lookupKeyWrite(c->db,c->argv[1]);

    // 取出 dst 的集合对象
    dstset = lookupKeyWrite(c->db,c->argv[2]);

    ele = c->argv[3] = tryObjectEncoding(c->argv[3]);

    // 如果 src 为空, 返回
    if (srcset == NULL) {
        addReply(c,shared.czero);
        return;
    }

    // src 和 dst 的类型检查
    if (checkType(c,srcset,REDIS_SET) ||
        (dstset && checkType(c,dstset,REDIS_SET))) return;
        
    // src 等于 dst
    if (srcset == dstset) {
        addReply(c,shared.cone);
        return;
    }

    // 删除 src 的 member, 不存在返回
    if (!setTypeRemove(srcset,ele)) {
        // 删除失败
        addReply(c,shared.czero);
        return;
    }

    // 发送通知, 更新键改次数
    notifyKeyspaceEvent(REDIS_NOTIFY_SET,"srem",c->argv[1],c->db->id);

    // 如果 src 的集合为空了
    if (setTypeSize(srcset) == 0) {
        dbDelete(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }

    signalModifiedKey(c->db,c->argv[1]);
    signalModifiedKey(c->db,c->argv[2]);

    server.dirty++;

    // 如果 dst 为空, 那么创建一个空集合
    if (!dstset) {
        dstset = setTypeCreate(ele);
        dbAdd(c->db,c->argv[2],dstset);
    }

    // 添加 member 到 dst
    if (setTypeAdd(dstset,ele)) {
        // 发送通知, 更新键改次数
        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sadd",c->argv[2],c->db->id);

        server.dirty++;
    }

    // 回复客户端
    addReply(c, shared.cone);
}

// SISMEMBER key member
void sismemberCommand(redisClient *c) {
    robj *set;

    // 取出集合对象
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    c->argv[2] = tryObjectEncoding(c->argv[2]);
    
    // 回复客户端 , 元素是否存在
    if (setTypeIsMember(set,c->argv[2])) {
        // 存在
        addReply(c,shared.cone);

    } else {
        // 不存在
        addReply(c,shared.czero);
    }
}

// SCARD key
void scardCommand(redisClient *c) {
    robj *set;

    // 取出集合对象
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // 回复客户端
    addReplyLongLong(c,setTypeSize(set));
}

// SPOP key
void spopCommand(redisClient *c) {
    robj *set, *ele, *aux;
    int64_t llele;
    int encoding;

    // 取出集合对象, 不存在直接返回
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // 随机获取一个元素
    encoding = setTypeRandomElement(set,&ele,&llele);

    if (encoding == REDIS_ENCODING_INTSET) {
        ele = createStringObjectFromLongLong(llele);
        set->ptr = intsetRemove(set->ptr,llele,NULL);

    } else if (encoding == REDIS_ENCODING_HT) {
        incrRefCount(ele);
        setTypeRemove(set,ele);
    }

    notifyKeyspaceEvent(REDIS_NOTIFY_SET,"spop",c->argv[1],c->db->id);

    aux = createStringObject("SREM",4);
    rewriteClientCommandVector(c,3,aux,c->argv[1],ele);
    decrRefCount(ele);
    decrRefCount(aux);

    // 回复客户端
    addReplyBulk(c,ele);

    // 集合空了
    if (setTypeSize(set) == 0) {
        dbDelete(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }

    // 发送通知
    signalModifiedKey(c->db,c->argv[1]);

    // 更新键改次数
    server.dirty++;
}

#define SRANDMEMBER_SUB_STRATEGY_MUL 3
// 实现 SRANDMEMBER key [count]
void srandmemberWithCountCommand(redisClient *c) {
    long l;
    unsigned long count, size;
    int encoding, uniq = 1;
    robj *ele, *set;
    int64_t llele;
    dict *d;

    // 取出 count 值
    if (getLongFromObjectOrReply(c,c->argv[2],&l,NULL) != REDIS_OK)
        return;

    // 将 count 负转正, 计算是否随机元素唯一
    if (l >= 0) {
        count = (unsigned) l;

    } else {
        count = -l;
        uniq = 0;
    }

    // 取出集合对象
    if ((set = lookupKeyWriteOrReply(c,c->argv[1],shared.emptymultibulk)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // 集合长度
    size = setTypeSize(set);

    // count = 0, 直接返回
    if (count == 0) {
        addReply(c,shared.emptymultibulk);
        return;
    }


    // case 1: count是负数, 可重复, 随机取 count 个返回
    if (!uniq) {

        addReplyMultiBulkLen(c,count);
        while (count--) {

            encoding = setTypeRandomElement(set,&ele,&llele);
            if (encoding == REDIS_ENCODING_INTSET) {
                addReplyLongLong(c,llele);
            } else {
                addReplyBulk(c,ele);
            }
        }

        return;
    }

    // case 2: count大于集合数量, 返回整个集合
    if (count >= size) {
        sunionDiffGenericCommand(c,c->argv+1,1,NULL,REDIS_OP_UNION);
        return;
    }

    // case 3 和 case 4 需要字典结构
    d = dictCreate(&setDictType,NULL);

    // case 3: count大于集合的三分之一, 随机取太慢, 采用随机删除 2/3 的方法
    if (count * SRANDMEMBER_SUB_STRATEGY_MUL > size) {
        setTypeIterator *si;

        // 将所有元素迭代到字典中
        si = setTypeInitIterator(set);

        while((encoding = setTypeNext(si,&ele,&llele)) != -1) {
            int retval = DICT_ERR;

            if (encoding == REDIS_ENCODING_INTSET) {
                retval = dictAdd(d,createStringObjectFromLongLong(llele),NULL);
            } else {
                retval = dictAdd(d,dupStringObject(ele),NULL);
            }
            redisAssert(retval == DICT_OK);
        }

        setTypeReleaseIterator(si);
        redisAssert(dictSize(d) == size);

        // 随机删除元素
        while (size > count) {
            dictEntry *de;

            de = dictGetRandomKey(d);
            dictDelete(d,dictGetKey(de));

            size--;
        }


    // case 4: 随机取 count 个元素
    } else {
        unsigned long added = 0;

        while (added < count) {

            // 随机元素
            encoding = setTypeRandomElement(set,&ele,&llele);

            // 元素装入 robj 中
            if (encoding == REDIS_ENCODING_INTSET) {
                ele = createStringObjectFromLongLong(llele);
            } else {
                ele = dupStringObject(ele);
            }

            // 添加到字典
            if (dictAdd(d,ele,NULL) == REDIS_OK) {
                added++;
            } else {
                decrRefCount(ele);
            }
        }
    }

    // 返回 case 3 或 case 4 的元素
    if (size > count) {
        dictIterator *di;
        dictEntry *de;

        di = dictGetIterator(d);
        while ((de = dictNext(di)) != NULL) {
            addReplyBulk(c,dictGetKey(de));
        }

        dictReleaseIterator(di);
        dictRelease(d);
    }
}

// SRANDMEMBER key [count]
void srandmemberCommand(redisClient *c) {
    robj *set, *ele;
    int64_t llele;
    int encoding;

    // 填写 count, 调用count专用方法
    if (c->argc == 3) {
        srandmemberWithCountCommand(c);
        return;

    // 参数错误, 报错返回
    } else if (c->argc > 3) {
        addReply(c,shared.syntaxerr);
        return;
    }

    // 获取集合对象
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    // 随机获取元素
    encoding = setTypeRandomElement(set,&ele,&llele);

    // 回复客户端
    if (encoding == REDIS_ENCODING_INTSET) {
        addReplyLongLong(c,llele);
    } else {
        addReplyBulk(c,ele);
    }
}

/**
 * 计算 s1 集合元素数量与 s2 集合元素数量之间的差值
 */
int qsortCompareSetsByCardinality(const void *s1, const void *s2) {
    return setTypeSize((robj**)s1)-setTypeSize((robj**)s2);
}

/**
 * 计算 s2 集合元素数量与 s1 集合元素数量之间的差值
 */
int qsortCompareSetsByRevCardinality(const void *s1, const void *s2) {
    robj *o1 = (robj**)s1;
    robj *o2 = (robj**)s2;

    return (o2 ? setTypeSize(o2) : 0) - (o1 ? setTypeSize(o1) : 0);
}

/**
 * sinter 的通用方法
 */
void sinterGenericCommand(redisClient *c, robj **setkeys, unsigned long setnum, robj *dstkey) {
    
    // 集合数组, 将 setkeys 指向的集合都放在这里
    robj **sets = zmalloc(sizeof(robj*)*setnum);

    setTypeIterator *si;
    robj *eleobj, *dstset = NULL;
    int64_t intobj;
    void *replylen = NULL;
    unsigned long j, cardinality = 0;
    int encoding;

    // 获取集合, 并填入数组
    for (j = 0; j < setnum; j++) {

        // 获取集合对象, 第一次取的是 dest 集合
        // 之后取的是 source 集合
        // 疑问 : 不明白这里区分的原因, sinterstore 是把所有 source 赋给 setkeys
        //       这里是取不到 dst 的
        robj *setobj = dstkey ? lookupKeyWrite(c->db,setkeys[j]) :
                          lookupKeyRead(c->db,setkeys[j]);

        // 集合不存在, 直接回复客户端, 清理变量
        if (!setobj) {
            zfree(sets);
            if (dstkey) {
                if (dbDelete(c->db, dstkey)) {
                    signalModifiedKey(c->db,dstkey);
                    server.dirty++;
                }
                addReply(c,shared.czero);
            } else {
                addReply(c,shared.emptymultibulk);
            }
            return;
        }

        // 检查对象类型
        if (checkType(c,setobj,REDIS_SET)) {
            zfree(sets);
            return;
        }

        sets[j] = setobj;
    }

    // 数据从小到大排序, 将最小的集合放在首地址
    qsort(sets,setnum,sizeof(robj*),qsortCompareSetsByCardinality);

    // 设置了 dst, 创建一个 dst, 交集元素写入
    // 未设置, 申请一个回复客户端的 buf
    if (!dstkey) {
        replylen = addDeferredMultiBulkLength(c);
    } else {
        dstset = createIntsetObject();
    }

    // 提取交集元素
    // 第一个集合时元素最少的集合, 交集元素一定在这个范围
    si = setTypeInitIterator(sets[0]);

    // 比对获取与最小集合内元素,相同的元素
    while((encoding = setTypeNext(si,&eleobj,&intobj)) != -1) {

        // 遍历其他集合
        for (j = 1; j < setnum; j++) {

            // 跳过第一个集合, 因为它是原始集合
            if (sets[j] == sets[0]) continue;

            // 元素值为 int
            // 在其他集合中查找是否存在这个元素值
            if (encoding == REDIS_ENCODING_INTSET) {

                if (sets[j]->encoding == REDIS_ENCODING_INTSET &&
                    !intsetFind((intset*)sets[j]->ptr,intobj)) 
                {
                    break;

                } else if (sets[j]->encoding == REDIS_ENCODING_HT) {
                    eleobj = createStringObjectFromLongLong(intobj);
                    if (!setTypeIsMember(sets[j],eleobj)) {
                        decrRefCount(eleobj);
                        break;
                    }
                    decrRefCount(eleobj);
                }

            } else {
                if (eleobj->encoding == REDIS_ENCODING_INT &&
                        sets[j]->encoding == REDIS_ENCODING_INTSET &&
                        !intsetFind((intset*)sets[j]->ptr,(long)eleobj->ptr)) 
                {
                    break;

                } else if (!setTypeIsMember(sets[j],eleobj)) {
                    break;
                }
            }
        }

        // 交集元素
        if (j == setnum) {

            // SINTER 命令，直接返回结果集元素
            if (!dstkey) {
                if (encoding == REDIS_ENCODING_HT) {
                    addReplyBulk(c,eleobj);
                } else {
                    addReplyLongLong(c,intobj);
                }
                cardinality++;
            // SINTERSTORE命令, 添加交集元素到 dst 集合
            } else {
                if (encoding == REDIS_ENCODING_INTSET) {
                    eleobj = createStringObjectFromLongLong(intobj);
                    setTypeAdd(dstset,eleobj);
                    decrRefCount(eleobj);
                } else {
                    setTypeAdd(dstset,eleobj);
                }
            }
        }
    }
    setTypeReleaseIterator(si);

    // SINTERSTORE 命令，将结果集关联到数据库
    if (dstkey) {

        // 删除现有 dstkey
        int deleted = dbDelete(c->db,dstkey);

        // 如果结果集非空, 将它关联到数据库中
        if (setTypeSize(dstset) > 0) {
            dbAdd(c->db,dstkey,dstset);
            addReplyLongLong(c,setTypeSize(dstset));
            notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sinterstore",dstkey,c->db->id);

        // 结果集是空的, 干掉
        } else {
            decrRefCount(dstset);
            addReply(c,shared.czero);
            if (deleted)
                notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",dstkey,c->db->id);
        }

        signalModifiedKey(c->db,dstkey);

        server.dirty++;
    
    // SINTER 命令, 回复结果集的基数
    } else {
        setDeferredMultiBulkLength(c,replylen,cardinality);
    }

    zfree(sets);
}

// SINTER key [key ...]
void sinterCommand(redisClient *c) {
    sinterGenericCommand(c,c->argv+1,c->argc-1,NULL);
}

// SINTERSTORE destination key [key ...]
void sinterstoreCommand(redisClient *c) {
    sinterGenericCommand(c,c->argv+2,c->argc-2,c->argv[1]);
}

/**
 * 命令类型, 并集/差集/交集
 */
#define REDIS_OP_UNION 0
#define REDIS_OP_DIFF 1
#define REDIS_OP_INTER 2

void sunionDiffGenericCommand(redisClient *c, robj **setkeys, int setnum, robj *dstkey, int op) {
    robj **sets = zmalloc(sizeof(robj*)*setnum);

    setTypeIterator *si;
    robj *ele, *dstset = NULL;
    int j, cardinality = 0;
    int diff_algo = 1;

    // 取出所有集合对象, 并添加到集合数组
    for (j = 0; j < setnum; j++) {
        
        // 取出集合对象
        robj *setobj = dstkey ? lookupKeyWrite(c->db,setkeys[j]) :
                          lookupKeyRead(c->db,setkeys[j]);

        // 不存在的集合, 给 NULL
        if (!setobj) {
            sets[j] = NULL;
            continue;
        }

        // 检查对象类型
        if (checkType(c,setobj,REDIS_SET)) {
            zfree(sets);
            return;
        }

        // 添加到集合数组
        sets[j] = setobj;
    }

    /**
     *  DIFF 算法决策
     * 
     * 算法 1 的复杂度为 O(N*M), N 是 set[0] 的元素数量, M 是其他集合的数量
     * 
     * 算法 2 的复杂度为 O(N), N 是全部集合的元素数量
     * 
     * 程序通过计算复杂度来决策使用哪一种算法
     */
    if (op == REDIS_OP_DIFF && sets[0]) {
        long long algo_one_work = 0, algo_two_work = 0;

        // 遍历所有集合
        for (j = 0; j < setnum; j++) {
            // 跳过空集合
            if (sets[j] == NULL) continue;

            // 算法 1 的复杂度
            algo_one_work += setTypeSize(sets[0]);

            // 算法 2 的复杂度
            algo_two_work += setTypeSize(sets[j]);
        }

        // 算法 1 的常数比较低, 优先考虑算法 1
        algo_one_work /= 2;
        diff_algo = (algo_one_work <= algo_two_work) ? 1 : 2;

        // 如果使用算法 1, 那么最好对 set[0] 以外的集合进行从大到小排序
        // 这样有助于优化算法性能
        if (diff_algo == 1 && setnum > 1) {

            qsort(sets+1,setnum-1,sizeof(robj*),
                qsortCompareSetsByRevCardinality);
        }
    }

    // 创建一个空的目标集合
    dstset = createIntsetObject();

    // 并集计算
    if (op == REDIS_OP_UNION) {

        for (j = 0; j < setnum; j++) {

            si = setTypeInitIterator(sets[j]);
            while ((ele = setTypeNextObject(si)) != NULL) {
                if (setTypeAdd(dstset,ele)) cardinality++;
                decrRefCount(ele);
            }
            setTypeReleaseIterator(si);
        }


    // 差集计算, 使用算法 1
    } else if (op == REDIS_OP_DIFF && sets[0] && diff_algo == 1) {


        /**
         * 迭代 set[0] 集合的元素, 当前元素不存在所有其他集合时
         * 
         * 将元素添加到目标集合
         */
        si = setTypeInitIterator(sets[0]);
        while ((ele = setTypeNextObject(si)) != NULL) {
            
            // 查找交集元素
            for (j = 1; j < setnum; j++) {
                if (!sets[j]) continue;
                if (sets[j] == sets[0]) break;
                if (setTypeIsMember(sets[j],ele)) break;
            }

            // 不存在交集元素
            if (j == setnum) {
                setTypeAdd(dstset,ele);
                cardinality++;
            }

            decrRefCount(ele);
        }
        setTypeReleaseIterator(si);

    // 差集计算, 使用算法 2
    } else if (op == REDIS_OP_DIFF && sets[0] && diff_algo == 2) {

        /**
         * 将 set[0] 集合的元素添加到 目标集合(dstset)
         * 
         * 其他集合中的元素, 如果存在 dstset 中, 将其移出 dstset
         */
        for (j = 0; j < setnum; j++) {
            if (!sets[j]) continue;

            si = setTypeInitIterator(sets[j]);
            while ((ele = setTypeNextObject(si)) != NULL) {

                if (j == 0) {
                    if (setTypeAdd(dstset,ele)) cardinality++;
                } else {
                    if (setTypeRemove(dstset,ele)) cardinality--;
                }
                decrRefCount(ele);
            }
            setTypeReleaseIterator(si);

            if (cardinality == 0) break;
        }
    }

    
    // 执行 SDIFF 或 SUNION命令
    // 打印目标集合的所有元素

    // 目标集合不存在
    if (!dstkey) {
        addReplyMultiBulkLen(c,cardinality);

        // 迭代目标集合, 回复客户端
        si = setTypeInitIterator(dstset);
        while ((ele = setTypeNextObject(si)) != NULL) {
            addReplyBulk(c,ele);
            decrRefCount(ele);
        }
        setTypeReleaseIterator(si);

        decrRefCount(dstset);

    // 目标集合存在
    } else {

        // 删除可能存在的目标集合
        int deleted = dbDelete(c->db,dstkey);

        // 目标集合不为空, 将目标集合关联到数据库中
        if (setTypeSize(dstset) > 0) {
            dbAdd(c->db,dstkey,dstset);
            addReplyLongLong(c,setTypeSize(dstset));
            notifyKeyspaceEvent(REDIS_NOTIFY_SET,
                op == REDIS_OP_UNION ? "sunionstore" : "sdiffstore",
                dstkey,c->db->id);


        // 目标集合为空, 清除内存
        } else {
            decrRefCount(dstset);
            // 回复客户端 0
            addReply(c,shared.czero);

            if (deleted)
                notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                    dstkey,c->db->id);
        }

        signalModifiedKey(c->db,dstkey);

        server.dirty++;
    }

    // 释放集合数组
    zfree(sets);
}

// SUNION key [key ...]
void sunionCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+1,c->argc-1,NULL,REDIS_OP_UNION);
}

// SUNIONSTORE destination key [key ...]
void sunionstoreCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+2,c->argc-2,c->argv[1],REDIS_OP_UNION);
}

// SDIFF key [key ...]
void sdiffCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+1,c->argc-1,NULL,REDIS_OP_DIFF);
}

// SDIFFSTORE destination key [key ...]
void sdiffstoreCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+2,c->argc-2,c->argv[1],REDIS_OP_DIFF);
}