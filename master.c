#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr

#include "err.h"

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

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(master_port);

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

    // validate ports
    master_port_str = argv[1];
    long int tmp_port = strtol(master_port_str, NULL, 10);
    if (tmp_port <= 0L) {
        fatal("Port (%s) should be number larger than 0.\n", master_port_str);
    }
    master_port = (uint16_t)tmp_port;
}

int main(int argc, char* argv[])
{
    validate_parameters(argc, argv);
    set_master_socket();

    return 0;
}
