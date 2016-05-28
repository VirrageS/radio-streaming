#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>

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

int remove_from_buffer(stream_t *stream, size_t bytes_count)
{
    stream->in_buffer -= bytes_count;
    memmove(&stream->buffer[0], &stream->buffer[bytes_count], stream->in_buffer);
    return 0;
}

int write_to_file(stream_t *stream, size_t bytes_count)
{
    if (stream->stream_on) {
        fwrite(&stream->buffer[0], sizeof(char), (size_t)bytes_count, stream->output_file);
        fflush(stream->output_file);
    }

    remove_from_buffer(stream, bytes_count);
    return 0;
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
