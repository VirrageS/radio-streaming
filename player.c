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

#include <pthread.h>

#include "err.h"
#include "misc.h"
#include "header.h"
#include "stream.h"
#include "parser.h"


// GLOBALS
char *host, *path, *file_name, *server_port_str, *command_port_str;
uint16_t server_port, command_port;
bool meta_data, save_to_file = true;

stream_t stream;
volatile bool player_on;

int command_socket;

pthread_t command_thread;

typedef enum {
    TITLE, PLAY, PAUSE, QUIT, NONE
} command_t;

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

void* handle_commands()
{
    // get command
    size_t in_buffer = 0;
    char command[30000];

    while (player_on) {
        ssize_t bytes_received = recv(command_socket, &command[in_buffer], sizeof(command) - in_buffer, 0);
        if (bytes_received < 0) {
            syserr("recv() handle_commands");
        } else if (bytes_received == 0) {
            // do nothing
        } else {
            in_buffer += bytes_received;
            command[in_buffer] = '\0';

            command_t c = NONE;
            do {
                char *cur_pos = NULL, *cur_min_pos = NULL;

                if ((cur_pos = strstr(command, "TITLE")) != NULL) {
                    if (cur_min_pos - cur_pos > 0) {
                        cur_min_pos = cur_pos;
                        c = TITLE;
                    }
                }

                if ((cur_pos = strstr(command, "PAUSE")) != NULL) {
                    if (cur_min_pos - cur_pos > 0) {
                        cur_min_pos = cur_pos;
                        c = PAUSE;
                    }
                }

                if ((cur_pos = strstr(command, "PLAY")) != NULL) {
                    if (cur_min_pos - cur_pos > 0) {
                        cur_min_pos = cur_pos;
                        c = PLAY;
                    }
                }

                if ((cur_pos = strstr(command, "QUIT")) != NULL) {
                    if (cur_min_pos - cur_pos > 0) {
                        cur_min_pos = cur_pos;
                        c = QUIT;
                    }
                }

                if (c == TITLE) {
                    // send current title
                    // send(command_socket, stream->title, strlen(stream->title), 0);
                } else if (c == PAUSE) {
                    stream.stream_on = false;
                } else if (c == PLAY) {
                    stream.stream_on = true;
                } else if (c == QUIT) {
                    player_on = false;
                }
            } while (c != NONE);
        }
    }

    return 0;
}

void start_command_listener()
{
    int err;

    err = pthread_create(&command_thread, 0, handle_commands, NULL);
    if (err < 0) {
        syserr("pthread_create");
    }


    err = pthread_detach(command_thread);
    if (err < 0) {
        syserr("pthread_detach");
    }

    debug_print("%s\n", "command listener just have started");
}

int set_command_socket()
{
    int err = 0;
    struct sockaddr_in server_address;

    // creating IPv4 UDP socket
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        syserr("socket() failed");
    }

    // binding the listening socket
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(command_port);
    err = bind(sock, (struct sockaddr *) &server_address, sizeof(server_address));
    if (err < 0) {
        syserr("bind() failed");
    }

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

    // command_port_str = argv[5];
    tmp_port = strtol(argv[5], NULL, 10);
    if (tmp_port <= 0L) {
        fatal("Port (%s) should be number larger than 0.\n", argv[5]);
    }
    command_port = (uint16_t)tmp_port;

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

    command_socket = set_command_socket();
    start_command_listener();

    // set player stream
    FILE* output_file = validate_parameters(argc, argv);
    stream_init(&stream, output_file);
    set_stream_socket(&stream, host, server_port_str);
    send_stream_request(&stream, path);

    stream_listen();

    clean_all();
    return 0;
}
