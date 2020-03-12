#include "dict.h"

extern dictType initDictType;

static int dict_can_resize = 1;
static unsigned int dict_can_force_resize_ratio = 5;

// 初始化或重置哈希表
static int _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 初始化字典属性
int _dictInit(dict *d, dictType *type, void *pridvDataPtr)
{
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = pridvDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;

    return DICT_OK;
}

// 创建并返回一个空字典
dict *dictCreate(dictType *type, void *pridvDataPtr)
{
    // 申请内存
    dict *d = zmalloc(sizeof(*d));
    // 初始化属性
    _dictInit(d,type,pridvDataPtr);

    return d;
}

// 扩容大小策略
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned int i = DICT_HT_INITIAL_SIZE;
    if (size >= LONG_MAX) return LONG_MAX;
    while (1) {
        if (i > size)
            return i;
        i *= 2;
    }
}

// 执行扩容,成功返回 DICT_OK, 否则 DICT_ERR
int dictExpand(dict *d,unsigned long size)
{   
    // dictht *n; myerr
    dictht n;
    unsigned long realsize;
    // 获取扩容大小策略给的长度
    realsize = _dictNextPower(size);

    // 不扩容情况
    // 1.rehash 状态
    // 2.已使用节点数大于申请节点数
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    // 申请内存,初始化新哈希表属性
    // n->table = zcalloc(realsize*sizeof(struct dictEntry)); myerr
    // n->size = realsize;
    // n->sizemask = realsize - 1;
    // n->used = 0;
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.used = 0;

    // 优先赋值给 0 号哈希表
    if (d->ht[0].size == 0) {
        // d->ht[0] = *n;
        d->ht[0] = n;
        return DICT_OK;
    }

    // 赋值给 1 号哈希表,并开启 rehash
    // d->ht[1] = *n;
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 扩容控制策略
static int _dictExpandIfNeeded(dict *d)
{
    // rehash 不处理
    if (dictIsRehashing(d)) return DICT_OK;

    // 初始化扩容
    if (d->ht[0].size == 0) 
        return dictExpand(d,DICT_HT_INITIAL_SIZE);

    // 容量严重不足,翻倍扩容
    if (d->ht[0].used >= d->ht[0].size && 
        (dict_can_resize || 
         (d->ht[0].used/d->ht[0].size) > dict_can_force_resize_ratio))
    {
        return dictExpand(d,d->ht[0].used*2);
    }

    // myerr 缺少
    return DICT_OK;
}

// 检查key是否存在于哈希表中
// 存在返回 -1 , 不存在返回索引值
static int _dictKeyIndex(dict *d, void *key)
{
    dictEntry *he,*nextHe;
    // int index,idx,table; myerr
    unsigned int index,idx,table;
    // 是否扩容
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    // 计算哈希值
    index = dictHashKey(d,key);

    // 遍历哈希表
    for (table=0; table<=1; table++) {

        // 计算索引值
        idx = index & d->ht[table].sizemask;

        he = d->ht[table].table[idx];
        // 遍历节点链表,查找目标节点
        while (he) {
            if (dictCompareKey(d,he->key,key)) {
                return -1;
            }
            he = he->next;
        }
        // 非 rehash 状态, 不查看 1 号哈希表
        if (!dictIsRehashing(d)) break;
    }

    // 未找到
    return idx;
}

// 尝试将键添加到新节点中
// 成功返回节点, 失败返回 NULL
dictEntry *dictAddRaw(dict *d, void *key)
{
    dictEntry *entry;
    dictht *ht;
    int index;

    // myerr 缺少
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 检查键是否已存在于哈希表中
    // 存在返回 -1
    if ((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    // 确定哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    // 创建新节点
    entry = zmalloc(sizeof(*entry));

    // 节点添加到节点链表
    entry->next = ht->table[index];
    ht->table[index] = entry;

    // 设置节点的键
    dictSetKey(d,entry,key);

    // 更新已用节点数
    ht->used++;

    // 返回
    return entry;
}

// 添加节点
// 成功返回 DICT_OK, 失败返回 DICT_ERR
int dictAdd(dict *d, void *key, void *val)
{
    // 尝试添加节点,如果成功返回节点信息,
    // 失败返回 NULL
    dictEntry *entry = dictAddRaw(d,key);
    if (!entry) return DICT_ERR;

    // 添加成功,设置 value
    dictSetVal(d,entry,val);

    // 返回
    return DICT_OK;
}

// 查找节点
// 成功返回节点, 否则返回 NULL
dictEntry *dictFind(dict *d, void *key)
{   
    dictEntry *he;
    unsigned int h,idx,table;

    // 空字典,不进行查找 myerr:缺少
    if (d->ht[0].size == 0) return NULL;

    // 尝试单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算哈希值
    h = dictHashKey(d,key);

    // 遍历哈希表
    for (table=0; table<=1; table++) {

        // 计算索引值
        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];
        // 遍历节点链表
        while (he) {
            // 找到节点
            if (dictCompareKey(d,he->key,key)) {
                return he;
            }
            // 处理下一个节点
            he = he->next;
        }

        if (!dictIsRehashing(d)) break;
    }

    // 未找到
    return NULL;
}

// 节点替换或新增
// 如果节点已存在, 替换节点值为 val, 返回 0
// 如果节点不存在, 新增节点, 返回 1
int dictReplace(dict *d, void *key, void *val)
{   
    dictEntry *entry,auxentry;
    // 尝试新增节点
    if (dictAdd(d,key,val) == DICT_OK)
        return 1;

    // 走到这里说明节点已存在,尝试修改
    entry = dictFind(d,key);

    auxentry = *entry;

    // 修改节点值
    dictSetVal(d,entry,val);

    // 释放节点
    dictFreeVal(d,&auxentry);

    return 0;
}

// 查找或新增,目的是返回节点
dictEntry *dictReplaceRaw(dict *d, void *key)
{
    dictEntry *entry = dictFind(d,key);
    return (entry) ? entry : dictAddRaw(d,key);
}

// 随机返回节点
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he,*origHe;
    unsigned int idx;
    int listlen, listele;

    // 空字典不处理
    if (d->ht[0].size == 0) return NULL;

    // 尝试 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 随机哈希表和索引值
    if (dictIsRehashing(d)) {
        do{
            idx = rand() % (d->ht[0].size + d->ht[1].size);
            he = (idx >= d->ht[0].size) ? 
                d->ht[1].table[idx-d->ht[0].size] : d->ht[0].table[idx];
        }while(he == NULL);
    } else {
        do{
            idx = rand() & d->ht[0].sizemask;
            he = d->ht[0].table[idx];
        }while(he == NULL);
    }

    // 随机节点链表
    origHe = he;
    listlen = 0;
    while (he) {
        listlen++;
        he = he->next;
    }

    listele = rand() % listlen;
    he = origHe;
    while(listele--) he = he->next;

    return he;
}

// 删除节点
// 删除成功返回 DICT_OK, 否则返回 DICT_ERR
int dictGenericDelete(dict *d, void *key,int nofree)
{
    dictEntry *he,*prevHe;
    unsigned int h,idx,table;
    // 空字典不处理
    if (d->ht[0].size == 0) return DICT_ERR;

    // 尝试单步 rehash
    // if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算哈希值
    h = dictHashKey(d, key);

    // 遍历哈希表
    for (table=0; table<=1; table++) {

        // 计算索引值
        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];
        prevHe = NULL;
        // 遍历节点链表
        while (he) {

            // 找到目标节点,并删除
            if (dictCompareKey(d,he->key,key)) {
                
                if (prevHe) {
                    prevHe->next = he->next;
                } else {
                    d->ht[table].table[idx] = he->next;
                }

                dictFreeVal(d,he);
                dictFreeKey(d,he);
                zfree(he);

                d->ht[table].used--;

                return DICT_OK;
            }
            prevHe = he;
            // 处理下一个节点
            he = he->next;
        }
    }

    // 未找到
    return DICT_ERR;
}

// 删除节点并释放键值对
int dictDelete(dict *d, void *key)
{
    return dictGenericDelete(d,key,0);
}

// 删除节点但不释放键值对
int dictDeleteNoFree(dict *d, void *key)
{
    return dictGenericDelete(d,key,1);
}

// 删除哈希表数组并重置哈希表属性
int _dictClear(dict *d, dictht *ht, void (*callback)(void *))
{
    dictEntry *he,*next;
    unsigned int i;

    // 遍历节点数组
    for (i=0; i<ht->size && ht->used>0; i++) {

        // 回调
        if (callback && (i % 65535)==0) callback(d->privdata);

        // 跳过空节点
        if ((he = ht->table[i]) == NULL) continue;
        // 遍历节点链表
        while (he) {
            next = he->next;
            // 释放键值对
            dictFreeVal(d,he);
            dictFreeKey(d,he);
            zfree(he);
            he = next;

            ht->used--;
        }
    }

    // 释放哈希表数组
    zfree(ht->table);

    // 重置哈希表
    _dictReset(ht);

    return DICT_OK;
}

// 释放字典
void dictRelease(dict *d)
{
    // 释放哈希表
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);

    // 释放字典
    zfree(d);
}

// 创建一个不安全的迭代器
dictIterator *dictGetIterator(dict *d)
{
    // 申请内存空间
    dictIterator *iter = zmalloc(sizeof(*iter));
    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

// 创建一个安全的迭代器
dictIterator *dictGetSafeIterator(dict *d)
{
    dictIterator *iter = dictGetIterator(d);
    iter->safe = 1;
    return iter;
}

// 销毁迭代器 myerr
dictIterator *dictReleaseIterator(dictIterator *iter)
{
    // myerr 缺少
    if(!(iter->index == -1 && iter->table ==0)) {
        if (iter->safe) {
            iter->d->iterators--;
        } else {
            // assert(iter->fingerprint == dictFingerPrint(iter->d));
        }
    }

    zfree(iter);
}

// 迭代器,前进一个节点
dictEntry *dictNext(dictIterator *iter)
{
    dictht *ht;
    // 进入这里的两种情况
    // 1.迭代器首次进入
    // 2.新的节点数组
    while (1) {

        if (iter->entry == NULL) {

            ht = &iter->d->ht[iter->table];

            // 首次进入
            if (iter->table == 0 && iter->index == -1) {
                if (iter->safe) {
                    iter->d->iterators++;
                } else {
                    // iter->fingerprint = fingerPrint(iter->d);
                }
            }

            iter->index++;

            // if (iter->index == ht->size) { myerr
            if (iter->index >= (unsigned)ht->size) {

                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    // myerr 缺少
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }

            // myerr 缺少
            iter->entry = ht->table[iter->index];
            
        } else {
            iter->entry = iter->nextEntry;
        }

        if (iter->entry) {
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }

    return NULL;
}

// N 步 rehash
// 返回 1, 表示还有节点可以 rehash
// 返回 0, 表示 rehash 完成
int dictRehash(dict *d, int n)
{
    // int h;
    // 必须是 rehash 状态
    if (!dictIsRehashing(d)) return 0;

    while (n--) {
        dictEntry *he,*nextHe;
        // rehash 完成
        if (d->ht[0].used == 0) {
            zfree(d->ht[0].table);
            // 1 号哈希表数据赋值给 0 号哈希表
            d->ht[0] = d->ht[1];
            _dictReset(&d->ht[1]);
            d->rehashidx = -1;
            // myerr 缺少
            return 0;
        }

        // myerr 缺少
        assert(d->ht[0].size > (unsigned)d->rehashidx);

        // 跳过空节点
        while(d->ht[0].table[d->rehashidx] == NULL) d->rehashidx++;

        // 越界检查
        // if (d->rehashidx > d->ht[0].size) {
        //     break;
        // }

        he = d->ht[0].table[d->rehashidx];
        // 遍历节点链表
        while (he) {
            
            unsigned long h;
            nextHe = he->next;// myerr 缺少

            // 计算 key 在 1 号哈希表的索引值
            h = dictHashKey(d,he->key) & d->ht[1].sizemask;

            // 1号哈希表添加节点 myerr
            // d->ht[1].table[h] = he;
            // // 0号哈希表删除节点
            // d->ht[0].table[d->rehashidx] = NULL;
            he->next = d->ht[1].table[h];
            d->ht[1].table[h] = he;

            // 更新哈希表已用节点数
            d->ht[1].used++;
            d->ht[0].used--;
            
            he = nextHe;// myerr 缺少
        }

        // myerr 缺少
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;

    }

    // return 0; myerr
    return 1;
}

// 单步 rehash
static void _dictRehashStep(dict *d)
{
    return dictRehash(d,1);
}

/*--------------------------- debug -------------------------*/
void dictPrintEntry(dictEntry *he)
{
    keyObject *k = (keyObject*)he->key;
    valObject *v = (valObject*)he->v.val;

    printf("dictPrintEntry: k=%d,v=%d\n",k->val,v->val);
}

void dictPrintAllEntry(dict *d)
{
    dictIterator *iter = dictGetIterator(d);
    dictEntry *he;
    while ((he = dictNext(iter)) != NULL) {
        dictPrintEntry(he);
    }
    dictReleaseIterator(iter);
}

// gcc -g zmalloc.c dictType.c dict.c
void main(void)
{
    int ret;
    dictEntry *he;

    srand(time(NULL));

    // 创建一个空字典
    dict *d = dictCreate(&initDictType, NULL);

    // 节点的键值
    keyObject *k = keyCreate(1);
    valObject *v = valCreate(10086);

    // 添加节点
    dictAdd(d, k, v);

    // dictPrintStats(d);
    printf("\ndictAdd, (k=1,v=10086) join dict,dict used size %d\n",dictSize(d));
    printf("---------------------\n");

    // 查找节点
    he = dictFind(d, k);
    if (he) {
        printf("dictFind,k is 1 to find entry\n");
        dictPrintEntry(he);
    } else {
        printf("dictFind, not find\n");
    }
    printf("---------------------\n");

    // 节点值替换
    valObject *vRep = valCreate(10010);
    dictReplace(d,k,vRep);
    he = dictFind(d, k);
    if (he) {
        printf("dictReplace, find entry(k=1,value=10086) and replace value(10010)\n");
        dictPrintEntry(he);
    } else {
        printf("dictReplace, not find\n");
    }
    printf("---------------------\n");

    // 新增节点(dictReplace)
    keyObject *k2 = keyCreate(2);
    valObject *v2 = valCreate(10000);
    dictReplace(d,k2,v2);
    he = dictFind(d, k2);
    if (he) {
        printf("dictAdd through dictReplace, add entry(k=2,value=10000) join dict\n");
        dictPrintEntry(he);
    } else {
        printf("dictAdd through dictReplace, not find entry\n");
    }
    // dictPrintStats(d);
    printf("---------------------\n");

    // 随机获取一个节点
    he = dictGetRandomKey(d);
    if (he) {
        printf("dictGetRandomKey , ");
        dictPrintEntry(he);
    } else {
        printf("dictGetRandomKey , not find entry\n");
    }
    printf("---------------------\n");

    // 通过迭代器获取打印所有节点
    dictPrintAllEntry(d);
    printf("---------------------\n");

    // 删除节点
    ret = dictDelete(d, k);
    printf("dictDelete : %s, dict size %d\n\n",((ret==DICT_OK) ? "yes" : "no"),dictSize(d));
    
    // 释放字典
    dictRelease(d);
}