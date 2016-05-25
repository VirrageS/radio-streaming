#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include "session.h"

class RadioNotFoundException: public std::exception
{
    virtual const char* what() const throw() {
        return "Radio has not been found";
    }
} RadioNotFound;

static std::string generate_id()
{
    std::string alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::string id;
    for (size_t i = 0; i < 40; ++i) {
        id += alphanum[rand() % (alphanum.length() - 1)];
    }

    return id;
}

void Session::remove_radio_with_id(std::string id)
{
    pthread_mutex_lock(&mutex);

    for (auto it = radios.begin(); it != radios.end(); ++it) {
        if (it->id == id) {
            radios.erase(it);
            break;
        }
    }

    std::priority_queue<Event> tmp_events;
    for (; !events.empty(); events.pop()) {
        auto event = events.top();
        if (event.radio_id != id)
            tmp_events.push(event);
    }

    events = tmp_events;
    pthread_mutex_unlock(&mutex);
}

bool Session::send_session_message(std::string message)
{
    char msg[1024];
    strcpy(msg, message.c_str());

    size_t to_sent = 0;
    while (true) {
        ssize_t bytes_send = send(socket, &msg[to_sent], strlen(msg) - to_sent, 0);
        if (bytes_send < 0) {
            if (errno != EWOULDBLOCK)
                return false;
        } else if (bytes_send == 0) {
            return false;
        } else {
            to_sent += bytes_send;
        }

        if (to_sent == strlen(msg))
            break;
    }

    return true;
}

Session& Sessions::add_session() {
    pthread_mutex_lock(&mutex);

    Session session;

    bool check = false;
    while (!check) {
        check = true;
        session.id = generate_id();

        for (Session s : sessions) {
            if (s.id == session.id) {
                check = false;
                break;
            }
        }
    }


    sessions.push_back(session);

    Session& s = sessions.back();
    pthread_mutex_unlock(&mutex);

    return s;
}

bool Radio::send_radio_command(std::string message)
{
    char msg[1024];
    strcpy(msg, message.c_str());

    struct hostent *found_host = (struct hostent *)gethostbyname(host);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return false;

    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr = *((struct in_addr *)found_host->h_addr);

    ssize_t bytes_send = sendto(sock, &msg, strlen(msg), 0, (struct sockaddr *)&server_address, (socklen_t)sizeof(server_address));
    if (bytes_send <= 0) {
        return false;
    }

    return true;
}

bool Radio::recv_radio_response(char *buffer)
{
    struct hostent *found_host = (struct hostent *)gethostbyname(host);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return false;

    struct sockaddr_in server_address;
    int server_len = sizeof(server_address);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr = *((struct in_addr *)found_host->h_addr);
    bzero(&(server_address.sin_zero), 8);

    ssize_t bytes_recieved = recvfrom(sock, &buffer, sizeof(buffer), 0, (struct sockaddr *)&server_address, (socklen_t *)&server_len);
    if (bytes_recieved <= 0) {
        return false;
    }

    return true;
}

Radio& Session::add_radio(char *host, unsigned long port,
                          unsigned short hour, unsigned short minute, unsigned int interval,
                          char *player_host, char *player_path, unsigned long player_port, char *player_file, char *player_md)
{
    Radio radio;
    bool check = false;

    while (!check) {
        check = true;
        radio.id = generate_id();

        for (Radio r : radios) {
            if (r.id == radio.id) {
                check = false;
                break;
            }
        }
    }

    radio.host = (char *)strdup(host);
    radio.port = port;
    radio.hour = hour;
    radio.minute = minute;
    radio.interval = interval;
    radio.player_host = (char *)strdup(player_host);
    radio.player_path = (char *)strdup(player_path);
    radio.player_port = player_port;
    radio.player_file = (char *)strdup(player_file);
    radio.player_md = (char *)strdup(player_md);

    radios.push_back(radio);
    return radios.back();
}

Radio& Session::get_radio_by_id(std::string id)
{
    for (Radio& radio : radios) {
        if (radio.id == id)
            return radio;
    }

    throw RadioNotFound;
}





bool Radio::start_radio()
{
    int err_pipe[2];
    if (pipe(err_pipe) < 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(err_pipe[0]);
        dup2(err_pipe[1], 2);

        // TODO: all have to be chars
        int err = execlp(
            "ssh", "ssh", host,
            "./player",
                player_host,
                player_path,
                player_port,
                player_file,
                port,
                player_md,
            NULL
        );

        if (err < 0) {
            char msg[] = "SSH failed \n";
            write(err_pipe[1], msg, sizeof(msg));
        }

        exit(0);
    } else {
        close(err_pipe[1]);
        player_stderr = err_pipe[0];
    }

    return true;
}


int Session::get_timeout()
{
    if (events.empty())
        return 1000000; // 10 ^ 6

    auto event = events.top();
    time_t current_time = time(NULL);

    return event.event_time - current_time;
}


void Session::handle_timeout()
{
    if (events.empty())
        return;

    auto event = events.top();
    events.pop();

    try {
        Radio& radio = get_radio_by_id(event.radio_id);

        switch (event.action) {
            case START_RADIO:
                radio.start_radio();
                add_poll_fd(radio.player_stderr);
                break;
            case SEND_QUIT:
                radio.send_radio_command("QUIT");
                break;
            default:
                break;
        }
    } catch (std::exception& e) {
        return;
    }
}

void Session::add_poll_fd(int socket)
{
    // make socket nonblocking
    int err = fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
    if (err < 0) {
        return;
    }

    pollfd fd;
    fd.fd = socket;
    fd.events = POLLIN | POLLHUP;

    poll_sockets.push_back(fd);
}

void Session::parse_and_action(char* buffer, size_t end)
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
            send_session_message("ERROR: Invalid command\n");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            send_session_message("ERROR: Invalid meta data parameter [yes / no]\n");
            return;
        }

        Radio& radio = add_radio(computer, listen_port, 0, 0, 0, host, path, resource_port, file, meta_data);

        bool started = radio.start_radio();
        if (!started) {
            remove_radio_with_id(radio.id);
            send_session_message("ERROR: ssh failed\n");
            return;
        }

        add_poll_fd(radio.player_stderr);

        std::string msg = "OK " + radio.id + "\n";
        send_session_message(msg);
        return;
    }

    // "AT" COMMAND
    items = sscanf(buffer, "%s %s:%s %u %s %s %s %hu %s %hu %s", command, hour, minute, &interval, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 11) {
        if (strcmp(command, "AT") != 0) {
            send_session_message("ERROR: Invalid command\n");
            return;
        }

        if ((strlen(hour) != 2) || (strlen(minute) != 2)) {
            send_session_message("ERROR: Invalid start hour or minute\n");
            return;
        }

        short ihour = ((int)hour[0] * 10) + (int)hour[1];
        short iminute = ((int)minute[0] * 10) + (int)minute[1];
        if ((ihour < 0) || (ihour > 24) || (iminute < 0) || (iminute >= 60)) {
            send_session_message("ERROR: Invalid start hour or minute\n");
            return;
        }

        if (strcmp(file, "-") == 0) {
            send_session_message("ERROR: Invalid file name\n");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            send_session_message("ERROR: Invalid meta data parameter [yes / no]\n");
            return;
        }


        Radio& radio = add_radio(computer, listen_port, ihour, iminute, interval, host, path, resource_port, file, meta_data);

        time_t current_time = time(NULL);
        auto t = localtime(&current_time);

        if (ihour < t->tm_hour) {
            current_time += (24 - t->tm_hour + ihour) * 3600;
        } else {
            current_time += (ihour - t->tm_hour) * 3600;
        }

        if ((ihour == t->tm_hour) && (iminute < t->tm_min)) {
            current_time += 24 * 3600;
        }

        current_time += (t->tm_min - iminute) * 60;

        Event event;
        event.radio_id = radio.id;
        event.action = START_RADIO;
        event.event_time = current_time;

        events.push(event);

        event.action = SEND_QUIT;
        event.event_time = current_time + interval * 60;

        events.push(event);

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
            send_session_message("ERROR: Invalid command\n");
            return;
        }

        try {
            Radio& radio = get_radio_by_id(id);

            std::string msg = "OK " + radio.id;
            if (strcmp(command, "TITLE") == 0) {
                char buffer[5000];
                bool received = radio.recv_radio_response(buffer);

                if (!received) {
                    send_session_message("ERROR: could not reach player\n");//, id);
                    remove_radio_with_id(radio.id);
                    return;
                }

                msg.append(buffer);
            }

            msg += "\n";
            send_session_message(msg);
        } catch (const std::exception& e) {
            send_session_message("ERROR: Player with provided 'id' does not exists\n");
            return;
        }

        return;
    }
}
