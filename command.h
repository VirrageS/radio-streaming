#ifndef __COMMAND_H__
#define __COMMAND_H__

#include "stream.h"

class Command
{
public:
    Command();
    Command(uint16_t port);
    virtual ~Command();

    Command(Command&& other) = default;
    Command(const Command& other) = default;
    Command& operator=(Command&& other) = default;
    Command& operator=(const Command& other) = default;

    void InitializeSocket();
    static void Listen(Command* command, Stream* stream);

private:
    int socket_;
    uint16_t port_;
};

#endif
