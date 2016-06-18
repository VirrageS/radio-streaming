#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include "command.h"

Command::Command()
  : socket_(),
    port_()
{}

Command::Command(uint16_t port)
  : socket_(),
    port_(port)
{}

Command::~Command()
{
    close(socket_);
}

/**
    Set socket for command listener.
    **/
void
Command::InitializeSocket()
{
    int err = 0;
    struct sockaddr_in server_address;

    // creating IPv4 UDP socket
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        throw std::runtime_error("socket() failed");
    }

    int opt = 1;
    err = setsockopt(socket_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int));
    if (err < 0) {
        throw std::runtime_error("setsockopt(SO_REUSEPORT) failed");
    }

    // binding the listening socket
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port_);

    err = bind(socket_, (struct sockaddr *)&server_address, sizeof(server_address));
    if (err < 0) {
        throw std::runtime_error("bind() failed");
    }
}


/**
    Function for thread which handle commands.
    **/
void
Command::Listen(Command* command, Stream* stream)
{
    char buffer[10];

    while (!stream->quiting()) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);

        ssize_t bytes_received = recvfrom(command->socket_, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client, (socklen_t *)&client_len);
        if (bytes_received < 0) {
            if (!stream->quiting()) {
                std::perror("recvfrom() handle_commands");
                std::exit(EXIT_FAILURE);
            }
        } else if (bytes_received > 0) {
            buffer[bytes_received] = '\0';

            if (strcmp(buffer, "TITLE") == 0) {
                std::string title = stream->title();
                sendto(command->socket_, title.c_str(), title.length(), 0, (struct sockaddr *)&client, (socklen_t)client_len);
            } else if (strcmp(buffer, "PAUSE") == 0) {
                stream->paused(true);
            } else if (strcmp(buffer, "PLAY") == 0) {
                stream->paused(false);
            } else if (strcmp(buffer, "QUIT") == 0) {
                stream->quiting(true);
            }
        }

        memset(&buffer, 0, sizeof(buffer));
    }
}
