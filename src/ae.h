


/**
 * 事件处理器状态
 */
struct aeEventLoop;

/**
 * 事件接口
 */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/**
 * 文件事件结构
 */
typedef struct aeFileEvent {
    // 监听事件类型 AE_READABLE | AE_WRITABLE
    int mask;

    // 读事件处理器
    aeFileProc *rfileProc;

    // 写事件处理器
    aeFileProc *wfileProc;

    // 多路复用库的私有数据
    void *clientData;
} aeFileEvent;


/**
 * 时间事件结构
 */
typedef struct aeTimeEvent {
    
    // 时间事件的唯一标识符
    long long id;

    // 事件的到达时间
    long when_sec;
    long when_ms;

    // 事件处理函数
    aeTimeProc *timeProc;

    // 事件释放函数
    aeEventFinalizerProc *finalizerProc;

    // 多路复用库的私有数据
    void *clientData;

    // 指向下一个时间事件, 形成链表
    struct aeTimeEvent *next;

} aeTimeEvent;

/**
 * 就绪事件
 */
typedef struct aeFiredEvent {
    // 已就绪文件描述符
    int fd;
    // 事件类型掩码
    int mask;
} aeFiredEvent;

/**
 * 事件处理器的状态
 */
typedef struct aeEventLoop {

    // 已注册的最大描述符的值
    int maxfd;

    // 已追踪的描述符数量
    int setsize;

    // 时间事件的id(生成时间事件id)
    long long timeEventNextId;

    // 记录最后一次执行时间事件的时间
    time_t lastTime;

    // 已注册的文件事件
    aeFileEvent *events;

    // 已就绪的文件事件
    aeFiredEvent *fired;

    // 时间事件
    aeTimeEvent *timeEventHead;

    // 事件处理器的开关 0开 1关
    int stop;

    // 多路复用库的私有数据
    void *apidata;

    // 处理事件前要执行的函数
    aeBeforeSleepProc *beforesleep;
    
} aeEventLoop;


