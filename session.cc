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

uint64_t Session::current_id_ = 0;


/**
********************
***** RADIO *****
********************
**/

Radio::Radio()
{
    port_ = 0;
    player_port_ = 0;
    hour_ = 0;
    minute_ = 0;
    interval_ = 0;

    player_stderr_ = -1;
    active_ = false;
}

Radio::Radio(uint64_t id, const char *host, unsigned long port, unsigned short hour,
             unsigned short minute, unsigned int interval, const char *player_host,
             const char *player_path, unsigned long player_port, const char *player_file,
             const char *player_md) : Radio()
{
    id_ = id;
    host_ = std::string(host);
    port_ = port;
    hour_ = hour;
    minute_ = minute;
    interval_ = interval;
    player_host_ = std::string(player_host);
    player_path_ = std::string(player_path);
    player_port_ = player_port;
    player_file_ = std::string(player_file);
    player_metadata_ = std::string(player_md);
}

Radio::~Radio()
{
    if (active_)
        SendCommand("QUIT");

    close(player_stderr_);
}

std::string Radio::to_string() const
{
    std::string radio = "";
    radio = radio + "##################################" + "\n";
    radio += "id\t: " + std::to_string(id_) + "\n";
    radio += "host\t: " + host_ + "\n";
    radio += "port\t: " + std::to_string(port_) + "\n";
    radio += "hour\t: " + std::to_string(hour_) + "\n";
    radio += "minute\t: " + std::to_string(minute_) + "\n";
    radio += "interval\t: " + std::to_string(interval_) + "\n";

    radio += "player-host\t: " + player_host_ + "\n";
    radio += "player-path\t: " + player_path_ + "\n";
    radio += "player-port\t: " + std::to_string(player_port_) + "\n";
    radio += "player-file\t: " + player_file_ + "\n";
    radio += "player-md\t: " + player_metadata_ + "\n";
    radio += "player-err\t: " + std::to_string(player_stderr_) + "\n";
    radio += "started\t: " + std::to_string(active_) + "\n";
    radio = radio + "##################################" + "\n";

    return radio;
}


bool Radio::StartRadio()
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

        std::string player = "bash -l -c 'player " + player_host_ + " " + player_path_ + " " + std::to_string(player_port_) + " " + player_file_ + " " + std::to_string(port_) + " " + player_metadata_.c_str() + "'";
        int err = execlp("ssh", "ssh", host_.c_str(), player.c_str(), NULL);

        if (err < 0) {
            char msg[] = "SSH failed\n";
            write(err_pipe[1], msg, sizeof(msg));
        }

        exit(0);
    } else {
        close(err_pipe[1]);
        player_stderr_ = err_pipe[0];
    }

    active_ = true;
    return true;
}


std::pair<bool, int> Radio::SendCommand(const std::string& command)
{
    // player is not active yet... or anymore
    if (!active_) {
        return std::make_pair(false, 0);
    }

    struct hostent *server = (struct hostent *)gethostbyname(host_.c_str());
    if (!server)
        return std::make_pair(false, 0);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return std::make_pair(false, 0);

    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_);
    server_address.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(server_address.sin_zero), 8);

    ssize_t bytes_send = sendto(sock, command.c_str(), command.length(), 0, (struct sockaddr *)&server_address, (socklen_t)sizeof(server_address));
    if (bytes_send <= 0) {
        return std::make_pair(false, 0);
    }

    return std::make_pair(true, sock);
}


std::pair<bool, std::string> Radio::ReceiveResponse(int socket)
{
    std::string response = "";

    struct hostent *server = (struct hostent *)gethostbyname(host_.c_str());
    if (!server)
        return std::make_pair(false, "");

    struct sockaddr_in server_address;
    int server_len = sizeof(server_address);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_);
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

Session::Session(uint64_t id)
  : buffer(),
    id_(id),
    socket_(-1)
{
}

Session::Session(uint64_t id, int socket)
  : buffer(),
    id_(id),
    socket_(socket)
{
}

Session::~Session()
{
    for (auto radio : radios_) {
        radio->SendCommand("QUIT");
        RemoveRadioById(radio->id());
    }

    close(socket_);
}

void Session::Parse(std::string message)
{
    while ((message.length() > 0) && (isspace(message.front()) || (message.front() == '\r') || (message.front() == '\n')))
        message.erase(message.begin());

    while ((message.length() > 0) && (isspace(message.back()) || (message.back() == '\r') || (message.back() == '\n')))
        message.pop_back();

    int items;

    // PARSE COMMAND
    uint64_t id;
    char command[10], hour[3], minute[3], computer[4096], host[4096], path[4096], file[256], meta_data[4];
    int interval, resource_port, listen_port;

    // "START" COMMAND
    items = sscanf(message.c_str(), "%9s %4095s %4095s %4095s %d %255s %d %3s", command, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 8) {
        if (strcmp(command, "START") != 0) {
            SendResponse("ERROR: Invalid START command");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            SendResponse("ERROR: Invalid meta data parameter [yes / no]");
            return;
        }

        if ((listen_port <= 0) || (resource_port <= 0) || (listen_port > 65538) || (resource_port > 65538)) {
            SendResponse("ERROR: Invalid port");
            return;
        }

        auto radio = AddRadio(current_id_++, computer, listen_port, 0, 0, 0, host, path, resource_port, file, meta_data);

        bool started = radio->StartRadio();
        if (!started) {
            RemoveRadioById(radio->id());
            SendResponse("ERROR: ssh failed");
            return;
        }

        debug_print("%s\n", radio->to_string().c_str());

        bool added = AddFileDescriptor(radio->player_stderr());
        if (!added) {
            RemoveRadioById(radio->id());
            SendResponse("ERROR: could not handle descriptor");
        }

        SendResponse("OK " + std::to_string(radio->id()));
        return;
    }

    // "AT" COMMAND
    items = sscanf(message.c_str(), "%9s %2s.%2s %d %4095s %4095s %4095s %d %255s %d %3s", command, hour, minute, &interval, computer, host, path, &resource_port, file, &listen_port, meta_data);
    if (items == 11) {
        if (strcmp(command, "AT") != 0) {
            SendResponse("ERROR: Invalid AT command");
            return;
        }

        if ((strlen(hour) != 2) || (strlen(minute) != 2)) {
            SendResponse("ERROR: Invalid start hour or minute");
            return;
        }

        short ihour = (((int)hour[0] - 48) * 10) + ((int)hour[1] - 48);
        short iminute = (((int)minute[0] - 48) * 10) + ((int)minute[1] - 48);
        if ((ihour < 0) || (ihour >= 24) || (iminute < 0) || (iminute >= 60)) {
            SendResponse("ERROR: Invalid start hour or minute");
            return;
        }

        if (interval <= 0) {
            SendResponse("ERROR: Invalid interval parameter");
            return;
        }

        if (strcmp(file, "-") == 0) {
            SendResponse("ERROR: Invalid file name");
            return;
        }

        if ((strcmp(meta_data, "yes") != 0) && (strcmp(meta_data, "no") != 0)) {
            SendResponse("ERROR: Invalid meta data parameter [yes / no]");
            return;
        }

        if ((listen_port <= 0) || (resource_port <= 0) || (listen_port > 65538) || (resource_port > 65538)) {
            SendResponse("ERROR: Invalid port");
            return;
        }

        auto radio = AddRadio(current_id_++, computer, listen_port, ihour, iminute, interval, host, path, resource_port, file, meta_data);
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

        events_.push({radio->id(), START_RADIO, current_time});
        events_.push({radio->id(), SEND_QUIT, current_time + (interval * 60)});

        std::string msg = "OK " + std::to_string(radio->id()) + "\n";
        SendResponse(msg);
        return;
    }

    // PLAYER COMMANDS
    items = sscanf(message.c_str(), "%9s %lld", command, &id);
    if (items == 2) {
        // check if any command matches
        if ((strcmp(command, "PAUSE") != 0) &&
            (strcmp(command, "PLAY") != 0) &&
            (strcmp(command, "TITLE") != 0) &&
            (strcmp(command, "QUIT") != 0)) {
            SendResponse("ERROR: Invalid command");
            return;
        }

        try {
            auto radio = GetRadioById(id);

            auto sent = radio->SendCommand(std::string(command));
            if (!sent.first) {
                SendResponse("ERROR " + std::to_string(radio->id()) + ": could not send command to player. Probably not reachable.");
                return;
            }

            std::string msg = "OK " + std::to_string(radio->id());

            if (strcmp(command, "TITLE") == 0) {
                auto received = radio->ReceiveResponse(sent.second);

                if (!received.first) {
                    SendResponse("ERROR " + std::to_string(radio->id()) + ": title command timeout");
                    return;
                }

                msg += " ";
                msg += received.second;
            }

            if (strcmp(command, "QUIT") == 0) {
                RemoveRadioById(radio->id());
            }

            SendResponse(msg);
        } catch (const std::exception& e) {
            SendResponse("ERROR: Player with provided [id] does not exists");
            return;
        }

        return;
    }

    SendResponse("ERROR: Invalid command");
    return;
}


std::shared_ptr<Radio> Session::AddRadio(uint64_t id, const char *host, unsigned long port,
    unsigned short hour, unsigned short minute, unsigned int interval,
    const char *player_host, const char *player_path, unsigned long player_port,
    const char *player_file, const char *player_md)
{
    std::shared_ptr<Radio> radio = std::make_shared<Radio>(
        id,
        host, port, hour, minute, interval, player_host, player_path,
        player_port, player_file, player_md
    );

    radios_.push_back(radio);
    return radios_.back();
}


std::shared_ptr<Radio> Session::GetRadioById(uint64_t id)
{
    for (auto radio : radios_) {
        if (radio->id() == id) {
            return radio;
        }
    }

    throw RadioNotFoundException();
}


void Session::RemoveRadioById(uint64_t id)
{
    for (auto it = radios_.begin(); it != radios_.end(); ++it) {
        if (it->get()->id() == id) {
            // remove stderr socket from poll sockets (if exists)
            for (auto itt = poll_sockets_.begin(); itt != poll_sockets_.end(); ++itt) {
                if (it->get()->player_stderr() == itt->fd) {
                    poll_sockets_.erase(itt);
                    break;
                }
            }

            it->get()->active(false);
            radios_.erase(it);
            break;
        }
    }

    // remove all events which points to removed radio
    std::priority_queue<Event> tmp_events;
    for (; !events_.empty(); events_.pop()) {
        auto event = events_.top();

        if (event.radio_id != id)
            tmp_events.push(event);
    }

    events_ = tmp_events;
}


bool Session::SendResponse(const std::string& message) const
{
    std::string full_message = message + "\r\n";
    auto msg = full_message.c_str();

    size_t to_sent = 0;
    while (to_sent != strlen(msg)) {
        ssize_t bytes_send = send(socket_, &msg[to_sent], strlen(msg) - to_sent, 0);
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


bool Session::AddFileDescriptor(int socket)
{
    // make socket nonblocking
    int err = fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
    if (err < 0) {
        return false;
    }

    pollfd fd;
    fd.fd = socket;
    fd.events = POLLIN;

    poll_sockets_.push_back(fd);

    return true;
}


unsigned int Session::Timeout() const
{
    if (events_.empty()) {
        return 1000000; // 10 ^ 6 seconds (this should be enough...)
    }

    auto event = events_.top();
    time_t current_time = time(NULL);

    return std::max((long)0, (long)(event.event_time - current_time));
}


void Session::HandleTimeout()
{
    if (events_.empty())
        return;

    auto event = events_.top();
    events_.pop();

    try {
        auto radio = GetRadioById(event.radio_id);

        if (event.action == START_RADIO) {
            bool started = radio->StartRadio();
            if (!started) {
                RemoveRadioById(radio->id());
                SendResponse("ERROR: ssh failed");
                return;
            }

            bool added = AddFileDescriptor(radio->player_stderr());
            if (!added) {
                RemoveRadioById(radio->id());
                SendResponse("ERROR: could not handle descriptor");
                return;
            }
        } else if (event.action == SEND_QUIT) {
            radio->SendCommand("QUIT");
            RemoveRadioById(radio->id());
        }
    } catch (std::exception& e) {
        // radio does not exists
        return;
    }
}
