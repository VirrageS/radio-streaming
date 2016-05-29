#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <fcntl.h>

#include <signal.h>

#include "misc.h"
#include "session.hh"

int master_socket;
uint16_t master_port;
char* master_port_str;

Sessions sessions;

void clean_all()
{

}


void handle_signal(int sig)
{
    clean_all();
    exit(sig);
}


/**
    Thread session function, which handle everything connected with session.
    **/
void handle_session(std::shared_ptr<Session> session)
{
    debug_print("[%s] started handling session...\n", session->id().c_str());

    bool added = session->add_poll_fd(session->socket());
    if (!added) {
        goto end_session;
    }

    while (true) {
        debug_print("[%s] before poll [%d - %lu]... timeout in %u...\n", session->id().c_str(), session->socket(), session->poll_sockets().size(), session->get_timeout());
        int err = poll(session->poll_sockets().data(), (int)session->poll_sockets().size(), session->get_timeout() * 1000);
        debug_print("[%s] after poll [%d - %lu]...\n", session->id().c_str(), session->socket(), session->poll_sockets().size());

        if (err < 0) {
            std::cerr << "poll() failed" << std::endl;
            goto end_session;
        } else if (err == 0) {
            debug_print("[%s] handling timeout...\n", session->id().c_str());
            session->handle_timeout();
        } else {
            for (auto descriptor = session->poll_sockets().begin(); descriptor != session->poll_sockets().end(); ++descriptor) {
                if (descriptor->revents == 0)
                    continue;

                if (!(descriptor->revents & (POLLIN | POLLHUP))) {
                    std::cerr << descriptor->fd << " - unexpected revents = " << descriptor->revents << std::endl;
                    goto end_session;
                }

                if (descriptor->fd != session->socket()) {
                    // we got message on player stderr
                    // so we should delete this
                    std::string buffer = "";

                    while (true) {
                        std::vector<char> tmp_buffer(4096);
                        ssize_t bytes_recieved = read(descriptor->fd, tmp_buffer.data(), tmp_buffer.size() - 1);
                        if (bytes_recieved <= 0)
                            break;

                        tmp_buffer.resize(bytes_recieved);
                        buffer.append(tmp_buffer.cbegin(), tmp_buffer.cend());
                    }

                    debug_print("%s\n", "got something on player stderr");

                    for (auto radio : session->radios()) {
                        if (radio->player_stderr() == descriptor->fd) {
                            std::string msg = "ERROR " + radio->id() + ": " + buffer;

                            session->send_session_message(msg);
                            session->remove_radio_by_id(radio->id());
                            break;
                        }
                    }
                } else {
                    std::vector<char> tmp_buffer(4096);

                    ssize_t bytes_recieved = read(session->socket(), tmp_buffer.data(), tmp_buffer.size() - 1);
                    if (bytes_recieved < 0) {
                        if (errno != EWOULDBLOCK) {
                            std::cerr << "read() in session failed" << std::endl;
                            goto end_session;
                        }
                    } else if (bytes_recieved == 0) {
                        debug_print("ending %d connection\n", session->socket());
                        goto end_session;
                    } else {
                        tmp_buffer.resize(bytes_recieved);
                        session->buffer.append(tmp_buffer.cbegin(), tmp_buffer.cend());

                        debug_print("%s\n", "got something to read");
                        debug_print("message: %s\n", session->buffer.c_str());

                        size_t end = 0;
                        for (size_t i = 0; i < session->buffer.length(); ++i) {
                            if (i + 1 < session->buffer.length()) {
                                if (session->buffer[i] == '\r' && session->buffer[i + 1] == '\n') {
                                    end = i + 2;
                                    break;
                                }
                            }

                            if ((session->buffer[i] == '\r') || (session->buffer[i] == '\n')) {
                                end = i + 1;
                                break;
                            }
                        }

                        if (end > 0) {
                            std::string message = session->buffer.substr(0, end);
                            session->parse(message);

                            session->buffer.erase(0, end);
                        }
                    }
                }

                break;
            }
        }
    }

end_session:
    debug_print("[%s] closing handling session...\n", session->id().c_str());
    sessions.remove_session_by_id(session->id());
}

/**
    Set connection on TCP listen socket.
    **/
void set_master_socket()
{
    int err, sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        syserr("socket() failed");

    int opt = 1;
    err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
    if (err < 0)
        syserr("setsockopt() failed");

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = (master_port > 0 ? htons(master_port) : 0);

    err = bind(sock, (struct sockaddr*)&server, sizeof(server));
    if (err < 0)
        syserr("bind() failed");

    err = listen(sock, 10);
    if (err < 0)
        syserr("listen() failed");

    if (master_port == 0) {
        // get port on which master is listening

        socklen_t len = sizeof(server);
        err = getsockname(sock, (struct sockaddr *)&server, &len);
        if (err < 0) {
            syserr("failed to get port number");
        }

        std::cout << ntohs(server.sin_port) << std::endl;
    }

    master_socket = sock;
}


/**
    Validates program parameters
    **/
void validate_parameters(int argc, char* argv[])
{
    if (argc > 2)
        fatal("Usage ./%s <port>", argv[0]);

    master_port = 0;

    if (argc == 2) {
        // validate ports
        master_port_str = argv[1];
        long int tmp_port = strtol(master_port_str, NULL, 10);
        if ((tmp_port <= 0L) || (errno == ERANGE) || (tmp_port > 65535L)) {
            fatal("Port (%s) should be number larger than 0.\n", master_port_str);
        }

        master_port = (uint16_t)tmp_port;
    }
}


int main(int argc, char* argv[])
{
    signal(SIGINT, handle_signal);
    signal(SIGKILL, handle_signal);

    srand(time(NULL));

    validate_parameters(argc, argv);
    set_master_socket();

    while (true) {
        int client_socket = accept(master_socket, NULL, NULL);
        if (client_socket < 0) {
            syserr("accept() failed");
        }

        auto session = sessions.add_session(client_socket);
        std::thread (handle_session, session).detach();
    }


    clean_all();
    return 0;
}
