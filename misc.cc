#include <vector>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>

#include "misc.h"

class ReceiveDataFailureException: public std::exception
{
    virtual const char* what() const throw() {
        return "Failed to receive data from the server";
    }
};


int strtob(bool* b, const char* str)
{
    if (strcmp(str, "yes") == 0) {
        *b = true;
        return 0;
    } else if (strcmp(str, "no") == 0) {
        *b = false;
        return 0;
    }

    return 1;
}


bool
PollRecv(int socket, std::string& buffer, unsigned int timeout)
{
    struct pollfd poll_socket[1];
    poll_socket[0].fd = socket;
    poll_socket[0].events = POLLIN;

    int err = poll(poll_socket, 1, timeout);
    if (err <= 0) {
        throw ReceiveDataFailureException();
    }

    std::vector<char> tmp_buffer(10000);
    ssize_t bytes_recieved = recv(socket, tmp_buffer.data(), tmp_buffer.size(), 0);
    if (bytes_recieved < 0) {
        throw ReceiveDataFailureException();
    } else if (bytes_recieved == 0) {
        return false;
    }

    tmp_buffer.resize(bytes_recieved);
    buffer.append(tmp_buffer.cbegin(), tmp_buffer.cend());
    return true;
}


uint16_t
ParsePort(std::string port)
{
    for (char c : port) {
        if (!isdigit(c)) {
            throw std::invalid_argument("Invalid port");
        }
    }

    auto tmp_port = stoul(port, NULL, 10);
    if ((tmp_port > 65535L) || (tmp_port == 0L)) {
        throw std::overflow_error("Invalid port");
    }

    return (uint16_t)tmp_port;;
}
