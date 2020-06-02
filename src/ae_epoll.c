/**
 * 
 * epoll_create, 创建一个白板
 * epoll_ctl, 构建 fd 与 event 关系, 并添加到 eptr
 * epoll_wait, 阻塞检查 eptr 是否存在就绪 fd, 如存在,返回就绪 fd 的数量
 */
#include <sys/epoll.h>
#include <ae.h>

typedef struct aeApiState {
    
    // 实例描述符
    int epfd;

    // 事件槽
    struct epoll_event *events;
} aeApiState;


/**
 * 创建一个新的 epoll 实例, 并将它赋值给 eventLoop
 */
static int aeApiCreate(aeEventLoop *eventLoop) {

    // 创建事件状态
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    
    // 初始化事件槽空间
    state->events = zmalloc(sizeof(struct epoll_event) * eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }

    // 创建 epoll 实例
    state->epfd = epoll_create(1024);
    if (state->epfd == -1) {
        zfree(state->events);
        zfree(state);
        return -1;
    }

    // 赋值给 eventLoop
    eventLoop->apidata = state;
    return 0;
}

/**
 * 调整事件槽大小
 */
static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;

    state->events = zrealloc(state->events,sizeof(struct epoll_event)*eventLoop->setsize);
    return 0;
}

/**
 * 释放 epoll 实例和事件槽
 */
static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->epfd);
    zfree(state->events);
    zfree(state);
}

/**
 * 关联给定事件到 fd
 */
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee;

    // fd 未关联事件, 那么这是一个 ADD 操作
    // 关联某些事件, 这是一个 MOD 操作
    int op = eventLoop->events[fd].mask == AE_NONE ?
        EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    // 注册事件到 epoll
    ee.events = 0;
    mask |= eventLoop->events[fd].mask;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0;
    ee.data.fd = fd;

    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) return -1;
    
    return 0;
}

/**
 * 从 fd 中删除给定事件
 */
static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee;

    int mask = eventLoop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0;
    ee.data.fd = fd;
    if (mask != AE_NONE) {
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } else {
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
}

/**
 * 获取可执行事件
 */
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    // 等待事件准备就绪
    retval = epoll_wait(state->epfd,state->events,eventLoop->setsize,
        tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);

    // 将就绪事件添加到就绪队列
    if (retval > 0) {
        int j;

        numevents = retval;
        for (j=0; j<numevents; j++) {

            int mask = 0;
            struct epoll_events *e = state->events+j;

            if (e->events & EPOLLIN) mask |= AE_READABLE;
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            if (e->events & EPOLLERR) mask |= AE_WRITABLE;
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE;

            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    }

    // 返回就绪事件数量
    return numevents;
}


/**
 * 返回正在使用的多路复用库的名字
 */
static char *aeApiName(void) {
    return "epoll";
}
