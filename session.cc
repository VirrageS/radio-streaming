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

#include <algorithm>

#include "session.h"

class RadioNotFoundException: public std::exception
{
    virtual const char* what() const throw() {
        return "Radio has not been found";
    }
};

static std::string generate_id()
{
    std::string alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::string id;
    for (size_t i = 0; i < 40; ++i) {
        id += alphanum[rand() % (alphanum.length() - 1)];
    }

    return id;
}


/**
********************
***** RADIO *****
********************
**/

Radio::Radio()
{
    m_port = 0;
    m_playerPort = 0;
    m_hour = 0;
    m_minute = 0;
    m_interval = 0;

    m_playerStderr = -1;
    m_active = false;
}

Radio::Radio(const char *host, unsigned long port, unsigned short hour,
             unsigned short minute, unsigned int interval, const char *player_host,
             const char *player_path, unsigned long player_port, const char *player_file,
             const char *player_md) : Radio()
{
    m_host = std::string(host);
    m_port = port;
    m_hour = hour;
    m_minute = minute;
    m_interval = interval;
    m_playerHost = std::string(player_host);
    m_playerPath = std::string(player_path);
    m_playerPort = player_port;
    m_playerFile = std::string(player_file);
    m_playerMeta = std::string(player_md);
}

Radio::~Radio()
{
    if (m_active)
        send_radio_command("QUIT");

    close(m_playerStderr);
}

std::string Radio::to_string()
{
    std::string radio = "";
    radio = radio + "##################################" + "\n";
    radio += "id\t: " + m_id + "\n";
    radio += "host\t: " + m_host + "\n";
    radio += "port\t: " + std::to_string(m_port) + "\n";
    radio += "hour\t: " + std::to_string(m_hour) + "\n";
    radio += "minute\t: " + std::to_string(m_minute) + "\n";
    radio += "interval\t: " + std::to_string(m_interval) + "\n";

    radio += "player-host\t: " + m_playerHost + "\n";
    radio += "player-path\t: " + m_playerPath + "\n";
    radio += "player-port\t: " + std::to_string(m_playerPort) + "\n";
    radio += "player-file\t: " + m_playerFile + "\n";
    radio += "player-md\t: " + m_playerMeta + "\n";
    radio += "player-err\t: " + std::to_string(m_playerStderr) + "\n";
    radio += "started\t: " + std::to_string(m_active) + "\n";
    radio = radio + "##################################" + "\n";

    return radio;
}


bool Radio::start_radio()
{
    int err_pipe[2];
    if (pipe(err_pipe) < 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return false;
    } else if (pid == 0) {
        close(err_pipe[0]);
        dup2(err_pipe[1], 2);

        std::string player = "bash -l -c 'player " + m_playerHost + " " + m_playerPath + " " + std::to_string(m_playerPort) + " " + m_playerFile + " " + std::to_string(m_port) + " " + m_playerMeta.c_str() + "'";
        int err = execlp("ssh", "ssh", m_host.c_str(), player.c_str(), NULL);

        if (err < 0) {
            char msg[] = "SSH failed\n";
            write(err_pipe[1], msg, sizeof(msg));
        }

        exit(0);
    } else {
        close(err_pipe[1]);
        m_playerStderr = err_pipe[0];
    }

    m_active = true;
    return true;
}


std::pair<bool, int> Radio::send_radio_command(const std::string& message)
{
    // player is not active yet... or anymore
    if (!m_active) {
        return std::make_pair(false, 0);
    }

    auto msg = message.c_str();

    struct hostent *server = (struct hostent *)gethostbyname(m_host.c_str());
    if (!server)
        return std::make_pair(false, 0);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return std::make_pair(false, 0);

    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(m_port);
    server_address.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(server_address.sin_zero), 8);

    ssize_t bytes_send = sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&server_address, (socklen_t)sizeof(server_address));
    if (bytes_send <= 0) {
        return std::make_pair(false, 0);
    }

    return std::make_pair(true, sock);
}


std::pair<bool, std::string> Radio::recv_radio_response(int socket)
{
    std::string response = "";

    struct hostent *server = (struct hostent *)gethostbyname(m_host.c_str());
    if (!server)
        return std::make_pair(false, "");

    struct sockaddr_in server_address;
    int server_len = sizeof(server_address);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(m_port);
    server_address.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(server_address.sin_zero), 8);

    struct pollfd poll_socket[1];
    poll_socket[0].fd = socket;
    poll_socket[0].events = POLLIN | POLLHUP;

    int err = poll(poll_socket, 1, 5000);
    if (err <= 0) {
        return std::make_pair(false, "");
    } else {
        char buffer[5000];

        ssize_t bytes_recieved = recvfrom(socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_address, (socklen_t *)&server_len);
        if (bytes_recieved <= 0)
            return std::make_pair(false, "");

        response = std::string(buffer);
    }

    return std::make_pair(true, response);
}


/**
********************
***** SESSION *****
********************
**/

void Session::parse(std::string message)
{
    while ((message.length() > 0) && (isspace(message.front()) || (message.front() == '\r') || (message.front() == '\n')))
        message.erase(message.begin());

    while ((message.length() > 0) && (isspace(message.back()) || (message.back() == '\r') || (message.back() == '\n')))
        message.pop_back();

    int items;

    // PARSE COMMAND
    char command[10], hour[3], minute[3], computer[4096], host[4096], path[4096], file[256], meta_data[4], id[256];
    int interval, resource_port, listen_port;

    // "START" COMMAND
    items = sscanf(message.c_str(), "%9s %4095s %4095s %4095s %d %255s %d %3s", command, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 8) {
        if (strcmp(command, "START") != 0) {
            send_session_message("ERROR: Invalid START command");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            send_session_message("ERROR: Invalid meta data parameter [yes / no]");
            return;
        }

        if ((listen_port <= 0) || (resource_port <= 0) || (listen_port > 65538) || (resource_port > 65538)) {
            send_session_message("ERROR: Invalid port");
            return;
        }

        auto radio = add_radio(computer, listen_port, 0, 0, 0, host, path, resource_port, file, meta_data);

        bool started = radio->start_radio();
        if (!started) {
            remove_radio_by_id(radio->id());
            send_session_message("ERROR: ssh failed");
            return;
        }

        debug_print("%s\n", radio->to_string().c_str());

        bool added = add_poll_fd(radio->player_stderr());
        if (!added) {
            remove_radio_by_id(radio->id());
            send_session_message("ERROR: could not handle descriptor");
        }

        send_session_message("OK " + radio->id());
        return;
    }

    // "AT" COMMAND
    items = sscanf(message.c_str(), "%9s %2s.%2s %d %4095s %4095s %4095s %d %255s %d %3s", command, hour, minute, &interval, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 11) {
        if (strcmp(command, "AT") != 0) {
            send_session_message("ERROR: Invalid AT command");
            return;
        }

        if ((strlen(hour) != 2) || (strlen(minute) != 2)) {
            send_session_message("ERROR: Invalid start hour or minute");
            return;
        }

        short ihour = (((int)hour[0] - 48) * 10) + ((int)hour[1] - 48);
        short iminute = (((int)minute[0] - 48) * 10) + ((int)minute[1] - 48);
        if ((ihour < 0) || (ihour >= 24) || (iminute < 0) || (iminute >= 60)) {
            send_session_message("ERROR: Invalid start hour or minute");
            return;
        }

        if (interval <= 0) {
            send_session_message("ERROR: Invalid interval parameter");
            return;
        }

        if (strcmp(file, "-") == 0) {
            send_session_message("ERROR: Invalid file name");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            send_session_message("ERROR: Invalid meta data parameter [yes / no]");
            return;
        }

        if ((listen_port <= 0) || (resource_port <= 0) || (listen_port > 65538) || (resource_port > 65538)) {
            send_session_message("ERROR: Invalid port");
            return;
        }

        auto radio = add_radio(computer, listen_port, ihour, iminute, interval, host, path, resource_port, file, meta_data);
        debug_print("%s\n", radio->to_string().c_str());

        time_t current_time = time(NULL);
        auto t = localtime(&current_time);

        if ((ihour < t->tm_hour) || ((ihour == t->tm_hour) && (iminute < t->tm_min))) {
            current_time += 24 * 3600;
        }

        current_time += (ihour - t->tm_hour) * 3600;
        current_time += (iminute - t->tm_min) * 60;
        current_time -= t->tm_sec;

        debug_print("current %ld; starts at: %ld\n", time(NULL), current_time);

        Event event;
        event.radio_id = radio->id();
        event.action = START_RADIO;
        event.event_time = current_time;

        m_events.push(event);

        event.action = SEND_QUIT;
        event.event_time = current_time + (interval * 60);

        m_events.push(event);

        std::string msg = "OK " + radio->id() + "\n";
        send_session_message(msg);
        return;
    }

    // PLAYER COMMANDS
    items = sscanf(message.c_str(), "%9s %255s", command, id);
    if (items == 2) {
        // check if any command matches
        if ((strcmp(command, "PAUSE") != 0) &&
            (strcmp(command, "PLAY") != 0) &&
            (strcmp(command, "TITLE") != 0) &&
            (strcmp(command, "QUIT") != 0)) {
            send_session_message("ERROR: Invalid command");
            return;
        }

        try {
            auto radio = get_radio_by_id(id);

            auto sent = radio->send_radio_command(std::string(command));
            if (!sent.first) {
                send_session_message("ERROR " + radio->id() + ": could not send command to player. Probably not reachable.");
                return;
            }

            std::string msg = "OK " + radio->id();

            if (strcmp(command, "TITLE") == 0) {
                auto received = radio->recv_radio_response(sent.second);

                if (!received.first) {
                    send_session_message("ERROR " + radio->id() + ": title command timeout");
                    return;
                }

                msg += " ";
                msg += received.second;
            }

            if (strcmp(command, "QUIT") == 0) {
                remove_radio_by_id(radio->id());
            }

            send_session_message(msg);
        } catch (const std::exception& e) {
            send_session_message("ERROR: Player with provided [id] does not exists");
            return;
        }

        return;
    }

    send_session_message("ERROR: Invalid command");
    return;
}


std::shared_ptr<Radio> Session::add_radio(const char *host, unsigned long port,
    unsigned short hour, unsigned short minute, unsigned int interval,
    const char *player_host, const char *player_path, unsigned long player_port,
    const char *player_file, const char *player_md)
{
    bool check = false;

    std::shared_ptr<Radio> radio = std::make_shared<Radio>(
        host, port, hour, minute, interval, player_host, player_path,
        player_port, player_file, player_md
    );

    while (!check) {
        check = true;
        radio->id(generate_id());

        for (auto r : m_radios) {
            if (r->id() == radio->id()) {
                check = false;
                break;
            }
        }
    }

    m_radios.push_back(radio);
    return m_radios.back();
}


std::shared_ptr<Radio> Session::get_radio_by_id(const std::string& id)
{
    for (auto radio : m_radios) {
        if (radio->id() == id) {
            return radio;
        }
    }

    throw RadioNotFoundException();
}


void Session::remove_radio_by_id(const std::string& id)
{

    for (auto it = m_radios.begin(); it != m_radios.end(); ++it) {
        if (it->get()->id() == id) {
            // remove stderr socket from poll sockets (if exists)
            for (auto itt = m_pollSockets.begin(); itt != m_pollSockets.end(); ++itt) {
                if (it->get()->player_stderr() == itt->fd) {
                    m_pollSockets.erase(itt);
                    break;
                }
            }

            it->get()->active(false);
            m_radios.erase(it);
            break;
        }
    }

    // remove all events which points to removed radio
    std::priority_queue<Event> tmp_events;
    for (; !m_events.empty(); m_events.pop()) {
        auto event = m_events.top();

        if (event.radio_id != id)
            tmp_events.push(event);
    }

    m_events = tmp_events;
}


bool Session::send_session_message(const std::string& message) const
{
    std::string full_message = message + "\r\n";
    auto msg = full_message.c_str();

    size_t to_sent = 0;
    while (to_sent != strlen(msg)) {
        ssize_t bytes_send = send(m_socket, &msg[to_sent], strlen(msg) - to_sent, 0);
        if (bytes_send < 0) {
            if (errno != EWOULDBLOCK)
                return false;
        } else if (bytes_send == 0) {
            return false;
        } else {
            to_sent += bytes_send;
        }
    }

    return true;
}


bool Session::add_poll_fd(int socket)
{
    // make socket nonblocking
    int err = fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
    if (err < 0) {
        return false;
    }

    pollfd fd;
    fd.fd = socket;
    fd.events = POLLIN;

    m_pollSockets.push_back(fd);

    return true;
}


unsigned int Session::get_timeout() const
{
    if (m_events.empty()) {
        return 1000000; // 10 ^ 6 seconds (this should be enough...)
    }

    auto event = m_events.top();
    time_t current_time = time(NULL);

    return std::max((long)0, (long)(event.event_time - current_time));
}


void Session::handle_timeout()
{
    if (m_events.empty())
        return;

    auto event = m_events.top();
    m_events.pop();

    try {
        auto radio = get_radio_by_id(event.radio_id);

        if (event.action == START_RADIO) {
            bool started = radio->start_radio();
            if (!started) {
                remove_radio_by_id(radio->id());
                send_session_message("ERROR: ssh failed");
                return;
            }

            bool added = add_poll_fd(radio->player_stderr());
            if (!added) {
                remove_radio_by_id(radio->id());
                send_session_message("ERROR: could not handle descriptor");
                return;
            }
        } else if (event.action == SEND_QUIT) {
            radio->send_radio_command("QUIT");
            remove_radio_by_id(radio->id());
        }
    } catch (std::exception& e) {
        // radio does not exists
        return;
    }
}


/**
********************
***** SESSIONS *****
********************
**/


std::shared_ptr<Session> Sessions::add_session()
{
    m_mutex.lock();

    std::shared_ptr<Session> session = std::make_shared<Session>();

    bool check = false;
    while (!check) {
        check = true;
        session->id(generate_id());

        // check if session with generated id already exists
        for (auto s : m_sessions) {
            if (s->id() == session->id()) {
                check = false;
                break;
            }
        }
    }


    m_sessions.push_back(session);
    auto s = m_sessions.back();

    m_mutex.unlock();
    return s;
}


std::shared_ptr<Session> Sessions::add_session(int socket)
{
    m_mutex.lock();

    std::shared_ptr<Session> session = std::make_shared<Session>(socket);

    bool check = false;
    while (!check) {
        check = true;
        session->id(generate_id());

        for (auto s : m_sessions) {
            if (s->id() == session->id()) {
                check = false;
                break;
            }
        }
    }


    m_sessions.push_back(session);
    auto s = m_sessions.back();

    m_mutex.unlock();
    return s;
}

void Sessions::remove_session_by_id(const std::string& id)
{
    m_mutex.lock();

   for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
       if (it->get()->id() == id) {
           m_sessions.erase(it);
           break;
       }
   }

   m_mutex.unlock();
}
