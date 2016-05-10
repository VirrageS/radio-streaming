#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include "err.h"
#include "misc.h"
#include "header.h"
#include "stream.h"
#include "parser.h"


// GLOBALS
char *host, *path, *file_name, *server_port_str;
uint16_t server_port, listen_port;
bool meta_data, save_to_file = true;

// opened file to which we should save music
FILE *output_file;

// struct for current song
// title, length or something like that

int shoutcast_socket;


void clean_all()
{
    if (save_to_file) {
        fclose(output_file);
    }

    close(shoutcast_socket);
}


void handle_signal(int sig)
{
    clean_all();
    exit(sig);
}

void handle_commands()
{
    // get command
    char *command = "";

    if (strcmp(command, "TITLE")) {
        // send current title
    }
}

int set_client_socket()
{
    int err = 0;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(host, server_port_str, &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    } else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    // initialize socket according to getaddrinfo results
    int sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0)
        syserr("socket");

    // connect socket to the server
    err = connect(sock, addr_result->ai_addr, addr_result->ai_addrlen);
    if (err < 0)
        syserr("connect() failed");

    freeaddrinfo(addr_result);

    debug_print("%s\n", "client socket initialized");
    return sock;
}


int send_stream_request(const stream_t *stream)
{
    int err;
    // send request for shoutcast
    char request[1000];
    sprintf(request, "GET %s HTTP/1.0 \r\nIcy-MetaData: %d \r\n\r\n", path, (int)meta_data);
    debug_print("request sent: %s\n", request);


    // TODO: add while
    err = send(stream->socket, request, sizeof(request), 0);
    if (err < 0) {
        syserr("send() request failed");
    }

    return 0;
}


void stream_listen()
{
    stream_t stream;
    stream.socket = shoutcast_socket;
    stream.output_file = output_file;
    stream.in_buffer = 0;
    memset(stream.buffer, 0, sizeof(stream.buffer));

    send_stream_request(&stream);
    parse_header(&stream);

    stream.current_interval = stream.header.metaint;

    if (DEBUG) {
        print_header(&stream.header);
    }

    debug_print("%s\n", "getting data");
    while (true) {
        parse_data(&stream);
    }
}


void validate_parameters(int argc, char *argv[])
{
    host = argv[1];
    path = argv[2];
    file_name = argv[4];

    if (strcmp(file_name, "-") == 0)
        save_to_file = false;

    // validate ports
    server_port_str = argv[3];
    long int tmp_port = strtol(argv[3], NULL, 10);
    if (tmp_port <= 0L) {
        fatal("Port (%s) should be number larger than 0.\n", argv[3]);
    }

    server_port = (uint16_t)tmp_port;

    tmp_port = strtol(argv[5], NULL, 10);
    if (tmp_port <= 0L) {
        fatal("Port (%s) should be number larger than 0.\n", argv[5]);
    }

    listen_port = (uint16_t)tmp_port;

    // validate meta data parameter
    if (strtob(&meta_data, argv[6]) != 0) {
        fatal("Meta data (%s) should be 'yes' or 'no'.\n", argv[6]);
    }

    if (save_to_file) {
        output_file = fopen(file_name, "wb");

        if (!output_file) {
            fatal("Could not create (%s) file.\n", file_name);
        }
    } else {
        debug_print("%s\n", "printing to stdout");
        output_file = stdout;
    }
}

int main(int argc, char *argv[])
{
    signal(SIGINT, handle_signal);
    signal(SIGKILL, handle_signal);

    // INITAL VALUES
    int err;

    validate_parameters(argc, argv);

    // setting up client socket
    shoutcast_socket = set_client_socket();
    stream_listen();

    clean_all();
    return 0;
}
