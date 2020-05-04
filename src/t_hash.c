#include "redis.h"
#include <math.h>

/*----------------------------- 迭代器 ------------------------------*/

/**
 * 初始化哈希结构迭代器
 */
hashTypeIterator *hashTypeInitIterator(robj *subject) {

    // 申请内存空间
    hashTypeIterator *hi = zmalloc(sizeof(hashTypeIterator));

    // 初始化属性
    hi->subject = subject;
    hi->encoding = subject->encoding;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        hi->fptr = NULL;
        hi->vptr = NULL;

    } else if (hi->encoding == REDIS_ENCODING_HT) {
        hi->di = dictGetIterator(subject->ptr);
    
    } else {
        redisPanic("Unknown hash encoding");
    }

    return hi;
}

/**
 * 释放哈希结构迭代器
 */
void hashTypeReleaseIterator(hashTypeIterator *hi) {

    if (hi->encoding == REDIS_ENCODING_HT) {
        dictReleaseIterator(hi->di);
    }

    zfree(hi);
}

/**
 * 移动哈希结构迭代器
 * 移动成功, 返回 REDIS_OK
 * 移动失败, 返回 REDIS_ERR
 */
int hashTypeNext(hashTypeIterator *hi) {

    // ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl;
        unsigned char *fptr, *vptr;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;

        // 首次迭代
        if (fptr == NULL) {
            redisAssert(vptr == NULL);
            fptr = ziplistIndex(zl, 0);
        
        // 移动至下一个哈希的键
        } else {
            redisAssert(vptr != NULL);
            fptr = ziplistNext(zl, vptr);
        }

        // 获取到下一个哈希的键失败
        if (fptr == NULL) return REDIS_ERR;

        // 获取下一个哈希的值
        vptr = ziplistNext(zl, fptr);
        redisAssert(vptr != NULL);

        // 赋值给迭代器的哈希键值指针
        hi->fptr = fptr;
        hi->vptr = vptr;


    } else if (hi->encoding == REDIS_ENCODING_HT) {
        if ((hi->de = dictNext(hi->di)) == NULL) return REDIS_ERR;
    
    } else {
        redisPanic("Unknown hash encoding");
    }

    return REDIS_OK;
}

/*----------------------------- 转码转换 ------------------------------*/
/**
 * 从 ZIPLIST 编码的哈希中, 取出迭代器指针当前指向节点的域或值
 */
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll)
{
    int ret;

    redisAssert(hi->encoding == REDIS_ENCODING_ZIPLIST);

    // 提取域
    if (what & REDIS_HASH_KEY) {
        ret = ziplistGet(hi->fptr, vstr, vlen, vll);
        redisAssert(ret);

    // 提取值
    } else {
        ret = ziplistGet(hi->vptr, vstr, vlen, vll);
        redisAssert(ret);
    }
}

/**
 * 从 ENCODING_HT 编码的哈希中, 取出迭代器指针当前指向节点的域或值
 */
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst) {

    redisAssert(hi->encoding == REDIS_ENCODING_HT);

    if (what & REDIS_HASH_KEY) {
        *dst = dictGetKey(hi->de);

    } else {
        *dst = dictGetVal(hi->de);
    }
}

/**
 * 从迭代器中提取当前编码的键或值, 以 robj 格式返回
 * 函数负责 incrRefCount, 调用方负责 decrRefCount
 */
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what) {
    robj *dst;

    // ziplist
    if (hi->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vlong = LLONG_MAX;

        // 取出域或值
        hashTypeCurrentFromZiplist(hi,what,&vstr,&vlen,&vlong);

        if (vstr) {
            dst = createStringObject(vstr,vlen);
        } else {
            dst = createStringObjectFromLongLong(vlong);
        }

    // hash
    } else if (hi->encoding == REDIS_ENCODING_HT) {

        // 取出域或值
        hashTypeCurrentFromHashTable(hi,what,&dst);
        incrRefCount(dst);

    } else {
        redisPanic("Unknown hash encoding");
    }

    return dst;
}

/**
 * 将 ZIPLIST 编码的哈希对象转换成 enc 编码
 */
void hashTypeConvertZiplist(robj *o, int enc) {
    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);
    
    if (enc == REDIS_ENCODING_ZIPLIST) {
        // nothing to do

    } else if (enc == REDIS_ENCODING_HT){
        hashTypeIterator *hi;
        dict *dict;
        int ret;

        // 迭代器
        hi = hashTypeInitIterator(o);

        // 创建空哈希表
        dict = dictCreate(&hashDictType,NULL);

        // 迭代压缩列表, 迁移元素
        while(hashTypeNext(hi) != REDIS_ERR) {
            robj *field, *value;

            // 取出 ziplist 的键
            field = hashTypeCurrentObject(hi,REDIS_HASH_KEY);
            field = tryObjectEncoding(field);

            // 取出 ziplist 的值
            value = hashTypeCurrentObject(hi,REDIS_HASH_VALUE);
            value = tryObjectEncoding(value);

            // 键值添加到字典
            ret = dictAdd(dict,field,value);
            if (ret != DICT_OK) {
                redisLogHexDump(REDIS_WARNING,"ziplist with dup elements dump",
                    o->ptr,ziplistBlobLen(o->ptr));
                redisAssert(ret == DICT_OK);
            }
        }

        // 释放迭代器
        hashTypeReleaseIterator(hi);

        // 释放压缩列表
        zfree(o->ptr);

        // 更新编码, 绑定哈希表
        o->encoding = REDIS_ENCODING_HT;
        o->ptr = dict;
    
    } else {
        redisPanic("Unknown hash encoding");
    }
}

/**
 * 将哈希对象的编码进行转换
 * 只对 REDIS_ENCODING_ZIPLIST 执行
 */
void hashTypeConvert(robj *o, int enc) {
    
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        hashTypeConvertZiplist(o, REDIS_ENCODING_HT);

    } else if (o->encoding == REDIS_ENCODING_HT) {
        redisPanic("Not implemented");

    } else {
        redisPanic("Unknown hash encoding");
        
    }
}

/**
 * 对 argv 数组中的多个对象进行检查
 * 尝试将 REDIS_ENCODING_ZIPLIST 转换成 REDIS_ENCODING_HT
 * 只对字符串转换, 因为数值不会超过长度限制
 */
void hashTypeTryConversion(robj *o, robj **argv, int start, int end) {
    int i;

    if (o->encoding != REDIS_ENCODING_ZIPLIST) return;

    for (i = start; i <= end; i++) {
        
        if (sdsEncodedObject(argv[i]) && 
            sdslen(argv[i]) > server.hash_max_ziplist_value)
        {
            hashTypeConvert(o, REDIS_ENCODING_HT);
            break;
        }
    }
}

/*----------------------------- 基础函数 ------------------------------*/

/**
 * 当 subject 编码为 REDIS_ENCODING_HT 时
 * 尝试将 o1 和 o2 进行编码压缩, 以节省内存
 */
void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2) {

    if (subject->encoding == REDIS_ENCODING_HT) {
        *o1 = tryObjectEncoding(*o1);
        *o2 = tryObjectEncoding(*o2);
    }
}

/**
 * 从 ziplist 编码的哈希结构中找到 field 指向的值
 * 如果值是字符串, 将内容和长度写入 vstr, vlen
 * 如果值是数值, 写入 vll
 * 
 * 查找成功, 返回 0. 否则返回 -1
 */
int hashTypeGetFromZiplist(robj *o, robj *field, 
                            unsigned char **vstr, unsigned int *vlen, long long *vll) {
    unsigned char *zl, *fptr = NULL, *vptr = NULL;
    int ret;

    redisAssert(o->encoding == REDIS_ENCODING_ZIPLIST);

    field =  getDecodedObject(field);

    zl = o->ptr;
    fptr = ziplistIndex(zl,ZIPLIST_HEAD);
    if (fptr != NULL) {
        // 获取域
        fptr = ziplistFind(fptr, field->ptr,sdslen(field->ptr),1);
        
        // 获取值元素
        if (fptr != NULL) {
            vptr = ziplistNext(zl,fptr);
            redisAssert(vptr != NULL);
        }
    }

    decrRefCount(field);

    // 提取值内容
    if (vptr != NULL) {
        ret = ziplistGet(vptr,*vstr,*vlen,*vll);
        redisAssert(ret);
        return 0;
    }

    // 提取失败
    return -1;
}

/**
 * 从 REDIS_ENCODING_HT 编码的哈希结构中取出 field 指向的值
 * 并存入 value 中
 * 
 * 查找成功, 返回 0, 否则返回 -1
 */
int hashTypeGetFromHashTable(robj *o, robj *field, robj **value) {
    dictEntry *de;

    // 检查编码类型
    redisAssert(o->encoding == REDIS_ENCODING_HT);

    de = dictFind(o->ptr,field->ptr);

    // 不存在的域
    if (de == NULL) return -1;

    // 提取值内容
    *value = dictGetVal(de);

    return 0;
}

/**
 * 在哈希结构中, 查找域(field)的值, 并以 robj 结构返回
 * 多态命令, 支持 ziplist 和 hash 两种编码
 */
robj *hashTypeGetObject(robj *o, robj *field) {
    robj *value = NULL;

    // ziplist
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        // 找到目标域的值
        if (hashTypeGetFromZiplist(o,field,&vstr,&vlen,&vll) == 0) {

            if (vstr) {
                value = createStringObject((char *)vstr,vlen);
            } else {
                value = createStringObjectFromLongLong(vll);
            }
        }

    // hashtable
    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *aux;

        if (hashTypeGetFromHashTable(o,field,&aux) == 0) {
            incrRefCount(aux);
            value = aux;
        }

    // 未知编码
    } else {
        redisPanic("Unknown hash encoding");
    }

    return value;
}

/**
 * 检查给定域(field) 是否存在于哈希结构中
 * 存在返回 1, 否则返回 0
 */
int hashTypeExists(robj *o, robj *field) {

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        
        if (hashTypeGetFromZiplist(o,field,&vstr,&vlen,&vll) == 0) {
            return 1;
        }

    } else if (o->encoding == REDIS_ENCODING_HT) {
        robj *aux;

        if (hashTypeGetFromHashTable(o,field,&aux) == 0) {
            return 1;
        }

    } else {
        redisPanic("Unknown hash encoding");
    }

    return 0;
}

/**
 * 将给定的 field-value 添加到哈希结构中
 * 如果 field 已存在, 更新 value
 * 
 * 这个函数负责对 field 和 value 增加引用计数
 * 
 * 返回 0, 表示新增
 * 返回 1, 表示更新
 */
int hashTypeSet(robj *o, robj *field, robj *value) {
    int update = 0;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr, *vptr;

        // 转成字符串
        field = getDecodedObject(field);
        value = getDecodedObject(value);

        // 查找目标域
        zl = o->ptr;
        fptr = ziplistIndex(zl,ZIPLIST_HEAD);
        
        if (fptr != NULL) {
            fptr = ziplistFind(fptr,field->ptr,sdslen(field->ptr),1);

            // 更新
            if (fptr != NULL) {
                // 定位到值
                vptr = ziplistNext(zl,fptr);
                redisAssert(vptr != NULL);

                update = 1;

                // 删除旧 value
                zl = ziplistDelete(zl,&vptr);

                // 绑定新 value
                zl = ziplistInsert(zl,vptr,value->ptr,sdslen(value->ptr));
            }
        }

        // 新增
        if (!update) {
            zl = ziplistPush(zl,field->ptr,sdslen(field->ptr),ZIPLIST_TAIL);
            zl = ziplistPush(zl,value->ptr,sdslen(value->ptr),ZIPLIST_TAIL);
        }

        // 更新对象
        o->ptr = zl;

        // 释放临时对象
        decrRefCount(field);
        decrRefCount(value);

        // 检查元素数量是否超限, 需要转码
        if (hashTypeLength(o) > server.hash_max_ziplist_entries) {
            hashTypeConvert(o, REDIS_ENCODING_HT);
        }


    } else if (o->encoding == REDIS_ENCODING_HT) {

        // 添加
        if (dictReplace(o->ptr,field,value)) {
            incrRefCount(field);

        // 更新
        } else {
            update = 1;
        }

        incrRefCount(value);

    } else {
        redisPanic("Unknown hash encoding");
    }

    return update;
}

/**
 * 删除哈希表的field-value
 * 删除成功返回 1, 否则返回 0
 */
int hashTypeDelete(robj *o, robj *field) {
    int deleted = 0;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *zl, *fptr;

        field = getDecodedObject(field);

        zl = o->ptr;
        fptr = ziplistIndex(zl,ZIPLIST_HEAD);
        if (fptr != NULL) {
            // 定位到域
            fptr = ziplistFind(fptr,field->ptr,sdslen(field->ptr),1);

            if (fptr != NULL) {
                // 删除域
                zl = ziplistDelete(zl,&fptr);
                
                // 删除值
                zl = ziplistDelete(zl,&fptr);

                // 更新 ptr
                o->ptr = zl;
                deleted = 1;
            }
        }

        decrrefcount(field);

    } else if (o->encoding == REDIS_ENCODING_HT) {
        
        if (dictDelete((dict*)o->ptr, field->ptr) == REDIS_OK) {
            deleted = 1;

            // 字典是否需要收缩
            if (htNeedsResize(o->ptr)) dictResize(o->ptr);
        }

    } else {
        redisPanic("Unknown hash encoding");
    }

    return deleted;
}

/**
 * 哈希表的 field-value 对数量
 */
unsigned long hashTypeLength(robj *o) {
    unsigned long length = ULONG_MAX;

    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        length = ziplistLen(o->ptr) / 2;

    } else if (o->encoding == REDIS_ENCODING_HT) {
        length = dictSize((dict*)o->ptr);

    } else {
        redisPanic("Unknown hash encoding");
    }

    return length;
}