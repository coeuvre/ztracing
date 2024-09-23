#pragma once

#define append_doubly_linked_list(first, last, node, prev, next)               \
    do {                                                                       \
        if (last) {                                                            \
            last->next = node;                                                 \
            node->prev = last;                                                 \
            node->next = 0;                                                    \
        } else {                                                               \
            first = last = node;                                               \
            node->prev = node->next = 0;                                       \
        }                                                                      \
    } while (0)
