#ifndef LIST_H
#define LIST_H

/*
 * Intrusive circular doubly-linked list, modelled after the Linux kernel's
 * list_head.  The list node is embedded directly inside the owner struct,
 * which gives O(1) insertion, removal, and – crucially for the buddy system –
 * O(1) lookup of the containing struct via list_entry / container_of.
 */

struct list_head {
    struct list_head *prev;
    struct list_head *next;
};

/* Initialise a list head so it points to itself (empty list). */
static inline void list_init(struct list_head *h) {
    h->prev = h;
    h->next = h;
}

static inline int list_empty(const struct list_head *h) { return h->next == h; }

/* Internal helper: splice node between prev and next. */
static inline void __list_add(struct list_head *node, struct list_head *prev,
                              struct list_head *next) {
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

/* Insert node at the tail of head's list (i.e. just before head). */
static inline void list_add_tail(struct list_head *node,
                                 struct list_head *head) {
    __list_add(node, head->prev, head);
}

/* Insert node at the head of head's list (i.e. just after head). */
static inline void list_add(struct list_head *node, struct list_head *head) {
    __list_add(node, head, head->next);
}

/* Remove an entry from its list and make it self-referential (safe). */
static inline void list_del(struct list_head *entry) {
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry;
    entry->prev = entry;
}

/*
 * container_of – given a pointer to a member, obtain the enclosing struct.
 * ptr    : pointer to the struct list_head member
 * type   : type of the enclosing struct
 * member : name of the list_head field inside that struct
 */
#define container_of(ptr, type, member)                                        \
    ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

/* Obtain a pointer to the enclosing struct from a list_head pointer. */
#define list_entry(ptr, type, member) container_of(ptr, type, member)

/* First entry (non-empty list assumed). */
#define list_first_entry(head, type, member)                                   \
    list_entry((head)->next, type, member)

/*
 * Safe list iteration that tolerates deletion of the current entry.
 * pos  : loop cursor (struct list_head *)
 * tmp  : temporary storage for next pointer
 * head : the list head
 */
#define list_for_each_safe(pos, tmp, head)                                     \
    for ((pos) = (head)->next, (tmp) = (pos)->next; (pos) != (head);           \
         (pos) = (tmp), (tmp) = (pos)->next)

#endif /* LIST_H */
