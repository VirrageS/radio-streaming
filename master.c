#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr

#include "err.h"
#include "misc.h"

int master_socket;
uint16_t master_port;
char* master_port_str;

void set_master_socket()
{
    int err, sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        syserr("socket() failed");
    }

    err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
    if (err < 0) {
        syserr("setsockopt() failed");
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = (master_port > 0 ? htons(master_port) : 0);

    err = bind(sock, (struct sockaddr*)&server, sizeof(server));
    if (err < 0) {
        syserr("bind() failed");
    }

    err = listen(sock, 10);
    if (err < 0) {
        syserr("listen() failed");
    }

    master_socket = sock;
}

void validate_parameters(int argc, char* argv[])
{
    if (argc > 2) {
        fatal("Usage ./%s <port>", argv[0]);
    }

    master_port = 0;

    if (argc == 2) {
        // validate ports
        master_port_str = argv[1];
        long int tmp_port = strtol(master_port_str, NULL, 10);
        if (tmp_port <= 0L) {
            fatal("Port (%s) should be number larger than 0.\n", master_port_str);
        }
        master_port = (uint16_t)tmp_port;
    }
}

int main(int argc, char* argv[])
{
    validate_parameters(argc, argv);
    set_master_socket();

    int client_socket = accept(master_socket, NULL, NULL);
    if (client_socket < 0) {
        syserr("accept() failed");
    }

    size_t in_buffer = 0;
    char buffer[30000];

    while (true) {
        ssize_t bytes_recieved = read(client_socket, &buffer[in_buffer], sizeof(buffer) - in_buffer);

        if (bytes_recieved < 0) {
            syserr("read() failed");
        } else if (bytes_recieved == 0) {
            debug_print("%s\n", "connection has ended");
        } else {
            in_buffer += bytes_recieved;

            debug_print("%s\n", "got message!");
            debug_print("%.*s\n", (int)in_buffer, buffer);

            memset(&buffer, 0, in_buffer);

            pid_t pid = fork();
            if (pid == 0) {
                // fd_in = open("td_in", O_RDWR | O_NONBLOCK);
                // dup2(fd_in, 0);
                // close(fd_in);
                // fd_out = open("td_out", O_RDWR | O_NONBLOCK);
                // dup2(fd_out, 1);
                // close(fd_out);
                execlp("ssh", "-t", "students", "./player", 0);
            }
        }
    }

    return 0;
}
