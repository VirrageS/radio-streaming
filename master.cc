#include <iostream>

#include "misc.h"
#include "session.h"
#include "sessions.h"

/**
    Validates program parameters
    **/
uint16_t validate_parameters(int argc, char* argv[])
{
    if (argc > 2) {
        std::cerr << "Usage ./" << argv[0] << " <port>" << std::endl;
        return EXIT_FAILURE;
    }

    uint16_t master_port = 0;

    if (argc == 2) {
        try {
            master_port = ParsePort(argv[1]);
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
            return EXIT_FAILURE;
        }
    }

    return master_port;
}


int main(int argc, char* argv[])
{
    srand(time(NULL));

    uint16_t master_port = validate_parameters(argc, argv);

    Sessions sessions;
    sessions.InitializeSocket(master_port);
    try {
        sessions.Accept();
    } catch (std::exception& e) {
        std::perror(e.what());
        return EXIT_FAILURE;
    }

    return 0;
}
