/**
 *!
 * \file        b_util_list.h
 * \version     v0.0.1
 * \date        2019/12/23
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_UTIL_LIST_H__
#define __B_UTIL_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "b_type.h"
/**
 * \addtogroup B_UTILS
 * \{
 */

/**
 * \addtogroup LIST
 * \{
 */

struct list_head
{
    struct list_head *next, *prev;
};

#define WRITE_ONCE(var, val) (var) = (val)

#define LIST_HEAD_INIT(name) \
    {                        \
        &(name), &(name)     \
    }

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr)  \
    do                       \
    {                        \
        (ptr)->next = (ptr); \
        (ptr)->prev = (ptr); \
    } while (0)

/**
 * list_entry - get the struct for this entry
 * @ptr: the &struct list_head pointer.
 * @type: the type of the struct this is embedded in.
 * @member: the name of the list_struct within the struct.
 */
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */

#define __list_add(_new, _prev, _next)     \
    do                                     \
    {                                      \
        (_next)->prev = (_new);            \
        (_new)->next  = (_next);           \
        (_new)->prev  = (_prev);           \
        WRITE_ONCE((_prev)->next, (_new)); \
    } while (0)

/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
#define list_add(new, head) __list_add((new), (head), (head)->next)

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */

#define list_add_tail(new, head) __list_add((new), (head)->prev, (head))

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */

#define __list_del(_prev, _next)            \
    do                                      \
    {                                       \
        (_next)->prev = (_prev);            \
        WRITE_ONCE((_prev)->next, (_next)); \
    } while (0)

/**
 * list_is_first -- tests whether @list is the first entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */

#define list_is_first(list, head) ((list)->prev == (head))

/**
 * list_is_last - tests whether @list is the last entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */

#define list_is_last(list, head) ((list)->next == (head))

/**
 * list_is_head - tests whether @list is the list @head
 * @list: the entry to test
 * @head: the head of the list
 */

#define list_is_head(list, head) ((list) == (head))

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */

#define list_empty(head) ((head)->next == (head))

/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
    for (pos = (head)->next; !list_is_head(pos, (head)); pos = pos->next)

/**
 * list_for_each_continue - continue iteration over a list
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 *
 * Continue to iterate over a list, continuing after the current position.
 */
#define list_for_each_continue(pos, head) \
    for (pos = pos->next; !list_is_head(pos, (head)); pos = pos->next)

/**
 * list_for_each_prev	-	iterate over a list backwards
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define list_for_each_prev(pos, head) \
    for (pos = (head)->prev; !list_is_head(pos, (head)); pos = pos->prev)

/**
 * list_for_each_safe - iterate over a list safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop cursor.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; !list_is_head(pos, (head)); pos = n, n = pos->next)

/**
 * list_for_each_prev_safe - iterate over a list backwards safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop cursor.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_prev_safe(pos, n, head) \
    for (pos = (head)->prev, n = pos->prev; !list_is_head(pos, (head)); pos = n, n = pos->prev)

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif
/************************ Copyright (c) 2019 Bean *****END OF FILE****/
