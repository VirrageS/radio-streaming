#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr

#include "err.h"
#include "misc.h"
#include "session.h"

int master_socket;
uint16_t master_port;
char* master_port_str;

sessions_t sessions;

void clean_all()
{
    destroy_sessions(&sessions);
}


void handle_signal(int sig)
{
    clean_all();
    exit(sig);
}


void* handle_session(void *arg)
{
    session_t *session = (session_t*)(arg);
    debug_print("%s\n", "started handling session...");

    while (true) {
        ssize_t bytes_recieved = read(session->socket, &session->buffer[session->in_buffer], sizeof(session->buffer) - session->in_buffer);

        if (bytes_recieved <= 0) {
            break;
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
                char* tmp_buffer = malloc(sizeof(char) * (end));
                strncpy(tmp_buffer, &session->buffer[0], end);

                parse_and_action(session, tmp_buffer, end);

                free(tmp_buffer);

                session->in_buffer -= end;
                memset(&session->buffer, 0, end);
            }
        }
    }

    remove_session(&sessions, session);
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
    signal(SIGINT, handle_signal);
    signal(SIGKILL, handle_signal);

    validate_parameters(argc, argv);
    set_master_socket();


    while (true) {
        int client_socket = accept(master_socket, NULL, NULL);
        if (client_socket < 0) {
            syserr("accept() failed");
        }

        session_t *session = add_session(&sessions);
        session->socket = client_socket;

        int err = pthread_create(&session->thread, 0, handle_session, (void*)session);
        if (err < 0) {
            syserr("pthread_create");
        }

        err = pthread_detach(session->thread);
        if (err < 0) {
            syserr("pthread_detach");
        }
    }


    clean_all();
    return 0;
}
