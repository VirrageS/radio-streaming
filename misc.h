#ifndef __MISC_H__
#define __MISC_H__

#include <stdbool.h>

#include "stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define debug_print(fmt, ...) \
        do { if (DEBUG) { fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); fflush(stderr); }} while (0)

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define DEBUG 1

int strtob(bool* b, const char* str);
int write_to_file(stream_t *stream, size_t bytes_count);
int remove_from_buffer(stream_t *stream, size_t bytes_count);


#ifdef __cplusplus
}
#endif

#endif
