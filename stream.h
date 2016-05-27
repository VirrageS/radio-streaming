#ifndef __STREAM_H__
#define __STREAM_H__

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "header.h"

#define MAX_BUFFER 100000
#define MAX_METADATA_LENGTH 5000

typedef struct {
    size_t in_buffer; // stores how many data is in buffer
    char buffer[MAX_BUFFER]; // buffer in which we store all the data

    bool meta_data; // should we parse meta data or not

    int socket; // socket on which we listen to ICY-Data
    FILE *output_file; // file to which we should write our data

    header_t header; // paresed ICY-Header

    unsigned int current_interval; // stores length of data to next ICY-MetaData header
    char title[MAX_METADATA_LENGTH];

    volatile bool stream_on; // check if stream is "playing" or "paused"
} stream_t;

/**
    Initialize stream.

    @param stream: Pointer to stream which we want to initialize.
    @param file: Pointer to file to which we write all mp3 data.
    @param meta_data: Value which TODO
    **/
void stream_init(stream_t *stream, FILE* file, bool meta_data);

/**
    Sends request for listening ICY stream.

    @param stream: Stream on which we want to listen.
    @param path: Path on which are resources on http.
    **/
int send_stream_request(const stream_t *stream, const char* path);


int set_stream_socket(stream_t *stream, const char* host, const char* port);

#endif
