#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "ae.h"
#include "zmalloc.h"
#include "config.h"

// 根据系统环境, 选择不同的IO多路复用库
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
    #ifdef HAVE_EPOLL
    #include "ae_epoll.c"
    #else
        #ifdef HAVE_KQUEUE
        #include "ae_kqueue.c"
        #else
        #include "ae_select.c"
        #endif
    #endif
#endif

/**
 * 初始化事件处理器
 */
aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;

    // 初始化文件事件结构和已就绪文件事件结构数组
    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;

    // 设置数组大小
    eventLoop->setsize = setsize;

    // 初始化最近一次执行时间
    eventLoop->lastTime = time(NULL);

    // 初始化时间事件结构
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;

    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;

    // 初始化IO多路复用实例
    if (aeApiCreate(eventLoop) == -1) goto err;

    // 初始化监听时间
    for (i=0; i<setsize; i++)
        eventLoop->events[i].mask = AE_NONE;

    return eventLoop;

err:
    if (eventLoop) {
        zfree(eventLoop->events);
        zfree(eventLoop->fired);
        zfree(eventLoop);
    }
    return NULL;
}

/**
 * 返回当前事件槽大小
 */
int aeGetSetSize(aeEventLoop *eventLoop) {
    return eventLoop->setsize;
}

/**
 * 调整事件槽大小
 * 成功返回 AE_OK, 失败返回 AE_ERR
 */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    int i;

    if (setsize == eventLoop->setsize) return AE_OK;
    if (eventLoop->maxfd >= setsize) return AE_ERR;
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    eventLoop->setsize = setsize;

    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    
    return AE_OK;
}

/**
 * 删除事件处理器
 */
void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);
    zfree(eventLoop->events);
    zfree(eventLoop->fired);
    zfree(eventLoop);
}

/**
 * 停止事件处理器
 */
void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

/**
 * 根据 mask 参数的值, 监听 fd 文件的状态
 * 当 fd 可用时, 执行 proc 函数
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }

    // 取出文件事件结构
    aeFileEvent *fe = &eventLoop->events[fd];

    // 将 fd 加入监听数组
    if (aeApiAddEvent(eventLoop,fd,mask) == -1)
        return AE_ERR;

    // 设置文件事件类型, 以及事件处理器
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;

    // 私有数据
    fe->clientData = clientData;

    // 更新最大 fd
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    // 返回
    return AE_OK;
}

/**
 * 将 fd 从 mask 指定的监听队列中删除
 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask) {

    if (fd >= eventLoop->setsize) return;

    // 取出文件事件结构
    aeFileEvent *fe = &eventLoop->events[fd];

    // 未设置监听的事件类型, 直接返回
    if (fe->mask == AE_NONE) return;

    // 计算新掩码
    fe->mask = fe->mask & (~mask);

    // 更新最大 fd
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        int j;
        for (j = eventLoop->maxfd-1; j >=0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }

    // 取消 fd 的事件监视
    aeApiDelEvent(eventLoop,fd,mask);
}

/**
 * 获取给定 fd 正在监听的事件类型
 */
int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return 0;
    aeFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

/**
 * 取出当前时间的秒和毫秒
 * 填充到参数中
 */
static void aeGetTime(long *seconds, long *milliseconds) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

/**
 * 将当前时间加上 milliseconds 的时间
 * 并将结果的秒和毫秒分别存在 sec 和 ms 中
 */
static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    // 获取当前时间
    aeGetTime(&cur_sec,&cur_ms);

    // 计算增加后的秒和毫秒
    when_sec = cur_sec + milliseconds/1000;
    when_ms = cur_ms + milliseconds%1000;

    // 进位
    if (when_ms >= 1000) {
        when_sec++;
        when_ms -= 1000;
    }

    // 保存
    *sec = when_sec;
    *ms = when_ms;
}

/**
 * 创建时间事件
 */
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    // 更新时间计数器
    long long id = eventLoop->timeEventNextId++;

    // 创建时间事件结构
    aeTimeEvent *te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;

    // 设置id
    te->id = id;

    // 设置处理的时间
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);

    // 设置事件处理器
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;

    // 设置私有数据
    te->clientData = clientData;

    // 新事件放进表头
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;

    // 返回
    return id;
}


/*
 * 删除给定 id 的时间事件
 */
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id) {

    aeTimeEvent *te, *prev = NULL;

    // 遍历链表
    te = eventLoop->timeEventHead;
    while (te) {

        // 目标事件, 删除
        if (te->id == id) {
            if (prev == NULL)
                eventLoop->timeEventHead = te->next;
            else 
                prev->next = te->next;

            // 清理处理器
            if (te->finalizerProc)
                te->finalizerProc(eventLoop,te->clientData);

            // 释放时间事件
            zfree(te);

            return AE_OK;
        }

        // 下一个节点
        prev = te;
        te = te->next
    }
}


/**
 * 查找距离目前最近的时间事件
 * 因为链表是乱序, 查找复杂度为O(N)
 */
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop) {
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;

    while (te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
            (te->when_sec == nearest->when_sec && 
                te->when_ms < nearest->when_ms))
            nearest = te;

        te = te->next;
    }
    return nearest;
}

/**
 * 处理所有已到时的时间事件
 * 返回处理的时间事件的数量
 */
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te;
    long long maxId;
    time_t now = time(NULL);

    // 重置事件的运行时间
    // 防止因时间穿插而造成的时间处理混乱
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while (te) {
            te->when_sec = 0;
            te = te->next;
        }
    }

    // 更新最后一次处理时间事件的时间
    eventLoop->lastTime = now;

    // 遍历链表, 执行到达的事件
    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId-1;
    while (te) {
        long now_sec, now_ms;
        long long id;

        // 跳过无效事件
        if (te->id > maxId) {
            te = te->next;
            continue;
        }

        // 获取当前时间
        aeGetTime(&now_sec,&now_ms);

        // 到达事件
        if (now_sec > te->when_sec || 
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            // 执行事件处理器
            retval = te->timeProc(eventLoop,te->clientData);
            processed++;

            // 循环执行
            if (retval != AE_NOMORE) {
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            // 只执行一次
            } else {
                aeDeleteTimeEvent(eventLoop,id);
            }

            te = eventLoop->timeEventHead;

        } else {
            te = te->next;
        }
    }

    return processed;
}


/**
 * 处理所有已到达的时间事件, 以及所有已就绪的文件事件
 * 
 * 如果不传入特殊 flags 的话, 那么函数睡眠直到文件事件就绪
 * 或者下个时间事件到达
 * 
 * 如果 flags 为 0, 那么函数不做动作, 直接返回
 * 
 * 如果 flags 包含 AE_ALL_EVENTS, 所有类型的事件被处理
 * 如果 flags 包含 AE_FILE_EVENTS, 那么处理文件事件
 * 如果 flags 包含 AE_TIME_EVENTS, 那么处理时间事件
 * 
 * 如果 flags 包含 AE_DONT_WAIT, 函数在处理完所有不阻塞的事件之后, 即刻返回
 * 
 * 函数返回已处理事件的数量
 */
int aeProcessEvents(aeEventLoop *eventLoop, int flags) {
    int processed = 0, numevents;

    if (!(flags & AE_TIME_EVENTS) && ！(flags & AE_FILE_EVENTS)) return 0;


    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        aeTimeEvent *shortest = NULL;
        struct timeval tv, *tvp;

        // 获取最近的时间事件
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT)) 
            shortest = aeSearchNearestTimer(eventLoop);
        if (shortest) {
            long now_sec, now_ms;

            // 存在可执行的时间事件
            // 那么根据可执行时间距当前时间的时间差来决定文件事件的阻塞时间
            aeGetTime(&now_sec,&now_ms);
            tvp = &tv;
            // 计算时间差
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms) * 1000;
                tvp->tv_sec--;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms) * 1000;
            }

            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;

        } else {

            // 不存在时间事件
            if (flags & AE_DONT_WAIT) {
                // 设置文件事件不阻塞
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                // 文件事件阻塞直到事件到达
                tvp = NULL;
            }

        }
        
        // 处理文件事件, 阻塞时间有 tvp 决定
        numevents = aeApiPoll(eventLoop,tvp);
        for (j=0; j<numevents; j++) {

            // 从就绪数据中获取事件
            aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];

            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int rfired = 0;

            // 读事件
            if (fe->mask & mask & AE_READABLE) {
                rfired = 1;
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);

            }
            // 写事件
            if (fe->mask & mask & AE_WRITABLE) {
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
            }

            processed++;
        }
    }

    // 执行时间事件
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed;
}

/**
 * 在给定毫秒内等待, 直到 fd 变成可读、可写或异常
 */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds)) == 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return mask;
    } else {
        return retval;
    }
}

/*
 * 事件处理器的主循环
 */
void aeMain(aeEventLoop *eventLoop) {

    eventLoop->stop = 0;

    while (!eventLoop->stop) {
        // 如有需要, 在事件处理之前执行的函数
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);

        // 开始处理事件
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}

char *aeGetApiName(void) {
    return aeApiName();
}

void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}