#ifndef __STREAM_H__
#define __STREAM_H__

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "header.h"

#define MAX_BUFFER 100000
#define MAX_METADATA_LENGTH 5000

typedef struct {
    size_t in_buffer;
    char buffer[MAX_BUFFER];

    int socket;
    FILE *output_file;

    header_t header;

    unsigned int current_interval;
    char title[MAX_METADATA_LENGTH];
} stream_t;

void stream_init(stream_t *stream, int sock, FILE* file);
int send_stream_request(const stream_t *stream, const char* path);

#endif
