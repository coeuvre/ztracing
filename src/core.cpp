#include "core.h"

#include <string.h>

static char *memory_strdup(const char *str) {
    usize size = strlen(str);
    char *result = (char *)memory_alloc(size);
    if (result) {
        memcpy(result, str, size);
    }
    return result;
}
