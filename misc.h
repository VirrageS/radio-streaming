#ifndef __MISC_H__
#define __MISC_H__

#include <stdbool.h>

#define debug_print(fmt, ...) \
        do { if (DEBUG) { fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); fflush(stderr); }} while (0)

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define DEBUG 1

int strtob(bool* b, const char* str);

#endif
