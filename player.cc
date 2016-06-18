#include <iostream>
#include <string>
#include <thread>

#include "misc.h"
#include "stream.h"
#include "command.h"

/**
    Validate player parameters.
    **/
std::tuple<FILE*, bool, std::string, uint16_t, std::string, uint16_t>
ValidateParameters(int argc, char *argv[])
{
    if (argc != 7) {
        std::cerr << "Wrong number of parameters" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // validate file parameter
    std::string file_name = argv[4];
    FILE* output_file;
    if (file_name.compare("-") != 0) {
        output_file = std::fopen(file_name.data(), "wb");

        if (!output_file) {
            std::perror("fopen() failed");
            std::exit(EXIT_FAILURE);
        }
    } else {
        output_file = stdout;
    }

    // validate ports
    uint16_t server_port, command_port;
    try {
        server_port = ParsePort(argv[3]);
        command_port = ParsePort(argv[5]);
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // validate metadata parameter
    bool metadata;
    if (strtob(&metadata, argv[6]) != 0) {
        std::cerr << "Meta data (" << argv[6] << ") should be 'yes' or 'no'" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    return std::make_tuple(output_file, metadata, argv[1], server_port, argv[2], command_port);
}


int
main(int argc, char *argv[])
{
    auto parameters = ValidateParameters(argc, argv);

    Command command(std::get<5>(parameters));
    Stream stream(std::get<0>(parameters), std::get<1>(parameters));

    try {
        command.InitializeSocket();
        std::thread (&Command::Listen, &command, &stream).detach();

        stream.InitializeSocket(std::get<2>(parameters), std::get<3>(parameters));
        stream.SendRequest(std::get<4>(parameters));
        stream.Listen();
    } catch (std::exception& e) {
        std::perror(e.what());
        return EXIT_FAILURE;
    }

    return 0;
}
