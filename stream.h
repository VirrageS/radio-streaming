#ifndef __STREAM_H__
#define __STREAM_H__

#define MAX_BUFFER 30000

typedef struct {
    size_t in_buffer;
    char buffer[MAX_BUFFER];

    int stream_socket;
    FILE *output_file;
} stream_t;

#endif
