#ifndef __SESSION_H__
#define __SESSION_H__

#include <sys/poll.h>
#include <pthread.h>

#include <string>
#include <vector>
#include <queue>
#include <iostream>

#define debug_print(fmt, ...) \
        do { if (DEBUG) { fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); fflush(stderr); }} while (0)

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define DEBUG 1

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
    std::string id;

    char* host;
    unsigned long port;

    char* player_host;
    char* player_path;
    unsigned long player_port;
    char* player_file;
    char* player_md;
    int player_stderr;

    unsigned short hour;
    unsigned short minute;
    unsigned int interval;

    Radio() {
        player_stderr = -1;

        host = NULL;

        player_host = NULL;
        player_path = NULL;
        player_file = NULL;
        player_md = NULL;

        port = 0;
        player_port = 0;
        hour = 0;
        minute = 0;
        interval = 0;
    }

    Radio(const Radio& radio) {
        id = radio.id;

        host = strdup(radio.host);
        port = radio.port;

        player_host = strdup(radio.player_host);
        player_path = strdup(radio.player_path);
        player_port = radio.player_port;
        player_file = strdup(radio.player_file);
        player_md = strdup(radio.player_md);
        player_stderr = radio.player_stderr;

        hour = radio.hour;
        minute = radio.minute;
        interval = radio.interval;

    }

    ~Radio() {
        // pthread_cancel(thread);

        free(host);

        free(player_host);
        free(player_path);
        free(player_file);
        free(player_md);

        close(player_stderr);
    }

    bool send_radio_command(std::string message);
    bool recv_radio_response(char *buffer);
    bool start_radio();

    void print_radio()
    {
        std::cerr << "##################################" << std::endl;
        std::cerr << "id\t: " << id << std::endl;
        std::cerr << "host\t: " << host << std::endl;
        std::cerr << "port\t: " << port << std::endl;
        std::cerr << "hour\t: " << hour << std::endl;
        std::cerr << "minute\t: " << minute << std::endl;
        std::cerr << "interval\t: " << interval << std::endl;

        std::cerr << "player-host\t: " << player_host << std::endl;
        std::cerr << "player-path\t: " << player_path << std::endl;
        std::cerr << "player-port\t: " << player_port << std::endl;
        std::cerr << "player-file\t: " << player_file << std::endl;
        std::cerr << "player-md\t: " << player_md << std::endl;
        std::cerr << "player-err\t: " << player_stderr << std::endl;
        std::cerr << "##################################" << std::endl;
    }
};

class Session {
public:
    pthread_t thread;
    pthread_mutex_t mutex;

    std::string id;

    int socket;
    size_t in_buffer;
    char buffer[30000];

    std::vector<Radio> radios;
    std::priority_queue<Event> events;
    std::vector<pollfd> poll_sockets;

    Session() {
        pthread_mutex_init(&mutex, NULL);

        socket = -1;

        in_buffer = 0;
        memset(&buffer, 0, sizeof(buffer));
    }

    ~Session() {
        radios.clear();
        radios.shrink_to_fit();
        poll_sockets.clear();
        poll_sockets.shrink_to_fit();

        close(socket);

        in_buffer = 0;
        memset(&buffer, 0, sizeof(buffer));

        pthread_mutex_destroy(&mutex);
    }

    Radio& add_radio(char *host, unsigned long port, unsigned short hour, unsigned short minute, unsigned int interval, char *player_host, char *player_path, unsigned long player_port, char *player_file, char *player_md);
    Radio& get_radio_by_id(std::string id);
    void remove_radio_with_id(std::string id);

    bool send_session_message(std::string message);

    void add_poll_fd(int socket);
    int get_timeout();
    void handle_timeout();

    void parse_and_action(char* buffer, size_t end);
};

class Sessions {
public:
    pthread_mutex_t mutex;
    std::vector<Session> sessions;

    Sessions() {
        pthread_mutex_init(&mutex, NULL);
    }

    ~Sessions() {
        sessions.clear();
        sessions.shrink_to_fit();

        pthread_mutex_destroy(&mutex);
    }

    Session& add_session();

    void remove_session(std::string id) {
        pthread_mutex_lock(&mutex);

        for (auto it = sessions.begin(); it != sessions.end(); ++it) {
            if (it->id == id) {
                sessions.erase(it);
                break;
            }
        }

        pthread_mutex_unlock(&mutex);
    }
};

#endif
