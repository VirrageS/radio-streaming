#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include "session.h"
#include "err.h"

static void init_radio(radio_t *radio)
{
    if (!radio)
        return;

    radio->player_stderr = -1;

    radio->id = NULL;
    radio->host = NULL;

    radio->player_host = NULL;
    radio->player_path = NULL;
    radio->player_file = NULL;
    radio->player_md = NULL;

    radio->port = 0;
    radio->player_port = 0;
    radio->hour = 0;
    radio->minute = 0;
    radio->interval = 0;
}


static void destroy_radio(radio_t *radio)
{
    if (!radio)
        return;

    pthread_cancel(radio->thread);

    free(radio->id);
    free(radio->host);

    free(radio->player_host);
    free(radio->player_path);
    free(radio->player_file);
    free(radio->player_md);

    close(radio->player_stderr);

    init_radio(radio);
}


void init_session(session_t *session)
{
    if (!session)
        return;

    pthread_mutex_init(&session->mutex , NULL);

    session->socket = 0;
    session->active = true;

    session->in_buffer = 0;
    memset(&session->buffer, 0, sizeof(session->buffer));

    session->length = 0;
    session->radios = NULL;
}

void destroy_session(session_t *session)
{
    if (!session)
        return;

    for (size_t i = 0; i < session->length; ++i) {
        destroy_radio(&session->radios[i]);
    }

    free(session->radios);

    session->socket = 0;
    session->active = false;

    session->in_buffer = 0;
    memset(&session->buffer, 0, sizeof(session->buffer));

    session->length = 0;
    session->radios = NULL;
    pthread_mutex_destroy(&session->mutex);
}

void remove_radio_with_id(session_t *session, char *id)
{
    pthread_mutex_lock(&session->mutex);

    size_t pos = 0;
    bool found = false;

    for (size_t i = 0; i < session->length; ++i) {
        if (strcmp(session->radios[i].id, id) == 0) {
            pos = i;
            found = true;
        }
    }

    if (found) {
        destroy_radio(&session->radios[pos]);

        session->length -= 1;
        memmove(&session->radios[pos], &session->radios[pos + 1], sizeof(radio_t) * (session->length - pos));
        session->radios = realloc(session->radios, session->length);

        if (!session->radios) {
            // TODO: wtf?!!?!?!?!??
        }
    }

    pthread_mutex_unlock(&session->mutex);
}


void init_sessions(sessions_t *sessions)
{
    if (!sessions)
        return;

    pthread_mutex_init(&sessions->mutex , NULL);

    sessions->length = 0;
    sessions->sessions = NULL;
}

void destroy_sessions(sessions_t *sessions)
{
    for (size_t i = 0; i < sessions->length; ++i) {
        destroy_session(&sessions->sessions[i]);
    }

    free(sessions->sessions);

    sessions->length = 0;
    sessions->sessions = NULL;
    pthread_mutex_destroy(&sessions->mutex);
}

session_t* add_session(sessions_t *sessions)
{
    pthread_mutex_lock(&sessions->mutex);

    sessions->length += 1;
    sessions->sessions = realloc(sessions->sessions, sessions->length * sizeof(session_t));
    if (!sessions->sessions) {
        syserr("realloc(): lost all sessions");
    }

    session_t *new_session = &sessions->sessions[sessions->length - 1];
    init_session(new_session);

    pthread_mutex_unlock(&sessions->mutex);
    return new_session;
}

void remove_session(sessions_t *sessions, session_t *session)
{
    pthread_mutex_lock(&sessions->mutex);

    size_t pos = 0;
    bool found = false;

    for (size_t i = 0; i < sessions->length; ++i) {
        if (&sessions->sessions[i] == session) {
            pos = i;
            found = true;
        }
    }

    if (found) {
        sessions->length -= 1;
        if (pos < sessions->length)
            memmove(&sessions->sessions[pos], &sessions->sessions[pos + 1], sizeof(session_t) * (sessions->length - pos));

        sessions->sessions = realloc(sessions->sessions, sizeof(session_t) * (sessions->length));
        if (sessions->sessions) {
            // TODO: wtf?!?!?!??!
        }
    }

    pthread_mutex_unlock(&sessions->mutex);
}

bool send_session_message(session_t *session, char *msg)
{
    size_t in_buffer = 0;
    while (true) {
        ssize_t bytes_send = send(session->socket, &msg[in_buffer], strlen(msg) - in_buffer, 0);
        if (bytes_send < 0) {
            syserr("send() failed when sending return messsage");
        } else if (bytes_send == 0) {
            return false;
        } else {
            in_buffer += bytes_send;
        }

        if (in_buffer == strlen(msg))
            break;
    }

    return true;
}

/////
// RADIO
/////

bool check_radio(radio_t *radio)
{
    if (!radio)
        return false;

    // check if stderr is empty
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(radio->player_stderr, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    if (select(1, &readfds, NULL, NULL, &timeout)) {
        return false;
    }

    return true;
}

bool send_radio_command(radio_t *radio, char *msg)
{
    if (!check_radio(radio))
        return false;

    struct hostent *host = (struct hostent *)gethostbyname(radio->host);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return false;

    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(radio->port);
    server_address.sin_addr = *((struct in_addr *)host->h_addr);

    ssize_t bytes_send = sendto(sock, &msg, strlen(msg), 0, (struct sockaddr *)&server_address, (socklen_t)sizeof(server_address));
    if (bytes_send <= 0) {
        return false;
    }

    return true;
}

bool recv_radio_response(radio_t *radio, char *buffer)
{
    struct hostent *host = (struct hostent *)gethostbyname(radio->host);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return false;

    struct sockaddr_in server_address;
    int server_len = sizeof(server_address);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(radio->port);
    server_address.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(server_address.sin_zero), 8);

    ssize_t bytes_recieved = recvfrom(sock, &buffer, sizeof(buffer), 0, (struct sockaddr *)&server_address, (socklen_t *)&server_len);
    if (bytes_recieved <= 0) {
        return false;
    }

    return true;
}


static char* generate_radio_id()
{
    enum { length = 40 };
    const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    char id[length + 1];
    for (int i = 0; i < length; ++i) {
        id[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    id[length] = '\0';
    return strdup(id);
}

radio_t* add_radio(session_t *session, char *host, unsigned long port,
                   unsigned short hour, unsigned short minute, unsigned int interval,
                   char *player_host, char *player_path, unsigned long player_port, char *player_file, char *player_md)
{
    if (!session)
        return NULL;

    pthread_mutex_lock(&session->mutex);

    radio_t radio;
    init_radio(&radio);

    char *id = NULL;
    bool check = false;

    while (!check) {
        check = true;

        for (size_t i = 0; i < session->length; ++i) {
            id = generate_radio_id();
            if (strcmp(session->radios[i].id, id) == 0) {
                check = false;
                break;
            }
        }
    }

    radio.id = id;
    radio.host = strdup(host);
    radio.port = port;
    radio.hour = hour;
    radio.minute = minute;
    radio.interval = interval;
    radio.player_path = strdup(player_path);
    radio.player_port = player_port;
    radio.player_host = strdup(player_host);
    radio.player_file = strdup(player_file);
    radio.player_md = strdup(player_md);

    session->length += 1;
    session->radios = realloc(session->radios, session->length * sizeof(radio_t));
    if (session->radios) {
        session->radios[session->length - 1] = radio;
    }


    pthread_mutex_unlock(&session->mutex);
    return 0;
}

radio_t* get_radio_by_id(session_t *session, char* id)
{
    if (!session)
        return NULL;

    pthread_mutex_lock(&session->mutex);

    radio_t *radio = NULL;
    for (size_t i = 0; i < session->length; ++i) {
        if (strcmp(session->radios[i].id, id) == 0) {
            radio = &session->radios[i];
            break;
        }
    }

    pthread_mutex_unlock(&session->mutex);
    return radio;
}





static bool start_radio(radio_t *radio)
{
    if (!check_radio(radio))
        return false;

    int err_pipe[2];
    if (pipe(err_pipe) < 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(err_pipe[0]);
        dup2(err_pipe[1], 2);

        int err = execlp(
            "ssh", "-t", radio->host,
            "./player",
                radio->player_host,
                radio->player_path,
                radio->player_port,
                radio->player_file,
                radio->port,
                radio->player_md,
            NULL
        );

        if (err < 0) {
            return false;
        }
    } else {
        close(err_pipe[1]);
        radio->player_stderr = err_pipe[0];
    }

    return true;
}

static void* start_radio_at_time(void *arg)
{
    radio_t *radio = (radio_t*)arg;

    time_t current_time = time(NULL);
    struct tm *timeinfo = localtime(&current_time);

    int sleep_time = (radio->hour - timeinfo->tm_hour) * 3600 + (radio->minute - timeinfo->tm_min) * 60;
    if (sleep_time < 0)
        return 0;

    // wait until specific time
    sleep(sleep_time);

    // start radio
    bool started = start_radio(radio);
    if (!started)
        return 0;

    // wait to finish radio after interval
    if (check_radio(radio))
        sleep(radio->interval * 60);

    if (check_radio(radio))
        send_radio_command(radio, "QUIT");

    return 0;
}


void parse_and_action(session_t *session, char* buffer, size_t end)
{
    size_t start = 0;
    while ((start < end) && (isspace(buffer[start]) || (buffer[start] == '\r') || (buffer[start] == '\n')))
        start++;

    while ((end > 0) && (isspace(buffer[end-1]) || (buffer[end-1] == '\r') || (buffer[end-1] == '\n')))
        end--;

    if (start > end)
        return;

    end -= start;

    memcpy(&buffer[0], &buffer[start], end);
    buffer[end] = '\0';

    int items;

    // PARSE COMMAND
    char command[10], hour[4], minute[4], computer[256], host[256], path[2048], file[256], meta_data[4], id[256];
    unsigned short resource_port, listen_port;
    unsigned int interval;

    // "START" COMMAND
    items = sscanf(buffer, "%s %s %s %s %hu %s %hu %s", command, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 8) {
        if (strcmp(command, "START") != 0) {
            send_session_message(session, "ERROR: Invalid command");
            return;
        }

        if (strcmp(file, "-") == 0) {
            send_session_message(session, "ERROR: Invalid file name");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            send_session_message(session, "ERROR: Invalid meta data parameter [yes / no]");
            return;
        }

        radio_t *radio = add_radio(session, computer, listen_port, 0, 0, 0, host, path, resource_port, file, meta_data);
        bool started = start_radio(radio);
        if (!started) {
            remove_radio_with_id(session, radio->id);
            send_session_message(session, "ERROR: ssh failed");
            return;
        }

        char msg[100];
        strcpy(msg, "OK ");
        strcat(msg, radio->id);

        send_session_message(session, &msg[0]);
        return;
    }

    // "AT" COMMAND
    items = sscanf(buffer, "%s %s:%s %u %s %s %s %hu %s %hu %s", command, hour, minute, &interval, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 11) {
        if (strcmp(command, "AT") != 0) {
            send_session_message(session, "ERROR: Invalid command");
            return;
        }

        if ((strlen(hour) != 2) || (strlen(minute) != 2)) {
            send_session_message(session, "ERROR: Invalid start hour or minute");
            return;
        }

        short ihour = ((int)hour[0] * 10) + (int)hour[1];
        short iminute = ((int)minute[0] * 10) + (int)minute[1];
        if ((ihour < 0) || (ihour > 24) || (iminute < 0) || (iminute >= 60)) {
            send_session_message(session, "ERROR: Invalid start hour or minute");
            return;
        }

        if (strcmp(file, "-") == 0) {
            send_session_message(session, "ERROR: Invalid file name");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            send_session_message(session, "ERROR: Invalid meta data parameter [yes / no]");
            return;
        }


        radio_t *radio = add_radio(session, computer, listen_port, ihour, iminute, interval, host, path, resource_port, file, meta_data);

        int err = pthread_create(&radio->thread, 0, start_radio_at_time, (void*)radio);
        if (err < 0) {
            remove_radio_with_id(session, radio->id);
            send_session_message(session, "ERROR: Failed to create player");
            return;
        }

        err = pthread_detach(radio->thread);
        if (err < 0) {
            remove_radio_with_id(session, radio->id);
            send_session_message(session, "ERROR: Failed to create player");
            return;
        }

        return;
    }

    // PLAYER COMMANDS
    items = sscanf(buffer, "%s %s", command, id);
    if (items == 2) {
        // check if any command matches
        if ((strcmp(command, "PAUSE") != 0) &&
            (strcmp(command, "PLAY") != 0) &&
            (strcmp(command, "TITLE") != 0) &&
            (strcmp(command, "QUIT") != 0)) {
            send_session_message(session, "ERROR: Invalid command");
            return;
        }

        radio_t *radio = get_radio_by_id(session, id);
        if (!radio) {
            send_session_message(session, "ERROR: Player with provided 'id' does not exists");
            return;
        }

        char msg[6000];
        strcpy(msg, "OK ");
        strcat(msg, radio->id);

        if (strcmp(command, "TITLE") == 0) {
            char buffer[5000];
            bool received = recv_radio_response(radio, buffer);
            if (!received) {
                send_session_message(session, "ERROR: could not reach player");//, radio->id);
                remove_radio_with_id(session, radio->id);
                return;
            }

            strcat(msg, buffer);
        }

        send_session_message(session, msg);
        return;
    }
}
