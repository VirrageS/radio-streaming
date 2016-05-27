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
}

Radio::Radio(const char *host, unsigned long port, unsigned short hour,
             unsigned short minute, unsigned int interval, const char *player_host,
             const char *player_path, unsigned long player_port, const char *player_file,
             const char *player_md)
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
    send_radio_command("QUIT");
    close(m_playerStderr);
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

        std::string player_port = std::to_string(m_playerPort);
        std::string port = std::to_string(m_port);

        int err = execlp(
            "ssh", "ssh", m_host.c_str(),
            "./player",
                m_playerHost.c_str(),
                m_playerPath.c_str(),
                player_port.c_str(),
                m_playerFile.c_str(),
                port.c_str(),
                m_playerMeta.c_str(),
            NULL
        );

        if (err < 0) {
            char msg[] = "SSH failed\n";
            write(err_pipe[1], msg, sizeof(msg));
        }

        exit(0);
    } else {
        close(err_pipe[1]);
        m_playerStderr = err_pipe[0];
    }

    return true;
}


bool Radio::send_radio_command(std::string message)
{
    auto msg = message.c_str();

    struct hostent *server = (struct hostent *)gethostbyname(m_host.c_str());
    if (!server)
        return false;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return false;

    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(m_port);
    server_address.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(server_address.sin_zero), 8);

    ssize_t bytes_send = sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&server_address, (socklen_t)sizeof(server_address));
    if (bytes_send <= 0) {
        return false;
    }

    return true;
}


bool Radio::recv_radio_response(char *buffer)
{
    struct hostent *server = (struct hostent *)gethostbyname(m_host.c_str());
    if (!server)
        return false;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return false;

    struct sockaddr_in server_address;
    int server_len = sizeof(server_address);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(m_port);
    server_address.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(server_address.sin_zero), 8);

    ssize_t bytes_recieved = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_address, (socklen_t *)&server_len);
    if (bytes_recieved <= 0) {
        return false;
    }

    return true;
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
    unsigned short resource_port, listen_port;
    unsigned int interval;

    // "START" COMMAND
    items = sscanf(message.c_str(), "%9s %4095s %4095s %4095s %hu %255s %hu %3s", command, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 8) {
        if (strcmp(command, "START") != 0) {
            send_session_message("ERROR: Invalid START command\n");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            send_session_message("ERROR: Invalid meta data parameter [yes / no]\n");
            return;
        }

        auto radio = add_radio(computer, listen_port, 0, 0, 0, host, path, resource_port, file, meta_data);

        bool started = radio->start_radio();
        if (!started) {
            remove_radio_by_id(radio->id());
            send_session_message("ERROR: ssh failed\n");
            return;
        }

        radio->print_radio();

        bool added = add_poll_fd(radio->player_stderr());
        if (!added) {
            remove_radio_by_id(radio->id());
            send_session_message("ERROR: could not handle descriptor\n");
        }

        std::string msg = "OK " + radio->id() + "\n";
        send_session_message(msg);
        return;
    }

    // "AT" COMMAND
    items = sscanf(message.c_str(), "%9s %2s:%2s %u %4095s %4095s %4095s %hu %255s %hu %3s", command, hour, minute, &interval, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 11) {
        if (strcmp(command, "AT") != 0) {
            send_session_message("ERROR: Invalid AT command\n");
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

        auto radio = add_radio(computer, listen_port, ihour, iminute, interval, host, path, resource_port, file, meta_data);
        radio->print_radio();

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
        event.radio_id = radio->id();
        event.action = START_RADIO;
        event.event_time = current_time;

        m_events.push(event);

        event.action = SEND_QUIT;
        event.event_time = current_time + interval * 60;

        m_events.push(event);

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
            send_session_message("ERROR: Invalid command\n");
            return;
        }

        try {
            auto radio = get_radio_by_id(id);

            bool sent = radio->send_radio_command(std::string(command));
            if (!sent) {
                send_session_message("ERROR " + radio->id() + ": could not send command to player. Probably not reachable.\n");
                remove_radio_by_id(radio->id());
                return;
            }

            std::string msg = "OK " + radio->id();
            if (strcmp(command, "TITLE") == 0) {
                char buffer[5000];
                bool received = radio->recv_radio_response(buffer);

                if (!received) {
                    send_session_message("ERROR " + radio->id() + ": could not reach player\n");
                    remove_radio_by_id(radio->id());
                    return;
                }

                msg.append(buffer);
            }

            if (strcmp(command, "QUIT") == 0) {
                remove_radio_by_id(radio->id());
            }

            msg += "\n";
            send_session_message(msg);
        } catch (const std::exception& e) {
            send_session_message("ERROR: Player with provided [id] does not exists\n");
            return;
        }

        return;
    }

    send_session_message("ERROR: Invalid command\n");
    return;
}


std::shared_ptr<Radio> Session::add_radio(const char *host, unsigned long port,
                          unsigned short hour, unsigned short minute,
                          unsigned int interval, const char *player_host,
                          const char *player_path, unsigned long player_port,
                          const char *player_file, const char *player_md)
{
    bool check = false;

    std::shared_ptr<Radio> radio = std::make_shared<Radio>(
        host, port, hour, minute, interval, player_host, player_path,
        player_port, player_file, player_md);

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

    throw RadioNotFound;
}


void Session::remove_radio_by_id(const std::string& id)
{

    for (auto it = m_radios.begin(); it != m_radios.end(); ++it) {
        if (it->get()->id() == id) {
            // remove stderr socket from poll sockets (if exists)
            for (auto itt = m_pollSockets.begin(); itt != m_pollSockets.end(); ++itt) {
                if (it->get()->player_stderr() == itt->fd) {
                    std::cerr << id << " - " << it->get()->player_stderr() << std::endl;
                    m_pollSockets.erase(itt);
                    break;
                }
            }

            m_radios.erase(it);
            break;
        }
    }

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
    auto msg = message.c_str();

    size_t to_sent = 0;
    while (true) {
        ssize_t bytes_send = send(m_socket, &msg[to_sent], strlen(msg) - to_sent, 0);
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


int Session::get_timeout() const
{
    if (m_events.empty())
        return 1000000; // 10 ^ 6

    auto event = m_events.top();
    time_t current_time = time(NULL);

    return event.event_time - current_time;
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
            radio->start_radio();
            bool added = add_poll_fd(radio->player_stderr());
            if (!added) {
                remove_radio_by_id(radio->id());
                send_session_message("ERROR: could not handle descriptor\n");
            }
        } else if (event.action == SEND_QUIT) {
            radio->send_radio_command("QUIT");
        }
    } catch (std::exception& e) {
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
