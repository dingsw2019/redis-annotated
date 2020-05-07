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



/*----------------------------- 基础函数 ------------------------------*/

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
