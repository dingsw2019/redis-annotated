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

// gcc -g zmalloc.c dictType.c dict.c
void main(void)
{
    int ret;
    dictEntry *he;

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

    // // 节点值替换
    // valObject *vRep = valCreate(10010);
    // dictReplace(d,k,vRep);
    // he = dictFind(d, k);
    // if (he) {
    //     printf("dictReplace, find entry(k=1,value=10086) and replace value(10010)\n");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictReplace, not find\n");
    // }
    // printf("---------------------\n");

    // // 新增节点(dictReplace)
    // keyObject *k2 = keyCreate(2);
    // valObject *v2 = valCreate(10000);
    // dictReplace(d,k2,v2);
    // he = dictFind(d, k2);
    // if (he) {
    //     printf("dictAdd through dictReplace, add entry(k=2,value=10000) join dict\n");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictAdd through dictReplace, not find entry\n");
    // }
    // dictPrintStats(d);
    // printf("---------------------\n");

    // // 随机获取一个节点
    // he = dictGetRandomKey(d);
    // if (he) {
    //     printf("dictGetRandomKey , ");
    //     dictPrintEntry(he);
    // } else {
    //     printf("dictGetRandomKey , not find entry\n");
    // }
    // printf("---------------------\n");

    // // 通过迭代器获取打印所有节点
    // dictPrintAllEntry(d);
    // printf("---------------------\n");

    // // 删除节点
    // ret = dictDelete(d, k);
    // printf("dictDelete : %s, dict size %d\n\n",((ret==DICT_OK) ? "yes" : "no"),dictSize(d));
    
    // // 释放字典
    // dictRelease(d);

}