#ifndef __MISC_H__
#define __MISC_H__

#include <stdbool.h>

#include "stream.h"


#define debug_print(fmt, ...) \
        do { if (DEBUG) { fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); fflush(stderr); }} while (0)

#define DEBUG 0

/**
    Changes string to bool.

    @param b: Pointer to bool to which we want to save value.
    @param str: Pointer to string from which we will be reading value.
    @returns: 0 if operation was successful, -1 otherwise.
    **/
int strtob(bool* b, const char* str);

/**
    Make poll with 5 sec timeout and recv on `socket`.

    @param socket: Socket on which we want to make poll and recv.
    @param buffer: Pointer to buffer to which we will be saving data.
    @param bytes: Number of bytes we want to maximally read.
    @returns: -1 if there was error, 0 if everything was good, bytes received otherwise.
    **/
bool PollRecv(int socket, std::string& buffer, unsigned int timeout);

uint16_t ParsePort(std::string port);


#endif
