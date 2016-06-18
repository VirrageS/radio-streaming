#include <vector>

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


ssize_t PollRecv(int socket, std::string& buffer, unsigned int timeout)
{
    struct pollfd poll_socket[1];
    poll_socket[0].fd = socket;
    poll_socket[0].events = POLLIN;

    int err = poll(poll_socket, 1, timeout);
    if (err <= 0) {
        return -1;
    }

    std::vector<char> tmp_buffer(10000);

    ssize_t bytes_recieved = recv(socket, tmp_buffer.data(), tmp_buffer.size(), 0);
    if (bytes_recieved <= 0)
        return bytes_recieved;

    tmp_buffer.resize(bytes_recieved);
    buffer.append(tmp_buffer.cbegin(), tmp_buffer.cend());
    return bytes_recieved;
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
