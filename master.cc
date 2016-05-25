#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr

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

    bool end_session = false;
    while (!end_session) {
        int err = poll(session->poll_sockets.data(), (int)session->poll_sockets.size(), session->get_timeout() * 1000);

        if (err < 0) {
            end_session = true;
        } else if (err == 0) {
            session->handle_timeout();
            debug_print("%s", "time out");
            end_session = true;
        } else {
            for (auto descriptor : session->poll_sockets) {
                if (descriptor.revents == 0)
                    continue;

                if (!(descriptor.revents & (POLLIN | POLLHUP))) {
                    debug_print("[ERROR] revents = %d\n", descriptor.revents);
                    end_session = true;
                    break;
                }

                if (descriptor.revents & POLLIN) {
                    ssize_t bytes_recieved = read(session->socket, &session->buffer[session->in_buffer], sizeof(session->buffer) - session->in_buffer);

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
                        char* tmp_buffer = (char*)malloc(sizeof(char) * (end));
                        strncpy(tmp_buffer, &session->buffer[0], end);

                        session->parse_and_action(tmp_buffer, end);

                        free(tmp_buffer);

                        session->in_buffer -= end;
                        memset(&session->buffer, 0, end);
                    }
                }
            }
        }
    }

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
        // syserr("socket() failed");
    }

    int opt = 1;
    err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
    if (err < 0) {
        // syserr("setsockopt() failed");
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = (master_port > 0 ? htons(master_port) : 0);

    err = bind(sock, (struct sockaddr*)&server, sizeof(server));
    if (err < 0) {
        // syserr("bind() failed");
    }

    err = listen(sock, 10);
    if (err < 0) {
        // syserr("listen() failed");
    }

    master_socket = sock;
}

void validate_parameters(int argc, char* argv[])
{
    if (argc > 2) {
        // fatal("Usage ./%s <port>", argv[0]);
    }

    master_port = 0;

    if (argc == 2) {
        // validate ports
        master_port_str = argv[1];
        long int tmp_port = strtol(master_port_str, NULL, 10);
        if (tmp_port <= 0L) {
            // fatal("Port (%s) should be number larger than 0.\n", master_port_str);
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
        int client_socket = accept(master_socket, NULL, NULL);
        if (client_socket < 0) {
            // syserr("accept() failed");
        }

        // make socket nonblocking
        int err = fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK);
        if (err < 0) {
            // syserr("fcntl() failed");
        }

        Session& session = sessions.add_session();
        session.socket = client_socket;

        err = pthread_create(&session.thread, 0, handle_session, (void*)&session);
        if (err < 0) {
            // syserr("pthread_create");
        }

        err = pthread_detach(session.thread);
        if (err < 0) {
            // syserr("pthread_detach");
        }
    }


    clean_all();
    return 0;
}
