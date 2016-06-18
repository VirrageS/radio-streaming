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
Stream g_stream;
int g_command_socket;
uint16_t g_command_port;

typedef enum {
    TITLE, PLAY, PAUSE, QUIT, NONE
} command_t;

void CleanAll()
{
    close(g_command_socket);
}

void HandleSignal(int sig)
{
    CleanAll();
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
        socklen_t client_len = sizeof(client);

        ssize_t bytes_received = recvfrom(g_command_socket, &command, sizeof(command), 0, (struct sockaddr *)&client, (socklen_t *)&client_len);
        if (bytes_received < 0) {
            std::perror("recvfrom() handle_commands");
            std::exit(EXIT_FAILURE);
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
void
SetCommandSocket()
{
    int err = 0;
    struct sockaddr_in server_address;

    // creating IPv4 UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::perror("socket() failed");
        std::exit(EXIT_FAILURE);
    }

    // int opt = 1;
    // err = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int));
    // if (err < 0) {
    //     std::perror("setsockopt(SO_REUSEPORT) failed");
    //     std::exit(EXIT_FAILURE);
    // }

    // binding the listening socket

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(g_command_port);

    err = bind(sock, (struct sockaddr *)&server_address, sizeof(server_address));
    if (err < 0) {
        std::perror("bind() failed");
        std::exit(EXIT_FAILURE);
    }

    g_command_socket = sock;
}


/**
    Validate player parameters.
    **/
std::tuple<FILE*, bool, std::string, uint16_t, std::string>
ValidateParameters(int argc, char *argv[])
{
    if (argc != 7) {
        std::cerr << "Wrong number of parameters" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::string file_name = argv[4];
    FILE* output_file;

    if (file_name.compare("-") != 0) {
        // we should save to file
        output_file = std::fopen(file_name.data(), "wb");

        if (!output_file) {
            std::perror("fopen() failed");
            std::exit(EXIT_FAILURE);
        }
    } else {
        output_file = stdout;
    }

    // validate ports
    uint16_t server_port;
    try {
        server_port = ParsePort(argv[3]);
        g_command_port = ParsePort(argv[5]);
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // validate meta data parameter
    bool metadata;
    if (strtob(&metadata, argv[6]) != 0) {
        std::cerr << "Meta data (" << argv[6] << ") should be 'yes' or 'no'" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cerr << metadata << std::endl;

    return std::make_tuple(output_file, metadata, argv[1], server_port, argv[2]);
}


int
main(int argc, char *argv[])
{
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGKILL, HandleSignal);

    auto parameters = ValidateParameters(argc, argv);

    SetCommandSocket();
    std::thread (HandleCommands).detach();

    // set player stream
    g_stream = Stream(std::get<0>(parameters), std::get<1>(parameters));

    try {
        g_stream.InitializeSocket(std::get<2>(parameters), std::get<3>(parameters));
        g_stream.SendRequest(std::get<4>(parameters));
        g_stream.Listen();
    } catch (std::exception& e) {
        std::perror(e.what());
        return EXIT_FAILURE;
    }

    CleanAll();
    return 0;
}
