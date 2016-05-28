#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>

#include "misc.h"


int strtob(bool* b, const char* str)
{
    if (strcmp(str, "yes") == 0) {
        *b = true;
        return 0;
    } else if (strcmp(str, "no") == 0) {
        *b = false;
        return 0;
    }

    return 1;
}


ssize_t poll_recv(int socket, char* buffer, size_t bytes)
{
    ssize_t bytes_received = -1;

    struct pollfd poll_socket[1];
    poll_socket[0].fd = socket;
    poll_socket[0].events = POLLIN | POLLHUP;

    int err = poll(poll_socket, 1, 5000);
    if (err <= 0)
        return -1;
    else {
        bytes_received = recv(socket, buffer, bytes, 0);
    }

    return bytes_received;
}

void syserr(const char *fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");
    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);
    fprintf(stderr, " (%d; %s)\n", errno, strerror(errno));

    clean_all();

    exit(EXIT_FAILURE);
}

void fatal(const char *fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");
    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);
    fprintf(stderr, "\n");

    clean_all();

    exit(EXIT_FAILURE);
}
