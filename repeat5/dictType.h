
// 节点键
typedef struct keyObject
{
    int val;
} keyObject;

// 节点值
typedef struct valObject
{
    int val;
} valObject;

// 创建一个键
keyObject *keyCreate(int n);
// 创建一个值
valObject *valCreate(int n);
// 对比键
int keyCompare(void *privdata,const void *key1,const void *key2);
// 销毁键
void keyDestructor(void *privdata,void *key);
// 销毁值
void valDestructor(void *privdata,void *val);
// 计算键的哈希值
unsigned int keyHashIndex(const void *key);