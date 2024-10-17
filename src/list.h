#ifndef ZTRACING_SRC_LIST_H_
#define ZTRACING_SRC_LIST_H_

#define APPEND_DOUBLY_LINKED_LIST(first, last, node, prev, next) \
  do {                                                           \
    if (last) {                                                  \
      (node)->prev = (last);                                     \
      (node)->next = 0;                                          \
      (last)->next = (node);                                     \
      (last) = (node);                                           \
    } else {                                                     \
      (first) = (last) = (node);                                 \
      (node)->prev = (node)->next = 0;                           \
    }                                                            \
  } while (0)

#define PREPEND_DOUBLY_LINKED_LIST(first, last, node, prev, next) \
  do {                                                            \
    if (first) {                                                  \
      (node)->prev = 0;                                           \
      (node)->next = (first);                                     \
      (first)->prev = (node);                                     \
      (first) = (node);                                           \
    } else {                                                      \
      (first) = (last) = (node);                                  \
      (node)->prev = (node)->next = 0;                            \
    }                                                             \
  } while (0)

// Insert node after node `after`. If `after` is NULL, append to the end of the
// list.
#define INSERT_DOUBLY_LINKED_LIST(first, last, after, node, prev, next) \
  do {                                                                  \
    if (after && last != after) {                                       \
      (node)->prev = (after);                                           \
      (node)->next = (after)->next;                                     \
      (node)->next->prev = (node);                                      \
      (node)->prev->next = (node);                                      \
    } else {                                                            \
      APPEND_DOUBLY_LINKED_LIST(first, last, node, prev, next);         \
    }                                                                   \
  } while (0)

#define REMOVE_DOUBLY_LINKED_LIST(first, last, node, prev, next) \
  do {                                                           \
    if ((first) == (node)) {                                     \
      (first) = (node)->next;                                    \
    }                                                            \
    if ((last) == (node)) {                                      \
      (last) = (node)->prev;                                     \
    }                                                            \
    if ((node)->next) {                                          \
      (node)->next->prev = (node)->prev;                         \
    }                                                            \
    if ((node)->prev) {                                          \
      (node)->prev->next = (node)->next;                         \
    }                                                            \
    (node)->prev = (node)->next = 0;                             \
  } while (0)

#endif  // ZTRACING_SRC_LIST_H_
