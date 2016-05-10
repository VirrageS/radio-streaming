#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "parser.h"
#include "err.h"
#include "misc.h"
#include "header.h"

int parse_header(stream_t *stream)
{
    debug_print("%s\n", "parsing header...");
    while (true) {
        debug_print("socket: %d; buffer_size: %zu\n", stream->socket, sizeof(stream->buffer) - stream->in_buffer);
        ssize_t bytes_received = recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer, 0);
        if (bytes_received < 0) {
            syserr("recv() failed");
        } else if (bytes_received == 0) {
            // syserr("recv(): connection closed");
            debug_print("%s\n", "not recieving anything (header)...");
        } else {
            int parse_point = -1;
            stream->in_buffer += bytes_received;

            for (size_t i = 3; i < stream->in_buffer; ++i) {
                if (stream->buffer[i-3] == '\r' && stream->buffer[i-2] == '\n' && stream->buffer[i-1] == '\r' && stream->buffer[i] == '\n') {
                    parse_point = i + 1;
                }
            }

            debug_print("%s\n", "got some header...");

            if (parse_point >= 0) {
                debug_print("%s\n", "parsing headers...");
                extract_header_fields(&stream->header, stream->buffer);

                if (stream->header.metaint == 0) {
                    syserr("Could not find metaint information\n");
                }

                stream->in_buffer -= parse_point;
                memmove(&stream->buffer[0], &stream->buffer[parse_point], stream->in_buffer);
                break;
            }
        }
    }

    return 0;
}

int check_metadata(stream_t *stream)
{
    if (stream->current_interval >= stream->in_buffer) {
        stream->current_interval -= stream->in_buffer;
        return 0;
    }

    // write until header
    fwrite(&stream->buffer[0], sizeof(char), (size_t)stream->current_interval, stream->output_file);
    fflush(stream->output_file);

    stream->in_buffer -= stream->current_interval;
    memmove(&stream->buffer[0], &stream->buffer[stream->current_interval], stream->in_buffer);

    unsigned int metadata_length = abs((int)stream->buffer[0]) * 16;

    // remove metadata length
    stream->in_buffer -= 1;
    memmove(&stream->buffer[0], &stream->buffer[1], stream->in_buffer);

    if (metadata_length == 0) {
        stream->current_interval = stream->header.metaint - stream->in_buffer;
        return 0;
    }

    while (metadata_length > stream->in_buffer) {
        ssize_t bytes_received = recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer, 0);
        if (bytes_received < 0) {
            syserr("recv() failed");
        } else if (bytes_received == 0) {
            debug_print("%s\n", "not recieving anything (metadata)...");
        } else {
            stream->in_buffer += bytes_received;
        }
    }

    char metadata_content[MAX_METADATA_LENGTH];
    strncpy(metadata_content, stream->buffer, metadata_length);
    get_metadata_field(metadata_content, "StreamTitle", stream->title);

    debug_print("title: \"%s\"\n", stream->title);
    debug_print("metadata_length: %d\n", metadata_length);

    stream->in_buffer -= metadata_length;
    memmove(&stream->buffer[0], &stream->buffer[metadata_length], stream->in_buffer);

    stream->current_interval = stream->header.metaint - stream->in_buffer;

    return 0;
}

int parse_data(stream_t *stream)
{
    ssize_t bytes_received = recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer, 0);
    if (bytes_received < 0) {
        syserr("recv() failed");
    } else if (bytes_received == 0) {
        debug_print("%s\n", "not recieving anything (data)...");
    } else {
        // TODO: checking for particular data
        // check if meta data header
        // check if mp3 data


        stream->in_buffer += bytes_received;
        // debug_print("in_buffer: %d\n", stream->in_buffer);
        check_metadata(stream);

        fwrite(&stream->buffer[0], sizeof(char), stream->in_buffer, stream->output_file);
        fflush(stream->output_file);
        // debug_print("%s\n", "printing");
        stream->in_buffer = 0;
        memset(&stream->buffer, 0, sizeof(stream->buffer));
    }

    return 0;
}
