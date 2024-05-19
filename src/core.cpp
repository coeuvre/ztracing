#include "core.h"

#include <string.h>

static char *MemStrDup(const char *str) {
    usize size = strlen(str);
    char *result = (char *)MemAlloc(size);
    if (result) {
        memcpy(result, str, size);
    }
    return result;
}
