#include "session.h"

void init_session(session_t *session)
{
    session->active = true;
    session->in_buffer = 0;
    session->length = 0;
    memset(&buffer, 0, sizeof(buffer));
}


void init_sessions(sessions_t *sessions)
{
    sessions->length = 0;
    memset(&sessions->sessions, 0, sizeof(sessions->sessions));
}

session_t* add_session(sessions_t sessions)
{
    // check if we can reuse any session that was closed
    for (size_t i = 0; i < sessions->length; ++i) {
        if (!sessions[i].active) {
            init_session(sessions[i]);
            return sessions[i];
        }
    }

    sessions->length += 1;
    sessions->sessions = realloc(sessions->sessions, sessions->length * sizeof(session_t));
    if (!sessions->sessions) {
        syserr("realloc(): lost all sessions");
    }

    session_t *new_session = sessions->sessions[sessions->length - 1]
    init_session(new_session);

    return new_session;
}

int add_radio(session_t *session, radio_t radio)
{
    session->length += 1;
    session->radios = realloc(sessions->radios, sessions->length * sizeof(radio_t));
    if (!session->radios) {
        session->active = false;
        syserr("realloc(): lost all radios");
        return 1;
    }

    session->radios[session->length - 1] = radio;
    return 0;
}

radio_t* get_radio_by_id(session_t session, char* id)
{
    for (size_t i = 0; i < session->length; ++i) {
        if (strcmp(session->radios[i].id, id) == 0) {
            return &session->radios[i];
        }
    }

    return NULL;
}
