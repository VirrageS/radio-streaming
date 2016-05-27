#ifndef __SESSION_H__
#define __SESSION_H__

#include <sys/poll.h>
#include <unistd.h>

#include <mutex>
#include <thread>
#include <memory>

#include <string>
#include <utility>
#include <vector>
#include <queue>
#include <iostream>

#include "misc.h"

enum ActionsEnum {
    START_RADIO, SEND_QUIT
};

struct Event {
    std::string radio_id;
    ActionsEnum action;
    time_t event_time;

    bool operator<(const Event& event) const {
        return event_time >= event.event_time;
    }
};

class Radio {
public:
    Radio();
    Radio(const char *host, unsigned long port, unsigned short hour,
          unsigned short minute, unsigned int interval, const char *player_host,
          const char *player_path, unsigned long player_port, const char *player_file,
          const char *player_md);
    ~Radio();

    std::string id() const { return m_id; }
    void id(const std::string& id) { m_id = id; }

    int player_stderr() const { return m_playerStderr; }
    void player_stderr(int player_stderr) { m_playerStderr = player_stderr; }

    bool start_radio();

    std::pair<bool, int> send_radio_command(std::string message);
    bool recv_radio_response(char *buffer, int socket);

    void print_radio()
    {
        std::cerr << "##################################" << std::endl;
        std::cerr << "id\t: " << m_id << std::endl;
        std::cerr << "host\t: " << m_host << std::endl;
        std::cerr << "port\t: " << m_port << std::endl;
        std::cerr << "hour\t: " << m_hour << std::endl;
        std::cerr << "minute\t: " << m_minute << std::endl;
        std::cerr << "interval\t: " << m_interval << std::endl;

        std::cerr << "player-host\t: " << m_playerHost << std::endl;
        std::cerr << "player-path\t: " << m_playerPath << std::endl;
        std::cerr << "player-port\t: " << m_playerPort << std::endl;
        std::cerr << "player-file\t: " << m_playerFile << std::endl;
        std::cerr << "player-md\t: " << m_playerMeta << std::endl;
        std::cerr << "player-err\t: " << m_playerStderr << std::endl;
        std::cerr << "##################################" << std::endl;
    }

private:
    std::string m_id;

    int m_playerStderr;

    std::string m_host;
    unsigned long m_port;

    std::string m_playerHost;
    std::string m_playerPath;
    unsigned long m_playerPort;
    std::string m_playerFile;
    std::string m_playerMeta;

    unsigned short m_hour;
    unsigned short m_minute;
    unsigned int m_interval;
};

class Session {
public:
    size_t in_buffer;
    char buffer[30000];

    Session()
    {
        m_socket = -1;

        in_buffer = 0;
        memset(&buffer, 0, sizeof(buffer));
    }

    Session(int socket) : Session()
    {
        m_socket = socket;
    }

    ~Session()
    {
        for (auto r : m_radios)
            remove_radio_by_id(r->id());

        m_radios.clear();
        m_radios.shrink_to_fit();
        m_pollSockets.clear();
        m_pollSockets.shrink_to_fit();

        close(m_socket);

        debug_print("%s\n", "session destructor");
    }

    std::string id() const { return m_id; }
    void id(const std::string& id) { m_id = id; }

    int socket() const { return m_socket; }
    void socket(int socket) { m_socket = socket; }

    std::vector<std::shared_ptr<Radio>> radios() { return m_radios; }
    std::vector<pollfd>& poll_sockets() { return m_pollSockets; }

    std::shared_ptr<Radio> add_radio(const char *host, unsigned long port, unsigned short hour,
                     unsigned short minute, unsigned int interval,
                     const char *player_host, const char *player_path,
                     unsigned long player_port, const char *player_file,
                     const char *player_md);

    /**
        Return radio with provided `id` or throws RadioNotFoundException
        if radio does not exists.

        @param id: ID of radio which we want to find
        @throws: RadioNotFoundException if radio does not exists.
        @returns: Radio with `id`.
        **/
    std::shared_ptr<Radio> get_radio_by_id(const std::string& id);

    void remove_radio_by_id(const std::string& id);

    bool send_session_message(const std::string& message) const;

    bool add_poll_fd(int socket);
    unsigned int get_timeout() const;
    void handle_timeout();

    /**
        Parse and make action depending on message.

        @param message: Message which is parsed.
        **/
    void parse(std::string message);

private:
    std::string m_id;

    int m_socket;
    std::vector<std::shared_ptr<Radio>> m_radios;
    std::priority_queue<Event> m_events;
    std::vector<pollfd> m_pollSockets;
};

class Sessions {
public:
    Sessions() {}

    ~Sessions() {
        m_sessions.clear();
        m_sessions.shrink_to_fit();
    }

    /**
        Add session to sessions and returns new created one.

        @returns: New created session.
        **/
    std::shared_ptr<Session> add_session();
    std::shared_ptr<Session> add_session(int socket);

    /**
        Remove sessions with `id`.

        @param id: ID of session which has to be deleted.
        **/
    void remove_session_by_id(const std::string& id);

private:
    std::mutex m_mutex;
    std::vector<std::shared_ptr<Session>> m_sessions;
};

#endif
