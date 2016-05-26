#ifndef __SESSION_H__
#define __SESSION_H__

#include <sys/poll.h>
#include <pthread.h>

#include <string>
#include <vector>
#include <queue>
#include <iostream>

enum ActionsEnum {
    START_RADIO, SEND_QUIT
};

struct Event {
    std::string radio_id;
    ActionsEnum action;
    time_t event_time;

    bool operator<(const Event& event) const {
        return event_time < event.event_time;
    }
};

class Radio {
public:
    Radio()
    {
        m_playerStderr = -1;

        m_host = NULL;
        m_port = 0;

        m_playerHost = NULL;
        m_playerPath = NULL;
        m_playerPort = 0;
        m_playerFile = NULL;
        m_playerMeta = NULL;

        m_hour = 0;
        m_minute = 0;
        m_interval = 0;
    }

    Radio(char *host, unsigned long port,
          unsigned short hour, unsigned short minute, unsigned int interval,
          char *player_host, char *player_path, unsigned long player_port,
          char *player_file, char *player_md)
    {
        m_host = (char *)strdup(host);
        m_port = port;
        m_hour = hour;
        m_minute = minute;
        m_interval = interval;
        m_playerHost = (char *)strdup(player_host);
        m_playerPath = (char *)strdup(player_path);
        m_playerPort = player_port;
        m_playerFile = (char *)strdup(player_file);
        m_playerMeta = (char *)strdup(player_md);
    }

    Radio(const Radio& radio)
    {
        m_id = radio.m_id;

        m_host = strdup(radio.m_host);
        m_port = radio.m_port;

        m_playerHost = strdup(radio.m_playerHost);
        m_playerPath = strdup(radio.m_playerPath);
        m_playerPort = radio.m_playerPort;
        m_playerFile = strdup(radio.m_playerFile);
        m_playerMeta = strdup(radio.m_playerMeta);
        m_playerStderr = radio.m_playerStderr;

        m_hour = radio.m_hour;
        m_minute = radio.m_minute;
        m_interval = radio.m_interval;

    }

    ~Radio() {
        free(m_host);

        free(m_playerHost);
        free(m_playerPath);
        free(m_playerFile);
        free(m_playerMeta);

        close(m_playerStderr);
    }

    std::string id() const { return m_id; }
    void id(const std::string& id) { m_id = id; }

    int playerStderr() const { return m_playerStderr; }
    void playerStderr(int player_stderr) { m_playerStderr = player_stderr; }

    bool start_radio();

    bool send_radio_command(std::string message);
    bool recv_radio_response(char *buffer);

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

    char* m_host;
    unsigned long m_port;

    char* m_playerHost;
    char* m_playerPath;
    unsigned long m_playerPort;
    char* m_playerFile;
    char* m_playerMeta;

    unsigned short m_hour;
    unsigned short m_minute;
    unsigned int m_interval;
};

class Session {
public:

    pthread_t m_thread;

    size_t in_buffer;
    char buffer[30000];

    Session() {
        m_socket = -1;

        in_buffer = 0;
        memset(&buffer, 0, sizeof(buffer));
    }

    Session(int socket)
    {
        m_socket = socket;

        in_buffer = 0;
        memset(&buffer, 0, sizeof(buffer));
    }

    ~Session() {
        m_radios.clear();
        m_radios.shrink_to_fit();
        m_pollSockets.clear();
        m_pollSockets.shrink_to_fit();

        close(m_socket);

        in_buffer = 0;
        memset(&buffer, 0, sizeof(buffer));
    }

    std::string id() const { return m_id; }
    void id(const std::string& id) { m_id = id; }

    int socket() const { return m_socket; }
    void socket(int socket) { m_socket = socket; }

    std::vector<Radio> radios() { return m_radios; }
    std::vector<pollfd> pollSockets() { return m_pollSockets; }

    Radio& add_radio(Radio& radio);

    /**
        Return radio with provided `id` or throws RadioNotFoundException
        if radio does not exists.

        @param id: ID of radio which we want to find
        @throws: RadioNotFoundException if radio does not exists.
        @returns: Radio with `id`.
        **/
    Radio& get_radio_by_id(const std::string& id);

    void remove_radio_by_id(const std::string& id);

    bool send_session_message(const std::string& message) const;

    void add_poll_fd(int socket);
    int get_timeout() const;
    void handle_timeout();

    /**
        Parse and make action depending on message.

        @param message: Message which is parsed.
        **/
    void parse(std::string message);

private:
    std::string m_id;

    int m_socket;
    std::vector<Radio> m_radios;
    std::priority_queue<Event> m_events;
    std::vector<pollfd> m_pollSockets;
};

class Sessions {
public:
    pthread_mutex_t m_mutex;
    std::vector<Session> m_sessions;

    Sessions() {
        pthread_mutex_init(&m_mutex, NULL);
    }

    ~Sessions() {
        m_sessions.clear();
        m_sessions.shrink_to_fit();

        pthread_mutex_destroy(&m_mutex);
    }

    /**
        Add session to sessions and returns new created one.

        @returns: New created session.
        **/
    Session& add_session(Session& session);

    /**
        Remove sessions with `id`.

        @param id: ID of session which has to be deleted.
        **/
    void remove_session_by_id(const std::string& id);
};

#endif
