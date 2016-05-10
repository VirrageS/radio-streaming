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

stream_t stream;
volatile bool player_on;

void clean_all()
{
    if (save_to_file) {
        fclose(stream.output_file);
    }

    close(stream.socket);
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
        // send(stream->title);
    } else if (strcmp(command, "PAUSE")) {

    } else if (strcmp(command, "PLAY")) {

    } else if (strcmp(command, "QUIT")) {
        // close connection
        // save file

        player_on = false;
        // clean_all() ???
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


void stream_listen()
{
    parse_header(&stream);

    if (DEBUG) {
        print_header(&stream.header);
    }

    debug_print("%s\n", "getting data");

    // command 'QUIT' can turn off player

    player_on = true;
    while (player_on) {
        parse_data(&stream);
    }
}


FILE* validate_parameters(int argc, char *argv[])
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

    FILE* output_file;
    if (save_to_file) {
        output_file = fopen(file_name, "wb");

        if (!output_file) {
            fatal("Could not create (%s) file.\n", file_name);
        }
    } else {
        debug_print("%s\n", "printing to stdout");
        output_file = stdout;
    }

    return output_file;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, handle_signal);
    signal(SIGKILL, handle_signal);

    // TODO: thread which should handle commands (UDP)


    // set player stream
    FILE* output_file = validate_parameters(argc, argv);
    int stream_socket = set_client_socket();
    stream_init(&stream, stream_socket, output_file);

    send_stream_request(&stream, path);
    stream_listen();

    clean_all();
    return 0;
}
