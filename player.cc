#include <iostream>
#include <string>
#include <fstream>
#include <ostream>
#include <csignal>
#include <thread>

#include <unistd.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include "misc.h"
#include "stream.h"


// GLOBALS
char *host, *path, *file_name, *server_port_str, *command_port_str;
uint16_t server_port, command_port;
bool metadata, save_to_file = true;

Stream g_stream;
int g_command_socket;

typedef enum {
    TITLE, PLAY, PAUSE, QUIT, NONE
} command_t;

void clean_all()
{
    close(g_command_socket);
}


void handle_signal(int sig)
{
    clean_all();
    exit(sig);
}

/**
    Function for thread which handle commands.
    **/
void HandleCommands()
{
    // get command
    char command[10];

    debug_print("%s\n", "handling command thread started...");

    while (!g_stream.quiting()) {
        struct sockaddr_in client;
        int client_len = sizeof(client);

        ssize_t bytes_received = recvfrom(g_command_socket, &command, sizeof(command), 0, (struct sockaddr *)&client, (socklen_t *)&client_len);
        if (bytes_received < 0) {
            syserr("recvfrom() handle_commands");
        } else if (bytes_received == 0) {
            debug_print("%s\n", "recieved nothing...");
        } else {
            command[bytes_received] = '\0';

            debug_print("received packet from %s:%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
            debug_print("%s\n", command);

            if (strcmp(command, "TITLE") == 0) {
                debug_print("%s\n", "sending current title");
                std::string title = g_stream.title();
                sendto(g_command_socket, title.c_str(), title.length(), 0, (struct sockaddr *)&client, (socklen_t)client_len);
            } else if (strcmp(command, "PAUSE") == 0) {
                debug_print("%s\n", "stream paused");
                g_stream.paused(true);
            } else if (strcmp(command, "PLAY") == 0) {
                debug_print("%s\n", "stream play");
                g_stream.paused(false);
            } else if (strcmp(command, "QUIT") == 0) {
                debug_print("%s\n", "quiting now");
                g_stream.quiting(true);
            }

            memset(&command, 0, sizeof(command));
        }
    }

    debug_print("%s\n", "closing handling commands...");
}


/**
    Set socket for command listener.
    **/
void set_command_socket()
{
    int err = 0;
    struct sockaddr_in server_address;

    // creating IPv4 UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        syserr("socket() failed");
    }

    int opt = 1;
    err = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int));
    if (err < 0) {
        syserr("setsockopt(SO_REUSEPORT) failed");
    }

    // binding the listening socket
    debug_print("command port: %d\n", command_port);
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(command_port);

    err = bind(sock, (struct sockaddr *)&server_address, (socklen_t)sizeof(server_address));
    if (err < 0) {
        syserr("bind() failed");
    }

    g_command_socket = sock;
}


/**
    Validate player parameters.
    **/
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
    for (int i = 0; i < strlen(server_port_str); ++i) {
        if (!isdigit(server_port_str[i]))
            fatal("Invalid number.");
    }

    long int tmp_port = strtol(server_port_str, NULL, 10);
    if ((tmp_port <= 0L) || (errno == ERANGE) || (tmp_port > 65535L)) {
        fatal("Port (%s) should be number larger than 0.", server_port_str);
    }
    server_port = (uint16_t)tmp_port;

    char *command_port_str = argv[5];
    for (int i = 0; i < strlen(command_port_str); ++i) {
        if (!isdigit(command_port_str[i]))
            fatal("Invalid number.");
    }

    tmp_port = strtol(command_port_str, NULL, 10);
    debug_print("tmp_port: (%s) %ld\n", command_port_str, tmp_port);
    if ((tmp_port <= 0L) || (errno == ERANGE) || (tmp_port > 65535L)) {
        fatal("Port (%s) should be number larger than 0.", argv[5]);
    }
    command_port = (uint16_t)tmp_port;

    // validate meta data parameter
    if (strtob(&metadata, argv[6]) != 0) {
        fatal("Meta data (%s) should be 'yes' or 'no'.", argv[6]);
    }

    FILE* output_file;
    if (save_to_file) {
        output_file = std::fopen(file_name, "wb");

        if (!output_file) {
            fatal("Could not create (%s) file.", file_name);
        }
    } else {
        debug_print("%s\n", "printing to stdout");
        output_file = stdout;
    }

    return output_file;
}


int main(int argc, char *argv[])
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGKILL, handle_signal);

    auto output_file = validate_parameters(argc, argv);

    set_command_socket();
    std::thread (HandleCommands).detach();

    // set player stream
    g_stream = Stream(output_file, metadata);
    g_stream.InitializeSocket(host, server_port_str);
    g_stream.SendRequest(path);
    g_stream.Listen();

    clean_all();

    return 0;
}
