#ifndef ZTRACING_SRC_LIST_H_
#define ZTRACING_SRC_LIST_H_

#define DLL_APPEND(first, last, node, prev, next) \
  do {                                            \
    if (last) {                                   \
      (node)->prev = (last);                      \
      (node)->next = 0;                           \
      (last)->next = (node);                      \
      (last) = (node);                            \
    } else {                                      \
      (first) = (last) = (node);                  \
      (node)->prev = (node)->next = 0;            \
    }                                             \
  } while (0)

#define DLL_PREPEND(first, last, node, prev, next) \
  do {                                             \
    if (first) {                                   \
      (node)->prev = 0;                            \
      (node)->next = (first);                      \
      (first)->prev = (node);                      \
      (first) = (node);                            \
    } else {                                       \
      (first) = (last) = (node);                   \
      (node)->prev = (node)->next = 0;             \
    }                                              \
  } while (0)

// Insert node after node `after`. If `after` is NULL, append to the end of the
// list.
#define DLL_INSERT(first, last, after, node, prev, next) \
  do {                                                   \
    if (after && last != after) {                        \
      (node)->prev = (after);                            \
      (node)->next = (after)->next;                      \
      (node)->next->prev = (node);                       \
      (node)->prev->next = (node);                       \
    } else {                                             \
      DLL_APPEND(first, last, node, prev, next);         \
    }                                                    \
  } while (0)

#define DLL_REMOVE(first, last, node, prev, next) \
  do {                                            \
    if ((first) == (node)) {                      \
      (first) = (node)->next;                     \
    }                                             \
    if ((last) == (node)) {                       \
      (last) = (node)->prev;                      \
    }                                             \
    if ((node)->next) {                           \
      (node)->next->prev = (node)->prev;          \
    }                                             \
    if ((node)->prev) {                           \
      (node)->prev->next = (node)->next;          \
    }                                             \
    (node)->prev = (node)->next = 0;              \
  } while (0)

#define DLL_CONCAT(first1, last1, first2, last2, prev, next) \
  do {                                                       \
    if (last1) {                                             \
      (last1)->next = first2;                                \
      (last1) = last2;                                       \
    } else {                                                 \
      first1 = first2;                                       \
      last1 = last2;                                         \
    }                                                        \
  } while (0)

#endif  // ZTRACING_SRC_LIST_H_
