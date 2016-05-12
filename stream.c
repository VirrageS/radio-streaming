#include <string.h>

#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "stream.h"
#include "misc.h"
#include "err.h"

void stream_init(stream_t *stream, FILE* file)
{
    stream->socket = -1;
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

int set_stream_socket(stream_t *stream, const char* host, const char* port)
{
    int err = 0;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(host, port, &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    } else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    // initialize socket according to getaddrinfo results
    stream->socket = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (stream->socket < 0)
        syserr("socket");

    // connect socket to the server
    err = connect(stream->socket, addr_result->ai_addr, addr_result->ai_addrlen);
    if (err < 0)
        syserr("connect() failed");

    freeaddrinfo(addr_result);

    debug_print("%s\n", "stream socket initialized");
    return 0;
}
