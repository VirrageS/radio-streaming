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

#include "misc.h"

class RadioNotFoundException: public std::exception
{
    virtual const char* what() const throw() {
        return "Radio has not been found";
    }
};

enum ActionTypes {
    START_RADIO,
    SEND_QUIT
};

struct Event
{
    uint64_t radio_id; // radio id for which this is connected
    ActionTypes action; // action which event will execute
    time_t event_time; // time at which event will be executed

    bool operator<(const Event& event) const {
        return event_time >= event.event_time;
    }
};


class Radio
{
public:
    Radio();
    Radio(uint64_t id, const char *host, unsigned long port, unsigned short hour,
          unsigned short minute, unsigned int interval, const char *player_host,
          const char *player_path, unsigned long player_port, const char *player_file,
          const char *player_md);
    virtual ~Radio();

    Radio(Radio&& other) = default;
    Radio(const Radio& other) = default;
    Radio& operator=(Radio&& other) = default;
    Radio& operator=(const Radio& other) = default;

    // Convert player to string.
    std::string to_string() const;

    // Return player's id.
    uint64_t id() const { return id_; }

    // Set |id| for player.
    void id(uint64_t id) { id_ = id; }

    // Return player's stderr descriptor.
    int player_stderr() const { return player_stderr_; }

    // Set |player_stderr| descriptor for player.
    void player_stderr(int player_stderr) { player_stderr_ = player_stderr; }

    // Return player state (active or not).
    bool active() const { return active_; }

    // Set player state (active or not).
    void active(bool active) { active_ = active; }

    // Start player on host. Returns true if player has started, false otherwise.
    bool StartRadio();

    // Send |command| to player. Returns pair <bool, int> if bool is true
    // it indicates that send was successful and socket is returned, false otherwise.
    std::pair<bool, int> SendCommand(const std::string& command);

    // Receive response from player on |socket|. Returns pair <bool, string>
    // if bool is true it indicates that receive was successful
    // and message is returned, false otherwise.
    std::pair<bool, std::string> ReceiveResponse(int socket);


private:
    uint64_t id_; // player id
    bool active_; // state of player, true if it is running, false othwerwise

    int player_stderr_; // player stderr descriptor

    std::string host_; // host on which player should be run
    uint16_t port_; // port on which player will listen and receive commands

    std::string player_host_; // host on which player will be listening for ICY data.
    std::string player_path_; // path to resource on which player will be listening for ICY data.
    uint16_t player_port_; // port on which player will connect to ICY server.
    std::string player_file_; // file to which player will save data ("-" will make player to send data on stdout).
    std::string player_metadata_; // checks if we should ask for metadata or not.

    uint8_t hour_; // hour at which player should be started.
    uint8_t minute_; // minute at which player shoule be started.
    unsigned int interval_; // how long should player been listening to data (in minutes).
};

class Session
{
public:
    std::string buffer; // session buffer

    Session(uint64_t id);
    Session(uint64_t id, int socket);

    virtual ~Session();

    Session(Session&& other) = default;
    Session(const Session& other) = default;
    Session& operator=(Session&& other) = default;
    Session& operator=(const Session& other) = default;


    // Return session's id.
    uint64_t id() const { return id_; }

    // Set |id| for session.
    void id(uint64_t id) { id_ = id; }

    // Return session socket.
    int socket() const { return socket_; }

    // Return all players.
    std::vector<std::shared_ptr<Radio>> radios() { return radios_; }

    // Return all poll sockets.
    std::vector<pollfd>& poll_sockets() { return poll_sockets_; }

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
    std::shared_ptr<Radio> AddRadio(uint64_t id,
        const char *host, unsigned long port, unsigned short hour,
                     unsigned short minute, unsigned int interval,
                     const char *player_host, const char *player_path,
                     unsigned long player_port, const char *player_file,
                     const char *player_md);

    // Return radio with provided |id| or throws RadioNotFoundException
    // if radio does not exists.
    std::shared_ptr<Radio> GetRadioById(uint64_t id);

    // Remove player with |id| from current session.
    void RemoveRadioById(uint64_t id);

    // Send |response| to the other side of (telnet) session. If sending
    // was successful, function returns true or false otherwise
    bool SendResponse(const std::string& response) const;

    // Add |socket| to poll sockets.
    // Returns true if adding was successful, false otherwise.
    bool AddFileDescriptor(int socket);

    /**
        Return time for next pending event.

        @returns: Time for next event.
        **/
    unsigned int Timeout() const;

    // Get pending event and execute it.
    void HandleTimeout();

    // Parse and make action depending on |message|.
    void Parse(std::string message);

private:
    static uint64_t current_id_;

    uint64_t id_; // session id

    int socket_; // socket on which (telnet) session is listening
    std::vector<std::shared_ptr<Radio>> radios_; // all players
    std::priority_queue<Event> events_; // pending session events
    std::vector<pollfd> poll_sockets_; // all poll sockets in session (session socket and players' stderr)
};

#endif
