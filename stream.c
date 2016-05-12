#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "stream.h"
#include "misc.h"
#include "err.h"

void stream_init(stream_t *stream, int sock, FILE* file)
{
    stream->socket = sock;
    stream->output_file = file;
    // stream.header
    stream->current_interval = 0;
    stream->in_buffer = 0;
    memset(stream->buffer, 0, sizeof(stream->buffer));

    memset(stream->title, 0, sizeof(stream->title));
    stream->title[0] = '\0';

    stream->stream_on = true;
}

int send_stream_request(const stream_t *stream, const char* path)
{
    // send request for shoutcast
    char request[1000];
    sprintf(request, "GET %s HTTP/1.0 \r\nIcy-MetaData: 1 \r\n\r\n", path);
    size_t request_length = sizeof(request);
    debug_print("request sent: %s\n", request);

    while (request_length > 0) {
        ssize_t bytes_send = send(stream->socket, request, sizeof(request), 0);
        if (bytes_send < 0) {
            syserr("send() request failed");
        }

        request_length -= bytes_send;
    }

    return 0;
}
