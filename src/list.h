#ifndef ZTRACING_SRC_LIST_H_
#define ZTRACING_SRC_LIST_H_

#define APPEND_DOUBLY_LINKED_LIST(first, last, node, prev, next) \
  do {                                                           \
    if (last) {                                                  \
      (last)->next = (node);                                     \
      (node)->prev = (last);                                     \
      (node)->next = 0;                                          \
      (last) = (node);                                           \
    } else {                                                     \
      (first) = (last) = (node);                                 \
      (node)->prev = (node)->next = 0;                           \
    }                                                            \
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
