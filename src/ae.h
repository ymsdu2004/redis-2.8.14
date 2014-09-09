/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __AE_H__
#define __AE_H__

#define AE_OK 0
#define AE_ERR -1

#define AE_NONE 0

/* IO事件类型: 读 */
#define AE_READABLE 1

/* IO事件类型: 写 */
#define AE_WRITABLE 2

/* 事件类型 */

/* IO类型事件 */
#define AE_FILE_EVENTS 1

/* 定时事件 */
#define AE_TIME_EVENTS 2

/* 所有类型事件, 即IO事件和定时事件 */
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4

/***
 * 决定时间事件是否要持续执行的 flag
 */
#define AE_NOMORE -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct aeEventLoop;

/***
 * IO事件就绪处理函数
 * @eventLoop[IN]: eventloop(reactor组件)
 * @fd[IN]: 就绪IO事件的文件描述符
 * @clientData[IN]: 用户注册事件时传入的私有数据
 * @mask[IN]: 就绪的IO事件掩码->AE_(READABLE|WRITABLE)
 */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);

/***
 * 定时事件就绪处理函数
 * @eventLoop[IN]: eventloop(reactor组件)
 * @id[IN]: 定时器ID
 * @clientData[IN]: 用户注册事件时传入的私有数据
 * @return:
 */
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);

/***
 * 定时器删除时调用的函数
 * @eventLoop[IN]: 时间循环处理器
 * @clientData[IN]: 用户私有数据
 */
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);

/***
 * 设置每轮事件处理Poll执行前的挂钩函数, 也就是每次调用aeProcessEvents前需要执行的函数
 * @eventLoop[IN]: 事件循环处理器
 */
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/***
 * I/O类型事件结构体
 */
typedef struct aeFileEvent {

	/* 关注的IO事件类型 */
    int mask; /* one of AE_(READABLE|WRITABLE) */
	
	/* IO事件读就绪处理函数 */
    aeFileProc *rfileProc;
	
	/* IO事件写就绪处理函数*/
    aeFileProc *wfileProc;
	
	/* 用户私有数据, IO就绪回调函数调用时回传给用户 */
    void *clientData;
} aeFileEvent;

/***
 * 定时事件结构体
 */
typedef struct aeTimeEvent {

	/* 定时器ID, 定时器全局唯一的标识 */
    long long id;
	
	/* 定时时间的秒数 */
    long when_sec;
	
	/* 定时时间的毫秒数 */
    long when_ms;
	
	/* 定时事件就绪处理函数 */
    aeTimeProc *timeProc;
	
	/* 定时器删除时调用的函数 */
    aeEventFinalizerProc *finalizerProc;
    
	/* 用户私有数据, 定时事件就绪回调函数调用时回传给用户 */
	void *clientData;
	
	/* 下一个定时器结构体, 所有的定时事件在一个链表中, 有该指针串联, 这里可以有改进的空间 */
    struct aeTimeEvent *next;
} aeTimeEvent;

/*
 * 表示一个就绪的IO事件的结构体
 * 这个结构体的填充在特定操作系统的aePollApi中进行, 它将就绪的IO事件的fd和mask依次填写在
 * fired数组中, 通常在aePollApi返回后, 遍历fired数组
 */
typedef struct aeFiredEvent {

	/* 就绪事件的文件描述符 */
    int fd;
	
	/* 就绪事件类型掩码, 可以是 AE_READABLE 或 AE_WRITABLE */
    int mask;
} aeFiredEvent;

/* State of an event based program */
typedef struct aeEventLoop {

	/* 当前已经注册的最大的文件描述符值 */
    int maxfd;
	
	/* 添加的描述符值只能小于这个值 */
    int setsize;
	
	/* 用于生成下一个时间事件ID */
    long long timeEventNextId;
	
	/* 最后一次执行时间事件的时间, 防止出现系统时间变化导致未来时间 */
    time_t lastTime;
	
	/* 已注册的IO事件, 一个setsize大小的数组 */
    aeFileEvent *events;
	
	/* 已就绪的IO事件, 一个setsize大小的数组 */
    aeFiredEvent *fired;
	
	/* 已注册的时间事件, 这是一个链表, 每次插入新的节点都插到表头 */
    aeTimeEvent *timeEventHead;
	
	/* 事件处理器停止开关 */
    int stop;
    void *apidata; /* This is used for polling API specific data */
	
	/* 在处理事件前要执行的操作*/
    aeBeforeSleepProc *beforesleep;
} aeEventLoop;

/***
 * 创建事件处理循环
 */
aeEventLoop *aeCreateEventLoop(int setsize);

/***
 * 删除事件处理循环
 */
void aeDeleteEventLoop(aeEventLoop *eventLoop);

/***
 * 停止事件处理循环
 */
void aeStop(aeEventLoop *eventLoop);

/***
 * 创建IO事件, 并添加到事件处理循环中
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask, aeFileProc *proc, void *clientData);

/***
 * 删除IO事件
 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);

/***
 * 返回文件描述符fd在事件循环中监听的事件类型, 即返回fd对应事件的mask
 */
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);

/***
 * 创建定时事件
 */
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds, aeTimeProc *proc, void *clientData, aeEventFinalizerProc *finalizerProc);

/***
 * 删除定时器事件
 */
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);

/***
 * 尝试进行一次事件处理
 */
int aeProcessEvents(aeEventLoop *eventLoop, int flags);

/***
 * 等待某个事件发生
 */
int aeWait(int fd, int mask, long long milliseconds);

/***
 * 开始执行事件处理循环
 */
void aeMain(aeEventLoop *eventLoop);

/***
 * 获取当前使用的事件处理机制类型: select, poll, epoll, kqueue等
 */
char *aeGetApiName(void);


void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
int aeGetSetSize(aeEventLoop *eventLoop);
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

#endif
