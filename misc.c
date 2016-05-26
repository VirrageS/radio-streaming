#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "misc.h"
#include "err.h"

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
