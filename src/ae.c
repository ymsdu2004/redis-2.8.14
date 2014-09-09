/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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

/***
 * 根据系统的支持情况, 优先选择性能最好的IO复用机制
 * 选择顺序: evport -> epoll -> kqueue -> select
 * 平台是否支持某种机制的宏, 在config.h中定义了, 其中select机制是绝大部分平台
 * 都支持的, 也是效率最低的一种
 */
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

aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;

	/* 新建一个aeEventLoop对象 */
    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;
	
	/* 创建保存已注册IO事件的数组, 数组大小为setsize的值, 数组以文件描述符索引
	 * 判断数组元素对应的事件是否注册的依据是mask成员, 如果mask=AE_NONE, 则表示该
	 * fd(数组索引)对应的事件未注册
	 */
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);
	
	/* 创建保存已就绪IO事件的数组, 数组大小为setsize的值 */
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
	
	
    eventLoop->setsize = setsize;
    eventLoop->lastTime = time(NULL);
    
	/* 定时事件链表初始为空 */
	eventLoop->timeEventHead = NULL;
	
	/* 下一个时间ID初始化为0 */
    eventLoop->timeEventNextId = 0;
	
	/* 循环处理停止标志初始化为否 */
    eventLoop->stop = 0;
	
	/* 当前最大文件描述符值初始化为-1, 表示没有注册任何IO事件 */
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;
	
	/* aeApiCreate是选择的IO复用机制中定义的用于创建IO复用机制私有结构体数据 */
    if (aeApiCreate(eventLoop) == -1) goto err;
	
	/* 初始化已注册数组中所有元素的mask成员为AE_NONE, 表示无任何注册IO事件 */
    for (i = 0; i < setsize; i++)
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

/* 获取事件循环处理器最大能处理的描述符值 */
int aeGetSetSize(aeEventLoop *eventLoop) {
    return eventLoop->setsize;
}

/****
 * 修改事件循环处理器的setsize值
 * @eventLoop[IN]: 事件循环处理器
 * @setsize[IN]: IO文件描述符上限(maxfd最大为setsize-1)
 * @return: 成功返回AE_OK, 失败返回AE_ERR
 */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    int i;

	/* 如果要设置的setsize值与eventLoop当前的setsize值相等, 则不执行任何动作 */
    if (setsize == eventLoop->setsize) return AE_OK;
	
	/* 如果要设置的setsize值不大于当前已注册的事件中最大的文件描述符, 则返回失败
 	 * 这个很容易理解, 因为这样修改后, 描述符值大于等于setsize的那部分事件无法保存
	 * 在已注册事件数组中, 所以这种修改是不允许进行的
	 */
    if (eventLoop->maxfd >= setsize) return AE_ERR;
	
	/* 操作系统特定IO复用设施设置setsize值 */
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

	/* 重新分配已注册IO事件数组空间, zrealloc与c语言的realloc函数功能一样 */
    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
	
	/* 重新分配已就绪IO事件数组空间 */
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
	
	/* 更新setsize值 */
    eventLoop->setsize = setsize;
	
	/* 如果扩大了数组空间, 则需要确保新增的槽位中事件的mask初始化为AE_NONE */
    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
		
    return AE_OK;
}

/****
 * 销毁时间循环处理器
 * @eventLoop[IN]: 事件循环处理器
 */
void aeDeleteEventLoop(aeEventLoop *eventLoop) {

	/* 清理操作系统特定IO复用设施 */
    aeApiFree(eventLoop);
	
	/* 释放已注册IO事件、已就绪IO事件数组即eventLoop本身的内存 */
    zfree(eventLoop->events);
    zfree(eventLoop->fired);
    zfree(eventLoop);
}

/***
 * 停止时间循环处理
 */
void aeStop(aeEventLoop *eventLoop) {

	/* 将stop标记置为true*/
    eventLoop->stop = 1;
}

/***
 * 创建IO事件并注册IO事件
 * @eventLoop[IN]: 新建事件要添加到的事件循环处理器
 * @fd[IN]: IO事件对应的文件描述符
 * @mask[IN]: IO事件关注的事件类型, AE_READABLE与AE_WRITEABLE的任意结合
 * @proc[IN]: IO事件就绪处理函数, 这里读和写事件公用一个处理函数, 可以根据处理函数传入的mask区分
 * @clientData[IN]: 用户私有数据, 事件处理函数调用时传回
 * @return: 成功返回AE_OK, 失败返回AE_ERR
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
	/* 如果要注册的IO事件对应的文件描述符不小于setsize, 则返回失败 */
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
	
	/* 根据fd定位到已注册IO事件数组中相应的event元素 */
    aeFileEvent *fe = &eventLoop->events[fd];

	/* 将fd注册到操作系统特定IO复用设施中 */
    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;
		
	/* 以下部分填充fd在数组中对应的event结构体 */
	
	/* 填充mask标记 */
    fe->mask |= mask;
	
	/* 如果关注读事件, 则设置读就绪处理函数 */
    if (mask & AE_READABLE) fe->rfileProc = proc;
	
	/* 如果关注些事件, 则设置写就绪处理函数 */
    if (mask & AE_WRITABLE) fe->wfileProc = proc;
	
	/* 记录用户私有数据 */
    fe->clientData = clientData;
	
	/* 如果新注册的IO事件的fd值大于eventLoop中的maxfd, 则以fd更新maxfd */
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;
		
    return AE_OK;
}

/***
 * 删除IO事件
 * @eventLoop[IN]: 要从中删除事件的事件循环处理器
 * @fd[IN]: 要删除的IO事件对应的文件描述符
 * @mask[IN]: 要删除IO事件的哪种事件, AE_READABLE与AE_WRITEABLE的任意组合
 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
	/* 如果要删除的事件对应的fd不小于setsize, 则不作任何动作, 因为fd必定不再此eventLoop中 */
    if (fd >= eventLoop->setsize) return;
	
	/* 根据fd定位到已注册IO事件数组中相应的event元素 */
    aeFileEvent *fe = &eventLoop->events[fd];
	
	/* 如果该事件的mask标记为AE_NONE, 说明根本未曾注册, 所以也是直接不动作返回 */
    if (fe->mask == AE_NONE) return;

	/* 从操作系统特定IO复用设施中删除fd的mask标记的事件类型 */
    aeApiDelEvent(eventLoop, fd, mask);
	
	/* 从event的mask中去除要删除的掩码位 */
    fe->mask = fe->mask & (~mask);
	
	/* 如果更新后的mask为NONE(说明该IO事件既不关心读也不关心写), 
	 * 意味着fd对应的事件将不再处于注册状态.
	 * 如果此fd恰好也是maxfd, 则需要更新maxfd
	 */
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        int j;
		
		/* 从描述符maxfd-1开始向下查找数组中的元素, 直到遇到的第一个mask不等于AE_NONE的event,
		 * 该event对应的fd就是新的maxfd
		 */
        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }
}

/***
 * 获取给定描述符感兴趣的IO事件类型掩码
 * @eventLoop[IN]: 事件循环处理器
 * @fd[IN]: 事件对应文件描述符
 * @return: fd对应事件在eventLoop中注册的事件的mask
 */
int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {

	/* 如果fd不小于eventLoop的setsize, 则fd对应的事件必定不在eventLoop中, 直接返回AE_NONE */
    if (fd >= eventLoop->setsize) return 0;
	
	/* 通过fd定位到已注册IO事件数组中对应的元素(event结构) */
    aeFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

/****
 * 获取系统当前时间(UTC时间), 以秒和毫秒表示
 * @seconds[OUT]: 当前绝对时间的秒数
 * @milliseconds[OUT]: 当前绝对时间的毫秒数
 */
static void aeGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

	/* 系统调用获取当前系统事件 */
    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
	
	/* 微妙转化为毫秒 */
    *milliseconds = tv.tv_usec/1000;
}

/***
 * 当前时间增加特定毫秒后得到的事件, 以秒和毫秒表示
 * @milliseconds[IN]: 要增加的毫秒数
 * @sec[OUT]: 当前时间增加特定毫秒后的得到的时间的秒数
 * @ms[OUT]: 当前时间增加特定毫秒后的得到的时间的毫秒数
 */
static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

	/* 获取当前时间值, 秒和毫秒部分 */
    aeGetTime(&cur_sec, &cur_ms);
	
	/* 新增毫秒数如果超过1秒, 则直接进位到当前时间的秒部分 */
    when_sec = cur_sec + milliseconds/1000;
	
	/* 新增毫秒不足1秒的部分, 则直接将其加到当前时间的毫秒部分
 	 * 注意: when_ms此时也可能存在进位, 所以下面还要处理when_ms到when_sec的进位
	 */
    when_ms = cur_ms + milliseconds%1000;
	
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
	
	/* 最终得到的未来时间的秒和毫秒部分 */
    *sec = when_sec;
    *ms = when_ms;
}

/***
 * 创建并注册定时事件到eventLoop中
 * @eventLoop[IN]: 新建定时事件要添加到的事件循环处理器
 * @milliseconds[IN]: 定时时间的定时值, 即多少毫秒后定时事件超时
 * @proc[IN]: 定时事件就绪处理函数
 * @clientData[IN]: 用户私有数据
 * @finalizerProc[IN]: 定时事件删除处理函数
 * @return: 新建定时器事件的ID
 */
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
	/* 下一个定时器ID, 初始时,eventLoop的timeEventNextId为0, 即第一个定时器的ID为0
	 * 这里可以看到, eventLoop是非线程安全的, 建议一个线程一个eventLoop
	 */
    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;

	/* 定时事件结构体分配内存 */
    te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
	
	/* 以下操作初始化定时事件 */
	
	/* 定时器ID */
    te->id = id;
	
	/* 将定时的时间值加到当前绝对时间值, 从而得到定时器超时的一个未来绝对时间值 */
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
	
	/* 定时事件超时处理函数 */
    te->timeProc = proc;
	
	/* 定时事件删除处理函数 */
    te->finalizerProc = finalizerProc;
	
	/* 用户私有数据, 通过处理函数传回 */
    te->clientData = clientData;
	
	/* 将定时事件添加到事件循环定时事件链表的头部 */
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;
	
    return id;
}

/***
 * 删除定时事件
 * @eventLoop[IN]: 事件循环处理器
 * @id[IN]: 标识定时器的定时事件ID
 * @return: 成功返回AE_OK, 失败返回AE_ERR
 */
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    aeTimeEvent *te, *prev = NULL;

    te = eventLoop->timeEventHead;
    while(te) {
        if (te->id == id) {
		
			/* 如果是头结点, 则直接修改定时事件链表的头指针 */
            if (prev == NULL)
                eventLoop->timeEventHead = te->next;
            else /* 如果不是头节点, 则将修改上一个节点的next指针 */
                prev->next = te->next;
				
			/* 如果有删除处理函数, 则调用该处理函数 */
            if (te->finalizerProc)
                te->finalizerProc(eventLoop, te->clientData);
				
			/* 释放该定时事件结构体占用的内存 */
            zfree(te);
            return AE_OK;
        }
        prev = te;
        te = te->next;
    }
    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
 
/***
 * 在定时事件链表中查找最先到期的定时事件节点
 * Note: 事件复杂度为O(N), 这里是可以进行改进的地方, 可以借鉴libevent的最小时间堆结构
 * @aeEventLoop[IN]: 事件处理循环
 * @return: 如果找到, 则返回该定时事件指针, 否则返回NULL
 */
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
	/* 定位到定时事件链表头指针 */
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;

    while(te) {
	
		/* 秒值小的必定先到期, 如果秒值相等, 则毫秒值小的先到期 */
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/***
 * 进行一次定时事件处理
 * @eventLoop[IN]: 事件循环处理器(Reactor组件)
 * @return: 本次得到处理的事件数量
 */
static int processTimeEvents(aeEventLoop *eventLoop) 
{
	/* 记录本次得到处理的定时事件数 */
    int processed = 0;
    aeTimeEvent *te;
    long long maxId;
	
	/* 系统当前绝对时间 */
    time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
	 
	/***
	 * 如果发现系统时间, 则将时间值纠正.
	 * 这里我们尝试检测系统时间是否不准确, 如果是, 则尽可能快的处理所有定时事件,
	 * 毕竟提前处理定时事件总比无限期的延迟定时时间更安全, 实践证明这种思想是正确的
	 */
	 
	/* 正常情况下, 当前系统时间值必定是大于eventLoop中记录的上次操作的时间值, 如果不是, 
	 * 则说明系统事件不准确, 此时将所有定时事件的超时时间的秒值设为0(尽可能快递处理所有定时事件),
	 * 同时eventLoop的上次操作事件更新为当前系统时间
	 */
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while(te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    eventLoop->lastTime = now;

    te = eventLoop->timeEventHead;
	
	/* 记录当前定时事件链表中最大的定时器ID */
    maxId = eventLoop->timeEventNextId-1;
    while(te) {
        long now_sec, now_ms;
        long long id;

		/* 为什么会出现某个定时事件的id会大于maxId呢? 可能是因为ae中的所有操作都是非线程
		 * 安全的, 比如在maxId获取的同时执行了aeCreateTimeEvent操作, 此时我们忽略新增的事件
		 * 等到下一轮while检测处理(以避免可能出现的无限循环)
		 */
        if (te->id > maxId) {
            te = te->next;
            continue;
        }
		
		/* 获取系统当前时间的秒和毫秒部分 */
        aeGetTime(&now_sec, &now_ms);
		
		/* 如果当前时间的秒值已经大于定时事件的超时值的秒部分; 或者秒部分等于当前系统时间的秒部分‘
		 * 但系统的毫秒部分大于事件的毫秒部分; 则说明该事件已经超时, 可以处理该定时事件了
		 */
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
			
			/* 调用定时事件处理函数 */
            retval = te->timeProc(eventLoop, id, te->clientData);
			
			/* 本次已处理定时事件数增1 */
            processed++;
            /* After an event is processed our time event list may
             * no longer be the same, so we restart from head.
             * Still we make sure to don't process events registered
             * by event handlers itself in order to don't loop forever.
             * To do so we saved the max ID we want to handle.
             *
             * FUTURE OPTIMIZATIONS:
             * Note that this is NOT great algorithmically. Redis uses
             * a single time event so it's not a problem but the right
             * way to do this is to add the new elements on head, and
             * to flag deleted elements in a special way for later
             * deletion (putting references to the nodes to delete into
             * another linked list). */
			 
			/* 定时事件处理函数的返回值用于指示该定时事件是循环定时还是单次定时事件
			 * 如果返回AE_NOMORE, 说明是单次定时, 则此时可以删除该定时事件;
			 * 如果不是返回AE_NOMORE(此时返回的值代表下次超时的相对值), 说明是循环定时事件, 则修改该事件的超时时间值
			 */
            if (retval != AE_NOMORE) {
				/* 将retval值(相对时间值)加到当前系统绝对事件, 并转化为秒值和毫秒值 */
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            } else {
                aeDeleteTimeEvent(eventLoop, id);
            }
			
			/* 处理后链表可能发生了变化, 所以我们再次从链表头开始处理 */
            te = eventLoop->timeEventHead;
        } else {
            te = te->next;
        }
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
 
 /***
  * 事件循环处理函数
  * @eventLoop[IN]: 事件循环处理器
  * @flag[IN]: 事件处理标记
  *			   0-> 表示不做任何处理, 直接返回
  *			   AE_ALL_EVENTS-> 表示处理所有事件(IO事件和定时事件)
  *			   AE_FILE_EVENTS-> 表示需要处理IO事件
  * 		   AE_TIME_EVENTS-> 表示需要处理定时事件
  *			   AE_DONT_WAIT-> 表示如果没有事件能够处理, 则直接返回
  * @return: 返回本次调用处理的事件的总数(IO事件+定时事件)
  */
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    int processed = 0, numevents;

    /* 如果没有任何需要处理的事件, 则直接返回*/
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
	 
	/***
	 * 1)如果maxfd不为-1, 说明存在IO事件, 需要调用IO复用接口进行依次poll
	 * 2)或者如果需要处理定时事件并且没有AE_DONT_WAIT标记, 也可以调用IO复用接口(进行等待)
	 * IO复用接口都存在一个调用超时事件, 这个时间参数可以为我们提供一个原始的定时器, 我们可以将
	 * 最近将要超时的定时事件的时间值传入, 这样可以避免反复的Poll来轮询是否有定时事件超时
	 *
	 * 另外值得注意的是AE_DONT_WAIT标记:
	 * 这个标记的意义在于: 如果没有任何IO事件在无需等待的情况下就绪, 或者没有任何定时事件在此刻超时
	 * 则应保证aeProcessEvents立即返回
	 */
    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT)))
	{
        int j;
        aeTimeEvent *shortest = NULL;
        struct timeval tv, *tvp;

		/* 如果需要处理定时事件, 并且flag标记没有AE_DONT_WAIT标记, 则获取最近可能要超时的定时事件 */
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            shortest = aeSearchNearestTimer(eventLoop);
			
		/* 如果存在定时事件, 并且没有AE_DONT_WAIT标记, 计算最近的定时事件多长时间后超时,
		 * 通过tvp指示
		 */
        if (shortest) {
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            aeGetTime(&now_sec, &now_ms);
            tvp = &tv;
			
			/* 将事件的超时事件的秒值减去当前系统时间值的秒值 */
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
                tvp->tv_sec --;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
            }
			
			/* 正常情况下是不可能得到负值的, 除非系统时间发生调整, 此时直接将等待事件设置为0,
			 * 从而保证aeApiPoll能够立刻返回(以便与系统事件发生调整时, 所有定时事件得到尽快的处理)
			 */
            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        }
		else { 
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
			
			/* 如果存在AE_DONT_WAIT标记, 则将等待事件设置为0, 以便aeApiPoll能够立刻返回 */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* 如果不存在任何定时事件并且flag中不存在AE_DONT_WAIT标记, 则tvp=NULL, 
				 *以便aeApiPoll能够一直阻塞直到有任何IO事件就绪
				 */
                tvp = NULL; /* wait forever */
            }
        }

		/* 调用特定操作系统IO复用API, 通过前面可知, 如果不存在任何定时事件, 则tvp=NULL */
        numevents = aeApiPoll(eventLoop, tvp);
        for (j = 0; j < numevents; j++) {
		
			/* aeApiPoll会修改eventLoop中的fired数组, 将就绪的IO事件在该数组中标记 */
			
			/* 通过fired中的fd定位IO事件数组, 得到该事件对应的事件结构体 */
            aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
			
			/* 取出代表就绪事件类型的mask标记 */
            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int rfired = 0;

	    /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
			 
			/* 如果IO事件读就绪, 则调用其读处理函数 */
            if (fe->mask & mask & AE_READABLE) {
                rfired = 1;
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);
            }
			
			/* 如果IO事件的写就绪, 则调用其写处理函数 */
            if (fe->mask & mask & AE_WRITABLE) {	
				/* 前面初始化的时候, 我们注意到, 读和写就绪处理函数时同一个函数, 他们通过mask来区分读写 */
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
            }
            processed++;
        }
    }
	
    /* Check time events */
	/* 处理可能超时的定时事件 */
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

	/* 返回本次Poll操作总共就绪并得到处理的事件总数 */
    return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
 
/***
 * 等待某个IO事件的发生
 * @fd[IN]: 标识要等待的IO事件的文件描述符
 * @mask[IN]: 要等待的IO事件的类型掩码, 如读还是写
 * @milliseconds[IN]: 等待的超时事件(即使这段时间内事件任然未发生, 则函数返回)
 * @return: IO事件就绪的类型掩码
 */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

	/* 仅仅支持poll类型的IO复用机制 */
    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
	if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

/***
 * 事件处理循环主函数, 它循环地调用aeProcessEvents来处理事件, 直到退出事件循环
 * @eventLoop[IN]: 时间循环处理器
 */
void aeMain(aeEventLoop *eventLoop) {
    eventLoop->stop = 0;
    while (!eventLoop->stop) {
	
		/* 如果设置的事件处理函数执行前的挂钩函数, 则先调用该函数 */
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);
			
		/* 调用一次事件处理(处理所有类型事件) */
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}

/***
 * 获取当前使用的特定操作系统IO复用机制的名称, 如epoll机制则名称为"epoll"
 */
char *aeGetApiName(void) {
    return aeApiName();
}

/***
 * 设置每轮事件处理Poll执行前的挂钩函数
 * @eventLoop[IN]: 事件循环处理器
 * @beforesleep[IN]: 挂钩函数指针
 */
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}
