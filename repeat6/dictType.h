#include <stdlib.h>

// 节点的键
typedef struct keyObject {
    int val;
} keyObject;
// 节点的值
typedef struct valObject {
    int val;
} valObject;

keyObject *keyCreate(int index);
valObject *valCreate(int index);