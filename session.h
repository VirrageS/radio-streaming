#ifndef __SESSION_H__
#define __SESSION_H__

#include <sys/types.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t radio_thread;
    char* id;

    char* host;
    int port;
} radio_t;

typedef struct {
    pthread_t session_thread;
    int socket;
    bool active;

    size_t in_buffer;
    char buffer[30000];

    size_t length;
    radio_t* radios;
} session_t;

typedef struct {
    size_t length;
    session_t* sessions;
} sessions_t;


void init_session(session_t *session);
void init_sessions(sessions_t *sessions);

session_t* add_session(sessions_t sessions);
int add_radio(session_t *session, radio_t *radio);
radio_t* get_radio_by_id(const session_t *session, char* id);

#endif
