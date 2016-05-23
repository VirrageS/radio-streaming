#include "session.h"

void init_session(session_t *session)
{
    session->active = true;
    session->in_buffer = 0;
    session->length = 0;
    memset(&buffer, 0, sizeof(buffer));
}


void init_sessions(sessions_t *sessions)
{
    sessions->length = 0;
    memset(&sessions->sessions, 0, sizeof(sessions->sessions));
}

session_t* add_session(sessions_t sessions)
{
    // check if we can reuse any session that was closed
    for (size_t i = 0; i < sessions->length; ++i) {
        if (!sessions[i].active) {
            init_session(sessions[i]);
            return sessions[i];
        }
    }

    sessions->length += 1;
    sessions->sessions = realloc(sessions->sessions, sessions->length * sizeof(session_t));
    if (!sessions->sessions) {
        syserr("realloc(): lost all sessions");
    }

    session_t *new_session = sessions->sessions[sessions->length - 1]
    init_session(new_session);

    return new_session;
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

bool send_radio_command(session_t *radio, char *msg)
{
    struct hostent *host = (struct hostent *)gethostbyname(radio->host);

    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        return false;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(radio->port);
    server_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(server_addr.sin_zero),8);

    size_t in_buffer = 0;
    while (true) {
        ssize_t bytes_send = sendto(sock, &msg[in_buffer], strlen(msg) - in_buffer, 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
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

bool recv_radio_response(radio_t *radio, char *buffer)
{
    struct hostent *host = (struct hostent *)gethostbyname(radio->host);

    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        return false;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(radio->port);
    server_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(server_addr.sin_zero), 8);

    ssize_t bytes_recieved = recvfrom(sock, &buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
    if (bytes_recieved < 0) {
        syserr("recvfrom() failed when sending return messsage");
    } else if (bytes_recieved == 0) {
        return false;
    }


    return true;
}


static char* generate_radio_id()
{
    const size_t length = 40;
    const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    char id[length];
    for (int i = 0; i < length; ++i) {
        id[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    id[length] = '\0';
    return strdup(id);
}

void init_radio(radio_t *radio, char *host, int port)
{
    radio->id = NULL;
    radio->host = host;
    radio->port = port;
}


radio_t* add_radio(session_t *session, char *host, int port)
{
    radio_t radio;
    init_radio(&radio, host, port);

    char *id;

    bool check = false;
    while (!check) {
        check = true;

        for (size_t i = 0; i < session->length; ++i) {
            char *id = generate_radio_id();
            if (strcmp(session->radios[i].id, id) == 0) {
                check = false;
                break;
            }
        }
    }

    radio->id = id;

    session->length += 1;
    session->radios = realloc(sessions->radios, sessions->length * sizeof(radio_t));
    if (!session->radios) {
        session->active = false;
        syserr("realloc(): lost all radios");
        return 1;
    }

    session->radios[session->length - 1] = radio;
    return 0;
}

radio_t* get_radio_by_id(session_t session, char* id)
{
    radio_t *radio = NULL;
    for (size_t i = 0; i < session->length; ++i) {
        if (strcmp(session->radios[i].id, id) == 0) {
            radio = &session->radios[i];
            break;
        }
    }

    return radio;
}


void parse_and_action(session_t *session, char* buffer, size_t end)
{
    size_t start = 0;
    while ((start < end) && (isspace(buffer[start]) || buffer[start] == '\r' || buffer[start] == '\n'))
        start++;

    while ((end > 0) && (isspace(buffer[end-1]) || buffer[end-1] == '\r' || buffer[end-1] == '\n'))
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


        radio_t *radio = add_radio(session, &host, listen_port);

        pid_t pid = fork();
        if (pid == 0) {
            // fd_in = open("td_in", O_RDWR | O_NONBLOCK);
            // dup2(fd_in, 0);
            // close(fd_in);
            // fd_out = open("td_out", O_RDWR | O_NONBLOCK);
            // dup2(fd_out, 1);
            // close(fd_out);

            int err = execlp("ssh", "-t", computer, "./player", host, path, resource_port, file, listen_port, meta_data, NULL);
            if (err < 0) {
                send_return_message(session, "SSH failed");
                return;
            }
        }

        char msg[100];
        strcpy(msg, "OK ");
        strcat(msg, radio->id);

        send_return_message(session, &msg);
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

        bool sent = send_radio_command(radio, &command);
        if (!sent) {
            // TODO:
            // delete radio
            // send
        }

        char msg[6000];
        strcpy(msg, "OK ");
        strcat(msg, radio->id);

        if (strcmp(command, "TITLE") == 0) {
            char buffer[5000];
            sent = recv_radio_response(radio, &buffer);
            if (sent) {
                // TODO:
                // delete radio
                // send
            }

            strcat(msg, buffer);
        }

        send_session_message(session, msg);
        return;
    }
}
