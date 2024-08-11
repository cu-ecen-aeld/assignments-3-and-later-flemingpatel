//
// Created by Fleming on 2024-08-10.
//

#ifndef CTWISTED_LIST_H
#define CTWISTED_LIST_H

struct list_head {
    struct list_head *next, *prev;
};

/* Initialize a new list head */
#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
    struct list_head name = LIST_HEAD_INIT(name)

/* Initialize the list head */
static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

/* Insert a new entry between two known consecutive entries. */
static inline void __list_add(struct list_head *new_entry,
                              struct list_head *prev,
                              struct list_head *next) {
    next->prev = new_entry;
    new_entry->next = next;
    new_entry->prev = prev;
    prev->next = new_entry;
}

/* Add a new entry */
static inline void list_add(struct list_head *new_entry, struct list_head *head) {
    __list_add(new_entry, head, head->next);
}

/* Add a new entry to the tail */
static inline void list_add_tail(struct list_head *new_entry, struct list_head *head) {
    __list_add(new_entry, head->prev, head);
}

/* Delete a list entry by making the prev/next entries point to each other */
static inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

/* Deletes entry from list and reinitializes it */
static inline void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

/* Check whether the list is empty */
static inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

/* Get the struct for this entry */
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/* Iterate over a list */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/* Iterate over a list safe against removal of list entry */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

#endif //CTWISTED_LIST_H
