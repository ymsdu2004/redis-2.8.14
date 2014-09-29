/* adlist.c - A generic doubly linked list implementation
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
 
/***
 * 创建一个新的链表(内部分配list结构体空间)
 * Note: 返回的list在不需要时候调用listRelease释放
 * @return: 成功则返回链表指针, 失败则返回NULL
 */
list *listCreate(void)
{
    struct list *list;

	/* 开辟链表结构体空间 */
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
		
	/* 头尾指针均初始化为NULL */
    list->head = list->tail = NULL;
	
	/* 初始链表长度肯定为0 */
    list->len = 0;
	
	/* 三个与数据相关的操作函数均设置为空, 如有必要用户可以在之后设置 */
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
	
    return list;
}
 
/***
 * 释放整个链表, 包括所有节点元素和list结构体本身(listCreate创建的)
 * @list[IN]: 有效链表指针
 */
void listRelease(list *list)
{
    unsigned long len;
    listNode *current, *next;

    current = list->head;
    len = list->len;
    while(len--) {
	
		/* 记录下一个节点 */
        next = current->next;
		
		/* 如果链表设置了数据析构函数, 则删除节点本身之前先调用析构函数析构数据 */
        if (list->free) list->free(current->value);
		
		/* 释放节点本身*/
        zfree(current);
		
		/* 当前节点移到下一个节点 */
        current = next;
    }
	
	/* 最后释放链表结构体本身 */
    zfree(list);
}

/***
 * 在链表头部插入新节点, 插入失败维持链表原来状态
 * @list[IN]: 有效链表指针
 * @value[IN]: 新增节点的数据值
 * @return: 成功则返回添加节点后的链表的指针, 失败则返回NULL
 */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;

	/* 分配节点空间, 失败直接返回NULL */
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
		
	/* 节点数据指针指向用户给出的数据 */
    node->value = value;
	
	/* 如果链表当前长度为0, 说明该添加的元素是链表的第一个元素 */
    if (list->len == 0) {
	
		/* 头尾指针都指向该新增的节点 */
        list->head = list->tail = node;
		
		/* 本节点的前一个节点和下一个节点的指针都为NULL */
        node->prev = node->next = NULL;
    } else { /* 如果新增节点不是链表的第一个节点 */
	
		/* 本节点的前一个元素节点设置为NULL */
        node->prev = NULL;
		
		/* 本节点的下一个节点指针设置为链表之前的头结点 */
        node->next = list->head;
		
		/* 链表之前的头结点的前一个节点指针指向新增节点 */
        list->head->prev = node;
		
		/* 新增节点现在作为链表的头节点了, 尾指针不用动, 
		 * 因为新增节点是插在表头的, 不会影响尾指针 
		 */
        list->head = node;
    }
	
	/* 成功插入后, 链表元素增1 */
    list->len++;
	
	/* 返回新增节点后的链表指针 */
    return list;
}
 
/***
 * 在链表尾部添加新节点, 失败保证维持链表原状
 * @list[IN]: 有效链表指针
 * @value[IN]: 新增节点的数据值
 * @return: 成功则返回添加节点后的链表的指针, 失败则返回NULL
 */
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

	/* 分配新增节点的空间, 失败则返回NULL */
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
		
	/* 用户数据赋值给节点的数据指针 */
    node->value = value;
	
	/* 如果链表当前长度为0, 说明链表为空, 此新增元素为第一个元素 */
    if (list->len == 0) {
	
		/* 链表头尾指针均指向该新增节点 */
        list->head = list->tail = node;
		
		/* 新增节点的前后节点指针均指向NULL */
        node->prev = node->next = NULL;
    } else	{ /* 如果链表当前不为空 */
	
		/* 新增节点的前指针指向当前链表尾指针所指的元素 */
        node->prev = list->tail;
		
		/* 新增节点的后指针为NULL */
        node->next = NULL;
		
		/* 链表的为指针指向的节点(即之前的最后一个节点)的后指针指向新增节点 */
        list->tail->next = node;
		
		/* 链表的尾指针现在指向新增的节点, 头指针不会影响, 因为新增节点是在尾部添加 */
        list->tail = node;
    }
	
	/* 成功插入后, 链表元素增1 */
    list->len++;
	
	/* 返回新增节点后的链表指针 */
    return list;
}

/***
 * 在指定节点之前或之后插入新节点
 * @list[IN]: 有效链表指针
 * @old_value[IN]: 插入参考节点
 * @value[IN]: 新增节点的数据值
 * @after[IN]: 指示在指定节点之前还是之后插入节点
 * @return: 成功则返回添加节点后的链表的指针, 失败则返回NULL
 */
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    listNode *node;

	/* 分配新节点空间 */
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
		
	/* 节点数据指针赋值 */
    node->value = value;
	
	/* 如果是在参考节点之后插入新节点 */
    if (after) {
	
		/* 新增节点的前节点指针指向参考节点 */
        node->prev = old_node;
		
		/* 新增节点的后节点指针指向参考节点当前的后节点 */
        node->next = old_node->next;
		
		/* 如果参考节点是尾节点, 则链表的为节点指针指向新增节点 */
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else { /* 如果是在参考节点之前插入新节点 */
	
		/* 新增节点的后节点指针指向参考节点 */
        node->next = old_node;
		
		/* 新增节点的前节点指针指向参考节点的前节点 */
        node->prev = old_node->prev;
		
		/* 如果参考节点就是头节点, 则链表的头指针变更为新增节点 */
        if (list->head == old_node) {
            list->head = node;
        }
    }
	
	/* 如果新增节点的前节点指针存在, 则将前节点的后节点指针指向新增节点 */
    if (node->prev != NULL) {
        node->prev->next = node;
    }
	
	/* 如果新增节点的后节点指针存在, 则将后节点的前节点指针指向新增节点 */
    if (node->next != NULL) {
        node->next->prev = node;
    }
	
	/* 成功插入后, 链表元素增1 */
    list->len++;
	
	/* 返回新增节点后的链表指针 */
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
 
/***
 * 从链表中删除指定的节点
 * @list[IN]: 有效链表指针
 * @node[IN]: 要删除的节点指针
 */
void listDelNode(list *list, listNode *node)
{
	/* 如果此节点有前节点, 则将其前节点的后节点指针指向此节点的后节点 */
    if (node->prev)
        node->prev->next = node->next;
    else /* 如果此节点没有前节点, 说明是删除表头节点, 则要将头指针指向此节点的后节点 */
        list->head = node->next;
    if (node->next) /* 如果此节点有后节点, 则将其后节点的前节点指针指向此节点的前节点 */
        node->next->prev = node->prev;
    else /* 如果此节点没有后节点, 则说明是删除表尾指针, 则要将尾指针指向此节点的前节点 */
        list->tail = node->prev;
		
	/* 如果此链表设置的节点数据析构函数, 则先析构节点数据 */
    if (list->free) list->free(node->value);
	
	/* 最后释放节点自身空间 */
    zfree(node);
	
	/* 链表长度减1 */
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail.
 */
 
/***
 * 创建链表给定方向的迭代器
 * @list[IN]: 有效链表指针
 * @direction[IN]: 迭代器的方向
 * @return: 成功则返回迭代器的指针, 失败则返回NULL
 */
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;

	/* 分配此迭代器空间 */
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
	
	/* 如果是Forward迭代器, 则迭代器的下一个元素指针指向头结点 */
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else /* 如果是Backward迭代器, 则迭代器的下一个元素指针指向尾节点 */
        iter->next = list->tail;
		
	/* 记录迭代器的方向, 以便迭代进行时确定next的走向 */
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */

/***
 * 释放迭代器, 仅仅是释放其自身的内存, 没有其他操作
 * @iter[IN]: 有效链表迭代器
 */
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */

/***
 * 重置迭代器为Forward迭代器(方向为Forward, 下一个节点初始为头结点)
 * @list[IN]: 有效链表指针
 * @li[IN]: 有效链表迭代器指针
 */
void listRewind(list *list, listIter *li) {
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

/***
 * 重置迭代器为Backward迭代器(方向为Backward, 下一个节点初始化为尾节点)
 * @list[IN]: 有效链表指针
 * @li[IN]: 有效链表迭代器指针
 */
void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */
 
/***
 * 链表迭代器获取下一个节点
 * @iter[IN]: 有效链表迭代器
 * @return: 如果还有下一个节点, 则返回下一个节点, 如果没有则返回NULL(迭代到链表的尾部了)
 */
listNode *listNext(listIter *iter)
{
	/* 获取迭代器的下一个节点 */
    listNode *current = iter->next;

	/* 如果下一个节点不为空 */
    if (current != NULL) {
	
		/* 方向为Forward, 则迭代器向前迭代一步 */
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else /* 方向为Backward, 则迭代器向后迭代一步 */
            iter->next = current->prev;
    }
	
	/* 返回当前迭代的节点或者NULL */
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
 
/***
 * 拷贝给定链表的一个副本, list结构本身和各个节点及数据都重新分配空间
 * @orig[IN]: 将要被拷贝的链表
 * @return[IN]: 成功则返回拷贝的新链表, 失败则返回NULL
 */
list *listDup(list *orig)
{
    list *copy;
    listIter *iter;
    listNode *node;

	/* 创建一个新的空链表 */
    if ((copy = listCreate()) == NULL)
        return NULL;
		
	/* 将原来链表的一些用户设置的数据操作方法复制给新建链表 */
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
	
	/* 创建原链表的迭代器 */
    iter = listGetIterator(orig, AL_START_HEAD);
	
	/* 迭代器遍历原链表的每个节点 */
    while((node = listNext(iter)) != NULL) {
        void *value;

		/* 如果链表设置了节点数据拷贝函数, 则调用该函数拷贝数据 */
        if (copy->dup) {
		
			/* 拷贝函数返回拷贝的数据的指针 */
            value = copy->dup(node->value);
			
			/* 如果拷贝失败, 则释放拷贝链表及原链表迭代器, 返回失败 */
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else /* 如果没有拷贝函数, 则直接赋值原链表节点的数据指针 */
            value = node->value;
			
		/* 将拷贝的节点添加到新链表的尾部, 如果失败则返回NULL */
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }
	
	/* 最后释放原链表迭代器 */
    listReleaseIterator(iter);
	
	/* 返回拷贝的新链表 */
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
 
/***
 * 查找给定关键字关联的节点(如果存在多个, 则返回第一个找到的)
 * @list[IN]: 有效链表指针
 * @key[IN]: 查找的关键字
 * @return: 如果找到匹配的节点, 则返回该节点指针, 否则返回NULL
 */
listNode *listSearchKey(list *list, void *key)
{
    listIter *iter;
    listNode *node;

	/* 创建链表迭代器 */
    iter = listGetIterator(list, AL_START_HEAD);
	
	/* 迭代器遍历链表 */
    while((node = listNext(iter)) != NULL) {
	
		/* 如果链表设置了节点数据匹配比较函数 */
        if (list->match) {
			/* 则调用匹配函数比较该节点的数据和给出的key是否匹配 */
            if (list->match(node->value, key)) {
			
				/* 如果匹配, 则找到了, 不必再查找, 直接返回该匹配的节点 */
                listReleaseIterator(iter);
                return node;
            }
        } else { /* 如果用户没有设置链表节点数据的匹配函数, 则直接比较数据指针 */
            if (key == node->value) {
                listReleaseIterator(iter);
                return node;
            }
        }
    }
	
	/* 最后没有找到则返回NULL */
    listReleaseIterator(iter);
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
 
/***
 * 获取链表中某个索引处对应的节点
 * 正向索引(>=0): 从表头开始向后计算, 表头节点索引为0, 往后依次增1即(0, 1, 2, ...)
 * 逆向索引(<0): 从表位开始向前计算, 表尾节点索引为-1, 往后依次减1, 即(-1, -2, -3, ...)
 * @list[IN]: 有效链表指针
 * @index[IN]: 链表节点索引
 * @return: 如果存在则返回对应的节点, 不存在则返回NULL
 */
listNode *listIndex(list *list, long index) {
    listNode *n;

	/* 如果索引是负值, 则说明是逆向索引 */
    if (index < 0) {
        index = (-index)-1;
		
		/* 从表尾开始向前计算 */
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else { /* 索引是正值或0, 则说明是正向索引 */
        n = list->head;
		
		/* 从表头开始向后计算 */
        while(index-- && n) n = n->next;
    }
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
/***
 * 旋转链表, 将链表的为节点旋转到链表的头部
 * @list[IN]: 有效链表指针
 */
void listRotate(list *list) {

	/* 保存当前尾节点的指针, 后面要用 */
    listNode *tail = list->tail;

	/* 如果链表的为空或者只有一个节点, 则不用处理 */
    if (listLength(list) <= 1) return;

    /* Detach current tail */
	/* 将链表的尾节点从链表尾部删除 */
	
	/* 链表的尾指针指向当前尾节点的上一个节点 */
    list->tail = tail->prev;
	
	/* 新的尾节点的后节点指针为NULL */
    list->tail->next = NULL;
	
    /* Move it as head */
	/* 再将该为节点插入到链表的头部 */
	
	/* 将链表头节点的前节点指针指向原来的尾节点 */
    list->head->prev = tail;
	
	/* 原来的尾节点的前节点指针为NULL */
    tail->prev = NULL;
	
	/* 原来的为节点的后节点指针指向原链表的头节点 */
    tail->next = list->head;
	
	/* 链表原来的为节点称为链表新的头结点 */
    list->head = tail;
}
