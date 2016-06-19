#ifndef __SESSIONS_H__
#define __SESSIONS_H__

#include "session.h"

#include <memory>
#include <mutex>

class Sessions
{
public:
    Sessions();
    virtual ~Sessions();

    Sessions(Sessions&& other) = default;
    Sessions(const Sessions& other) = default;
    Sessions& operator=(Sessions&& other) = default;
    Sessions& operator=(const Sessions& other) = default;

    /// Add session to sessions and returns new created one.
    std::shared_ptr<Session> AddSession();

    // Add session with |socket| to sessions and returns new created one.
    std::shared_ptr<Session> AddSession(int socket);

    // Remove sessions with |id|.
    void RemoveSessionById(uint64_t id);

    void Accept();
    void InitializeSocket(int port);


private:
    void HandleSession(std::shared_ptr<Session> session);


private:
    static uint64_t current_id_;

    int socket_;

    std::mutex mutex_; // mutex to protect sessions
    std::vector<std::shared_ptr<Session>> sessions_; // all sessions
};

#endif
