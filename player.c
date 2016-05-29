#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include <signal.h>
#include <pthread.h>

#include "misc.h"
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
    close(command_socket);
}


void handle_signal(int sig)
{
    clean_all();
    exit(sig);
}

void* handle_commands(void *arg)
{
    // get command
    char command[10];

    debug_print("%s\n", "handling command thread started...");

    while (player_on) {
        struct sockaddr_in client;
        int client_len = sizeof(client);

        ssize_t bytes_received = recvfrom(command_socket, &command, sizeof(command), 0, (struct sockaddr *)&client, (socklen_t *)&client_len);
        if (bytes_received < 0) {
            if (player_on) {
                syserr("recvfrom() handle_commands");
            }
        } else if (bytes_received == 0) {
            debug_print("%s\n", "recieved nothing...");
        } else {
            command[bytes_received] = '\0';

            debug_print("received packet from %s:%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
            debug_print("%s\n", command);

            if (strcmp(command, "TITLE") == 0) {
                debug_print("%s\n", "sending current title");
                sendto(command_socket, stream.title, strlen(stream.title), 0, (struct sockaddr *)&client, (socklen_t)client_len);
            } else if (strcmp(command, "PAUSE") == 0) {
                debug_print("%s\n", "stream paused");
                stream.stream_on = false;
            } else if (strcmp(command, "PLAY") == 0) {
                debug_print("%s\n", "stream play");
                stream.stream_on = true;
            } else if (strcmp(command, "QUIT") == 0) {
                debug_print("%s\n", "quiting now");
                player_on = false;
            }

            memset(&command, 0, sizeof(command));
        }
    }

    debug_print("%s\n", "closing handling commands...");
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
    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        syserr("socket() failed");
    }

    // binding the listening socket
    debug_print("command port: %d\n", command_port);
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(command_port);

    err = bind(sock, (struct sockaddr *)&server_address, sizeof(server_address));
    if (err < 0) {
        syserr("bind() failed");
    }

    return sock;
}


void stream_listen()
{
    if (parse_header(&stream) < 0)
        player_on = false;

    print_header(&stream.header);
    debug_print("%s\n", "stream is listening");

    // command 'QUIT' can turn off player
    while (player_on) {
        if (parse_data(&stream) < 0) // check if stream radio has ended connection
            player_on = false;
    }

    debug_print("%s\n", "stream is not listening");
}


FILE* validate_parameters(int argc, char *argv[])
{
    if (argc != 7) {
        fatal("Wrong number of parameters");
    }

    host = argv[1];
    path = argv[2];
    file_name = argv[4];

    if (strcmp(file_name, "-") == 0)
        save_to_file = false;

    // validate ports
    server_port_str = argv[3];
    long int tmp_port = strtol(server_port_str, NULL, 10);
    if ((tmp_port <= 0L) || (errno == ERANGE) || (tmp_port > 65535L)) {
        fatal("Port (%s) should be number larger than 0.\n", server_port_str);
    }
    server_port = (uint16_t)tmp_port;

    // command_port_str = argv[5];
    tmp_port = strtol(argv[5], NULL, 10);
    debug_print("tmp_port: (%s) %ld\n", argv[5], tmp_port);
    if ((tmp_port <= 0L) || (errno == ERANGE) || (tmp_port > 65535L)) {
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

    FILE* output_file = validate_parameters(argc, argv);
    player_on = true;

    command_socket = set_command_socket();
    start_command_listener();

    // set player stream
    stream_init(&stream, output_file, meta_data);
    set_stream_socket(&stream, host, server_port_str);
    send_stream_request(&stream, path);

    stream_listen();

    clean_all();

    return 0;
}
