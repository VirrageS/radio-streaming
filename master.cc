#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr

#include "err.h"
#include "misc.h"
#include "session.h"

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


void* handle_session(void *arg)
{
    Session *session = (Session*)(arg);
    debug_print("%s\n", "started handling session...");

    session->add_poll_fd(session->socket);

    while (true) {
        debug_print("%s\n", "before poll...");
        int err = poll(session->poll_sockets.data(), (int)session->poll_sockets.size(), session->get_timeout() * 1000);
        debug_print("%s\n", "poll activated...");

        if (err < 0) {
            std::cerr << "poll() failed" << std::endl;
            goto end_session;
        } else if (err == 0) {
            session->handle_timeout();
        } else {
            for (auto descriptor = session->poll_sockets.begin(); descriptor != session->poll_sockets.end(); ++descriptor) {
                if (descriptor->revents == 0)
                    continue;

                if (!(descriptor->revents & (POLLIN | POLLHUP))) {
                    debug_print("[ERROR] revents = %d\n", descriptor->revents);
                    goto end_session;
                }

                if (descriptor->fd != session->socket) {
                    // we got message on player stderr
                    // so we should delete this
                    char buffer[1024];
                    read(descriptor->fd, buffer, sizeof(buffer));

                    for (auto radio : session->radios) {
                        if (radio.player_stderr == descriptor->fd) {
                            std::string msg = "ERROR " + radio.id + ": " + buffer;

                            session->send_session_message(msg);
                            session->remove_radio_with_id(radio.id);
                            break;
                        }
                    }

                    session->poll_sockets.erase(descriptor);
                } else {
                    ssize_t bytes_recieved = read(session->socket, &session->buffer[session->in_buffer], sizeof(session->buffer) - session->in_buffer);
                    if (bytes_recieved < 0) {
                        if (errno != EWOULDBLOCK) {
                            std::cerr << "read() in session failed" << std::endl;
                            goto end_session;
                        }
                    } else if (bytes_recieved == 0) {
                        debug_print("ending %d connection", session->socket);
                        goto end_session;
                    } else {
                        session->in_buffer += bytes_recieved;

                        debug_print("%s\n", "got something to read");
                        debug_print("message: %s\n", session->buffer);

                        size_t end = 0;
                        for (size_t i = 0; i < session->in_buffer; ++i) {
                            if (i + 1 < session->in_buffer) {
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
                            std::string message = std::string(session->buffer, end);
                            session->parse_and_action(message);

                            session->in_buffer -= end;
                            memset(&session->buffer, 0, end);
                        }
                    }
                }

                break;
            }
        }
    }

end_session:
    sessions.remove_session(session->id);
    debug_print("%s\n", "closing handling session...");
    return 0;
}



void set_master_socket()
{
    int err, sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        syserr("socket() failed");
    }

    int opt = 1;
    err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
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
    signal(SIGINT, handle_signal);
    signal(SIGKILL, handle_signal);

    validate_parameters(argc, argv);
    set_master_socket();

    while (true) {
        int err;

        int client_socket = accept(master_socket, NULL, NULL);
        if (client_socket < 0) {
            syserr("accept() failed");
        }

        Session& session = sessions.add_session();
        session.socket = client_socket;

        err = pthread_create(&session.thread, 0, handle_session, (void*)&session);
        if (err < 0) {
            sessions.remove_session(session.id);
            std::cerr << "Failed to establish session" << std::endl;
        }

        err = pthread_detach(session.thread);
        if (err < 0) {
            sessions.remove_session(session.id);
            std::cerr << "Failed to establish session" << std::endl;
        }
    }


    clean_all();
    return 0;
}