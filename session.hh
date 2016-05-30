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
    std::string radio_id; // radio id for which this is connected
    ActionsEnum action; // action which event will execute
    time_t event_time; // time at which event will be executed

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

    /**
        Convert player to string.

        @returns: Human readable player.
        **/
    std::string to_string();

    /**
        Return player's id.

        @returns: Id assigned to player.
        **/
    std::string id() const { return m_id; }

    /**
        Set `id` for player.
        **/
    void id(const std::string& id) { m_id = id; }

    /**
        Return number of player stderr descriptor.

        @returns: Player's stderr number descriptor.
        **/
    int player_stderr() const { return m_playerStderr; }

    /**
        Set stderr descriptor for player.

        @param player_stderr: Stderr descriptor number.
        **/
    void player_stderr(int player_stderr) { m_playerStderr = player_stderr; }

    /**
        Return player state (active or not).

        @returns: Current state of player.
        **/
    bool active() const { return m_active; }

    /**
        Set player state (active or not).

        @param active: True if player is active, false otherwise.
        **/
    void active(bool active) { m_active = active; }

    /**
        Start player on host.

        @returns: True if player has started, false otherwise.
        **/
    bool start_radio();

    /**
        Send command to player.

        @param message: Message which we want to save.
        @returns: Pair <bool, int> if bool is true it indicates that send was successful
                  and socket is returned, false otherwise.
        **/
    std::pair<bool, int> send_radio_command(std::string message);

    /**
        Receive command from player.

        @param socket: Socket on which we want to listen for response.
        @returns: Pair <bool, string> if bool is true it indicates that receive was successful
                  and message is returned, false otherwise.
        **/
    std::pair<bool, std::string> recv_radio_response(int socket);


private:
    std::string m_id; // player id
    bool m_active; // state of player, true if it is running, false othwerwise

    int m_playerStderr; // player stderr descriptor

    std::string m_host; // host on which player should be run
    unsigned long m_port; // port on which player will listen and receive commands

    std::string m_playerHost; // host on which player will be listening for ICY data.
    std::string m_playerPath; // path to resource on which player will be listening for ICY data.
    unsigned long m_playerPort; // port on which player will connect to ICY server.
    std::string m_playerFile; // file to which player will save data ("-" will make player to send data on stdout).
    std::string m_playerMeta; // checks if we should ask for metadata or not.

    unsigned short m_hour; // hour at which player should be started.
    unsigned short m_minute; // minute at which player shoule be started.
    unsigned int m_interval; // how long should player been listening to data (in minutes).
};

class Session {
public:
    std::string buffer; // session buffer

    Session() : buffer()
    {
        m_socket = -1;
    }

    Session(int socket) : Session()
    {
        m_socket = socket;
    }

    ~Session()
    {
        for (auto r : m_radios) {
            r->send_radio_command("QUIT");
            remove_radio_by_id(r->id());
        }

        m_radios.clear();
        m_radios.shrink_to_fit();
        m_pollSockets.clear();
        m_pollSockets.shrink_to_fit();

        buffer.clear();

        close(m_socket);
    }

    /**
        Return session's id.

        @returns: Id assigned for session.
        **/
    std::string id() const { return m_id; }

    /**
        Set `id` for session.

        @param id: Id which we want to set for session.
        **/
    void id(const std::string& id) { m_id = id; }

    /**
        Return session socket.

        @returns: Session socket.
        **/
    int socket() const { return m_socket; }

    /**
        Return all players.

        @returns: All players in session.
        **/
    std::vector<std::shared_ptr<Radio>> radios() { return m_radios; }

    /**
        Return all poll sockets.

        @returns: All poll sockets.
        **/
    std::vector<pollfd>& poll_sockets() { return m_pollSockets; }

    /**
        Add player with parameters to session.

        @param host: Host on which player will be started (and also listening).
        @param port: Port on which player will be listening to commands.
        @param hour: Hour at which player should be started.
        @param minute: Minute at which player shoule be started.
        @param interval: How long should player been listening to data (in minutes).
        @param player_host: Host on which player will be listening for ICY data.
        @param player_path: Path to resource on which player will be listening for ICY data.
        @param player_port: Port on which player will connect to ICY server.
        @param player_file: File to which player will save data ("-" will make player to send data on stdout).
        @param player_md: Checks if we should ask for metadata or not.
        @returns: Radio with parameters.
        **/
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

    /**
        Remove player with `id` from current session.

        @param id: Id of player which we want to delete.
        **/
    void remove_radio_by_id(const std::string& id);

    /**
        Send message to the other side of (telnet) session.

        @param message: Message which we want to send.
        @returns: True if sending was successful, false otherwise
        **/
    bool send_session_message(const std::string& message) const;

    /**
        Add socket to poll sockets.

        @param socket: Socket which we want to add.
        @returns: True if adding was successful, false otherwise.
        **/
    bool add_poll_fd(int socket);

    /**
        Return time for next pending event.

        @returns: Time for next event.
        **/
    unsigned int get_timeout() const;

    /**
        Get pending event and execute it.
        **/
    void handle_timeout();

    /**
        Parse and make action depending on message.

        @param message: Message which is parsed.
        **/
    void parse(std::string message);

private:
    std::string m_id; // session id

    int m_socket; // socket on which (telnet) session is listening
    std::vector<std::shared_ptr<Radio>> m_radios; // all players
    std::priority_queue<Event> m_events; // pending session events
    std::vector<pollfd> m_pollSockets; // all poll sockets in session (session socket and players' stderr)
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

    /**
        Add session with `socket` to sessions and returns new created one.

        @param socket: Socket on which new session will be listening
        @returns: New created session.
        **/
    std::shared_ptr<Session> add_session(int socket);

    /**
        Remove sessions with `id`.

        @param id: ID of session which has to be deleted.
        **/
    void remove_session_by_id(const std::string& id);

private:
    std::mutex m_mutex; // mutex to protect sessions
    std::vector<std::shared_ptr<Session>> m_sessions; // all sessions
};

#endif
