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


void parse_and_action(char* buffer, size_t end)
{
    size_t start = 0;
    while ((isspace(buffer[start]) || buffer[start] == '\r' || buffer[start] == '\n') && start < end)
        start++;

    while ((isspace(buffer[end-1]) || buffer[end-1] == '\r' || buffer[end-1] == '\n') && end > 0)
        end--;

    end -= start;
    memcpy(&buffer[0], &buffer[start], end);
    buffer[end] = '\0';

    int items;

    // PARSE COMMAND
    char command[10], hour[4], minutes[4], computer[256], host[256], file[256], meta_data[4], id[256];
    unsigned int resource_port, listen_port, time_length;

    // "START" COMMAND
    items = sscanf(buffer, "%s %s %s %s %u %s %u %s", &command, &computer, &host, &path, &resource_port, &file, &listen_port, &meta_data);
    if (items == 8) {
        if (strcmp(command, "START") != 0)
            return;

        if (strcmp(file, "-") == 0)
            return;

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0))
            return;

        pid_t pid = fork();
        if (pid == 0) {
            // fd_in = open("td_in", O_RDWR | O_NONBLOCK);
            // dup2(fd_in, 0);
            // close(fd_in);
            // fd_out = open("td_out", O_RDWR | O_NONBLOCK);
            // dup2(fd_out, 1);
            // close(fd_out);
            execlp("ssh", "-t", "students", "./player", NULL);
        }

        return;
    }

    // "AT" COMMAND
    items = sscanf(buffer, "%s %s:%s %u %s %s %s %u %s %u %s", &command, &hour, &minutes, &time_length, &computer, &host, &path, &resource_port, &file, &listen_port, &meta_data);
    if (items == 11) {
        if (strcmp(command, "AT") != 0)
            return;

        if ((strlen(hour) != 2) || (strlen(minutes) != 2))
            return;

        int ihour = ((int)hour[0] * 10) + (int)hour[1];
        if ((ihour < 0) || (ihour > 24))
            return;

        int iminutes = ((int)minutes[0] * 10) + (int)minutes[1];
        if ((iminutes < 0) || (iminutes >= 60))
            return;

        if (strcmp(file, "-") == 0)
            return;

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0))
            return;

        // TODO:
        // start new thread
        // sleep until HH:MM
        // start player
        // sleep M minutes
        // send QUIT command
        return;
    }

    // PLAYER COMMANDS
    items = sscanf(buffer, "%s %s", &command, &id);
    if (items == 2) {
        radio_t *radio = get_radio_by_id(session, id);
        if (!radio) {
            return;
        }

        // check if any command matches
        if ((strcmp(command, "PAUSE") != 0) &&
            (strcmp(command, "PLAY") != 0) &&
            (strcmp(command, "TITLE") != 0) &&
            (strcmp(command, "QUIT") != 0)) {
            return;
        }

        // MAKE UDP CONNECTION
        struct hostent *host = (struct hostent *)gethostbyname(radio->host);

        int sock;
        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            syserr("socket()");
        }

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(radio->port);
        server_addr.sin_addr = *((struct in_addr *)host->h_addr);
        bzero(&(server_addr.sin_zero),8);

        // TODO: make while and check if full data is send
        // send command
        ssize_t bytes_send = sendto(sock, command, strlen(command), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
        if (bytes_send != strlen(command)) {
            syserr("sendto() failed/partial");
        }
    }

}

void* handle_session(void *arg)
{
    session_t *session = (session_t*)(arg);
    debug_print("%s\n", "started handling session...");

    while (true) {
        ssize_t bytes_recieved = read(session->socket, &session->buffer[session->in_buffer], sizeof(session->buffer) - session->in_buffer);

        if (bytes_recieved < 0) {
            syserr("read() failed");
        } else if (bytes_recieved == 0) {
            debug_print("%s\n", "connection has ended");
        } else {
            session->in_buffer += bytes_recieved;

            size_t end = 0;
            for (size_t i = 0; i < session->in_buffer - 1; ++i) {
                if ((session->buffer[i] == '\r' && session->buffer[i + 1] == '\n') ||
                    (session->buffer[i + 1] == '\r' || session->buffer[i + 1] == '\n')) {
                    end = i + 2;
                    break;
                }
            }

            if (end > 0) {
                char* tmp_buffer = malloc(sizeof(char) * (session->end));
                strncpy(&tmp_buffer, &session->buffer, session->end);

                parse_and_action(tmp_buffer);

                memset(&session->buffer, 0, end);
            }

            memset(&session->buffer, 0, session->end);
        }
    }

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
    validate_parameters(argc, argv);
    set_master_socket();


    while (true) {
        int client_socket = accept(master_socket, NULL, NULL);
        if (client_socket < 0) {
            syserr("accept() failed");
        }

        session_t *session = add_session(&sessions);
        session->socket = client_socket;

        int err = pthread_create(&session->thread, 0, handle_commands, (void*)session);
        if (err < 0) {
            syserr("pthread_create");
        }

        err = pthread_detach(session->thread);
        if (err < 0) {
            syserr("pthread_detach");
        }
    }

    return 0;
}
