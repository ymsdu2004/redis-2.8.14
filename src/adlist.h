/* adlist.h - A generic doubly linked list implementation
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

#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */

/***
 * 链表节点结构体
 */
typedef struct listNode {
    struct listNode *prev;		/* 前一个节点指针 */
    struct listNode *next;		/* 下一个节点指针 */
    void *value;				/* 本节点数据(可以存储一个int数据或者其他数据的指针) */
} listNode;

/***
 * 链表迭代器结构体
 */
typedef struct listIter {
    listNode *next;		/* 下一个节点指针 */
    int direction;		/* 迭代器的迭代方向 */
} listIter;

/***
 * 链表结构体
 */
typedef struct list {
    listNode *head;		/* 链表头指针 */
    listNode *tail;		/* 链表尾指针 */
    void *(*dup)(void *ptr);	/* 节点数据的复制方法 */
    void (*free)(void *ptr);	/* 节点数据的释放方法 */
    int (*match)(void *ptr, void *key);	/* 节点数据的匹配方法 */
    unsigned long len;		/* 链表当前长度(即元素个数) */
} list;

/* Functions implemented as macros */

/* 获取链表当前的长度 */
#define listLength(l) ((l)->len)

/* 获取链表的头指针 */
#define listFirst(l) ((l)->head)

/* 获取链表的尾指针 */
#define listLast(l) ((l)->tail)

/* 获取当前节点的前一个节点 */
#define listPrevNode(n) ((n)->prev)

/* 获取当前节点的下一个节点 */
#define listNextNode(n) ((n)->next)

/* 获取当前节点的值 */
#define listNodeValue(n) ((n)->value)

/* 设置链表的数据拷贝函数 */
#define listSetDupMethod(l,m) ((l)->dup = (m))

/* 设置链表的数据析构函数 */
#define listSetFreeMethod(l,m) ((l)->free = (m))

/* 设置链表的数据比较函数 */
#define listSetMatchMethod(l,m) ((l)->match = (m))

/* 获取链表的数据拷贝函数 */
#define listGetDupMethod(l) ((l)->dup)

/* 获取链表的数据析构函数 */
#define listGetFree(l) ((l)->free)

/* 获取链表的数据比较函数 */
#define listGetMatchMethod(l) ((l)->match)

/* Prototypes */

/***
 * 创建一个新的链表(内部分配list结构体空间)
 * Note: 返回的list在不需要时候调用listRelease释放
 * @return: 成功则返回链表指针, 失败则返回NULL
 */
list *listCreate(void);

/***
 * 释放整个链表, 包括所有节点元素和list结构体本身(listCreate创建的)
 * @list[IN]: 有效链表指针
 */
void listRelease(list *list);

/***
 * 在链表头部插入新节点, 插入失败维持链表原来状态
 * @list[IN]: 有效链表指针
 * @value[IN]: 新增节点的数据值
 * @return: 成功则返回添加节点后的链表的指针, 失败则返回NULL
 */
list *listAddNodeHead(list *list, void *value);

/***
 * 在链表尾部添加新节点, 失败保证维持链表原状
 * @list[IN]: 有效链表指针
 * @value[IN]: 新增节点的数据值
 * @return: 成功则返回添加节点后的链表的指针, 失败则返回NULL
 */
list *listAddNodeTail(list *list, void *value);

/***
 * 在指定节点之前或之后插入新节点
 * @list[IN]: 有效链表指针
 * @old_value[IN]: 插入参考节点
 * @value[IN]: 新增节点的数据值
 * @after[IN]: 指示在指定节点之前还是之后插入节点
 * @return: 成功则返回添加节点后的链表的指针, 失败则返回NULL
 */
list *listInsertNode(list *list, listNode *old_node, void *value, int after);

/***
 * 从链表中删除指定的节点
 * @list[IN]: 有效链表指针
 * @node[IN]: 要删除的节点指针
 */
void listDelNode(list *list, listNode *node);

/***
 * 创建链表给定方向的迭代器
 * @list[IN]: 有效链表指针
 * @direction[IN]: 迭代器的方向
 * @return: 成功则返回迭代器的指针, 失败则返回NULL
 */
listIter *listGetIterator(list *list, int direction);

/***
 * 链表迭代器获取下一个节点
 * @iter[IN]: 有效链表迭代器
 * @return: 如果还有下一个节点, 则返回下一个节点, 如果没有则返回NULL(迭代到链表的尾部了)
 */
listNode *listNext(listIter *iter);

/***
 * 释放迭代器, 仅仅是释放其自身的内存, 没有其他操作
 * @iter[IN]: 有效链表迭代器
 */
void listReleaseIterator(listIter *iter);

/***
 * 拷贝给定链表的一个副本, list结构本身和各个节点及数据都重新分配空间
 * @orig[IN]: 将要被拷贝的链表
 * @return[IN]: 成功则返回拷贝的新链表, 失败则返回NULL
 */
list *listDup(list *orig);

/***
 * 查找给定关键字关联的节点(如果存在多个, 则返回第一个找到的)
 * @list[IN]: 有效链表指针
 * @key[IN]: 查找的关键字
 * @return: 如果找到匹配的节点, 则返回该节点指针, 否则返回NULL
 */
listNode *listSearchKey(list *list, void *key);

/***
 * 获取链表中某个索引处对应的节点
 * 正向索引(>=0): 从表头开始向后计算, 表头节点索引为0, 往后依次增1即(0, 1, 2, ...)
 * 逆向索引(<0): 从表位开始向前计算, 表尾节点索引为-1, 往后依次减1, 即(-1, -2, -3, ...)
 * @list[IN]: 有效链表指针
 * @index[IN]: 链表节点索引
 * @return: 如果存在则返回对应的节点, 不存在则返回NULL
 */
listNode *listIndex(list *list, long index);

/***
 * 重置迭代器为Forward迭代器(方向为Forward, 下一个节点初始为头结点)
 * @list[IN]: 有效链表指针
 * @li[IN]: 有效链表迭代器指针
 */
void listRewind(list *list, listIter *li);

/***
 * 重置迭代器为Backward迭代器(方向为Backward, 下一个节点初始化为尾节点)
 * @list[IN]: 有效链表指针
 * @li[IN]: 有效链表迭代器指针
 */
void listRewindTail(list *list, listIter *li);

/***
 * 旋转链表, 将链表的为节点旋转到链表的头部
 * @list[IN]: 有效链表指针
 */
void listRotate(list *list);

/* Directions for iterators */
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */
