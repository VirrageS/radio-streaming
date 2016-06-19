#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <fcntl.h>

#include <exception>
#include <iostream>

#include "sessions.h"

uint64_t Sessions::current_id_ = 0;

// class SessionNotFoundException: public std::exception
// {
//     virtual const char* what() const throw() {
//         return "Session has not been found";
//     }
// };




/**
********************
***** SESSIONS *****
********************
**/

Sessions::Sessions()
  : socket_(),
    mutex_(),
    sessions_()
{
}

Sessions::~Sessions()
{
}


std::shared_ptr<Session> Sessions::AddSession()
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::shared_ptr<Session> session = std::make_shared<Session>(current_id_++);

    sessions_.push_back(session);
    return sessions_.back();
}


std::shared_ptr<Session> Sessions::AddSession(int socket)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::shared_ptr<Session> session = std::make_shared<Session>(current_id_++, socket);

    sessions_.push_back(session);
    return sessions_.back();
}

void Sessions::RemoveSessionById(uint64_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);

   for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
       if (it->get()->id() == id) {
           sessions_.erase(it);
       }
   }
}





/**
    Set connection on TCP listen socket.
    **/
void Sessions::InitializeSocket(int port)
{
    int err;
    struct sockaddr_in server;

    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0) {
        throw std::runtime_error("socket() failed");
    }

    int opt = 1;
    err = setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
    if (err < 0) {
        throw std::runtime_error("setsockopt() failed");
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = (port > 0 ? htons(port) : 0);

    err = bind(socket_, (struct sockaddr*)&server, sizeof(server));
    if (err < 0) {
        throw std::runtime_error("bind() failed");
    }


    err = listen(socket_, 64);
    if (err < 0) {
        throw std::runtime_error("listen() failed");
    }

    if (port == 0) {
        // get port on which master is listening

        socklen_t len = sizeof(server);
        err = getsockname(socket_, (struct sockaddr *)&server, &len);
        if (err < 0) {
            throw std::runtime_error("getsockname() failed");
        }

        std::cout << ntohs(server.sin_port) << std::endl;
    }
}


void Sessions::Accept()
{
    while (true) {
        int client_socket = accept(socket_, NULL, NULL);
        if (client_socket < 0) {
            throw std::runtime_error("accept() failed");
        }

        auto session = AddSession(client_socket);
        std::thread (&Sessions::HandleSession, this, session).detach();
    }
}


/**
    Thread session function, which handle everything connected with session.
    **/
void Sessions::HandleSession(std::shared_ptr<Session> session)
{
    debug_print("[%llu] started handling session...\n", session->id());

    bool added = session->AddFileDescriptor(session->socket());
    if (!added) {
        goto end_session;
    }

    while (true) {
        int err = poll(session->poll_sockets().data(), (int)session->poll_sockets().size(), session->Timeout() * 1000);

        if (err < 0) {
            std::cerr << "poll() failed" << std::endl;
            goto end_session;
        } else if (err == 0) {
            debug_print("[%llu] handling timeout...\n", session->id());
            session->HandleTimeout();
        } else {
            for (auto descriptor = session->poll_sockets().begin(); descriptor != session->poll_sockets().end(); ++descriptor) {
                if (descriptor->revents == 0)
                    continue;

                if (!(descriptor->revents & (POLLIN | POLLHUP))) {
                    std::cerr << descriptor->fd << " - unexpected revents = " << descriptor->revents << std::endl;
                    goto end_session;
                }

                if (descriptor->fd != session->socket()) {
                    // we got message on player stderr
                    // so we should delete this
                    std::string buffer = "";

                    while (true) {
                        std::vector<char> tmp_buffer(4096);
                        ssize_t bytes_recieved = read(descriptor->fd, tmp_buffer.data(), tmp_buffer.size() - 1);
                        if (bytes_recieved <= 0)
                            break;

                        tmp_buffer.resize(bytes_recieved);
                        buffer.append(tmp_buffer.cbegin(), tmp_buffer.cend());
                    }

                    debug_print("%s\n", "got something on player stderr");

                    for (auto radio : session->radios()) {
                        if (radio->player_stderr() == descriptor->fd) {
                            std::string msg = "ERROR " + std::to_string(radio->id()) + ": " + buffer;

                            session->SendResponse(msg);
                            session->RemoveRadioById(radio->id());
                            break;
                        }
                    }
                } else {
                    std::vector<char> tmp_buffer(4096);

                    ssize_t bytes_recieved = read(session->socket(), tmp_buffer.data(), tmp_buffer.size() - 1);
                    if (bytes_recieved < 0) {
                        if (errno != EWOULDBLOCK) {
                            std::cerr << "read() in session failed" << std::endl;
                            goto end_session;
                        }
                    } else if (bytes_recieved == 0) {
                        debug_print("ending %d connection\n", session->socket());
                        goto end_session;
                    } else {
                        tmp_buffer.resize(bytes_recieved);
                        session->buffer.append(tmp_buffer.cbegin(), tmp_buffer.cend());

                        debug_print("%s\n", "got something to read");
                        debug_print("message: %s\n", session->buffer.c_str());

                        size_t end = 0;
                        for (size_t i = 0; i < session->buffer.length(); ++i) {
                            if (i + 1 < session->buffer.length()) {
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
                            std::string message = session->buffer.substr(0, end);

                            // remove controlling bytes
                            bool found = true;
                            while (found) {
                                found = false;

                                for (size_t i = 0; i < message.length(); i++) {
                                    if (message[i] == '\xff') {
                                        size_t to_remove;

                                        if (message[i + 1] == '\xff') {
                                            to_remove = 2;
                                        } else if (message[i + 1] > '\xfa') {
                                            to_remove = 3;
                                        } else  {
                                            to_remove = 2;
                                        }

                                        found = true;
                                        message.erase(std::next(message.begin(), i), std::next(message.begin(), i + to_remove));
                                        break;
                                    }
                                }
                            }

                            session->Parse(message);
                            session->buffer.erase(0, end);
                        }
                    }
                }

                break;
            }
        }
    }

end_session:
    debug_print("[%llu] closing handling session...\n", session->id());
    RemoveSessionById(session->id());
}
