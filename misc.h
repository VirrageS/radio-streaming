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
#define DEBUG 0

int strtob(bool* b, const char* str);
ssize_t poll_recv(int socket, char* buffer, size_t bytes);

/**
    Wypisuje informację o błędnym zakończeniu funkcji systemowej
    i kończy działanie programu.
    **/
extern void syserr(const char *fmt, ...);

/**
    Wypisuje informację o błędzie i kończy działanie programu.
    **/
extern void fatal(const char *fmt, ...);

void clean_all();

#ifdef __cplusplus
}
#endif

#endif
