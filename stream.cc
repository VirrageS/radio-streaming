#include <iostream>
#include <functional>

#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "stream.h"
#include "misc.h"

/**
    Get field from header.
    **/
static bool GetHttpHeaderField(const std::string& header, const std::string& field, std::string& value)
{
    auto start = header.find(field);
    if (start == std::string::npos) {
        return false;
    }
    start += field.length() + 1;

    auto end = header.find("\r\n", start);
    if (end == std::string::npos) {
        return false;
    }

    value = header.substr(start, end - start);
    return true;
}


/**
    Get field from metadata
    **/
static bool GetMetadataField(const std::string& metadata, const std::string& field, std::string& value)
{
    auto start = metadata.find(field + "=");
    if (start == std::string::npos) {
        return false;
    }
    start += field.length() + 2;

    auto end = metadata.find(";", start);
    if (end == std::string::npos) {
        return false;
    }

    value = metadata.substr(start, end - start - 2);
    return true;
}


std::string Header::to_string() const
{
    std::string header = "";
    header += "##################################\n";
    header += "Name\t: " + icy_name + "\n";
    header += "icy-notice1\t: " + icy_notice1 + "\n";
    header += "icy-notice2\t: " + icy_notice2 + "\n";
    header += "Genre\t: " + icy_genre + "\n";
    header += "Bitrate\t: " + icy_bitrate + "\n";
    header += "Meta-int\t: " + std::to_string(icy_metaint) + "\n";
    header += "##################################\n";

    return header;
}


void Stream::WriteToStream(size_t bytes_count)
{
    if (!quiting_ && !paused_) {
        std::fwrite(buffer_.data(), sizeof(char), bytes_count, stream_);
        std::fflush(stream_);
    }

    buffer_.erase(0, bytes_count);
}


Stream::Stream()
  : stream_(NULL),
    buffer_(),
    socket_(-1),
    header_(),
    current_interval_(0),
    metadata_length_(0),
    title_(),
    state_(PARSE_HEADER),
    metadata_(false),
    paused_(false),
    quiting_(false)
{}

Stream::Stream(FILE* stream, bool metadata)
  : stream_(stream),
    buffer_(),
    socket_(-1),
    header_(),
    current_interval_(0),
    metadata_length_(0),
    title_(),
    state_(PARSE_HEADER),
    metadata_(metadata),
    paused_(false),
    quiting_(false)
{
}

Stream::Stream(const Stream& other)
  : stream_(other.stream_),
    buffer_(other.buffer_),
    socket_(other.socket_),
    header_(other.header_),
    current_interval_(other.current_interval_),
    metadata_length_(other.metadata_length_),
    title_(other.title_),
    state_(other.state_),
    metadata_(other.metadata_),
    paused_(false),
    quiting_(false)
{
}


Stream::~Stream() noexcept
{
    if (stream_ != NULL)
        std::fclose(stream_);
}

void
Stream::SendRequest(const std::string& path)
{
    std::string request = "GET " + path + " HTTP/1.0 \r\nIcy-MetaData:" + std::to_string(metadata_) + " \r\n\r\n";
    size_t currently_sent = 0;

    std::cerr << request << std::endl;

    while (currently_sent < request.length()) {
        ssize_t bytes_sent = send(socket_, &request.data()[currently_sent], (request.length() - currently_sent), 0);
        if (bytes_sent <= 0) {
            throw std::runtime_error("Failed to send request");
        }

        currently_sent += bytes_sent;
    }
}


void
Stream::InitializeSocket(std::string host, uint16_t port)
{
    struct addrinfo addr_hints;
    struct addrinfo* addr_result;

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;

    int err = getaddrinfo(host.data(), std::to_string(port).data(), &addr_hints, &addr_result);
    if (err != 0) {
        freeaddrinfo(addr_result);
        throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(err));
    }

    // initialize socket according to getaddrinfo results
    socket_ = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (socket_ < 0) {
        freeaddrinfo(addr_result);
        throw std::runtime_error("socket() failed");
    }

    // connect socket to the server
    err = connect(socket_, addr_result->ai_addr, addr_result->ai_addrlen);
    if (err < 0) {
        freeaddrinfo(addr_result);
        throw std::runtime_error("connect() failed");
    }

    freeaddrinfo(addr_result);
}


void
Stream::ExtractHeaderFields()
{
    std::string response, metaint;
    bool success;

    success = GetHttpHeaderField(buffer_, "ICY", response);
    if (!success || (response.compare("200 OK") != 0))
        throw ParseHeaderFailureException();

    GetHttpHeaderField(buffer_, "icy-name", header_.icy_name);
    GetHttpHeaderField(buffer_, "icy-notice1", header_.icy_notice1);
    GetHttpHeaderField(buffer_, "icy-notice2", header_.icy_notice2);
    GetHttpHeaderField(buffer_, "icy-genre", header_.icy_genre);
    GetHttpHeaderField(buffer_, "icy-pub", header_.icy_pub);
    GetHttpHeaderField(buffer_, "icy-br", header_.icy_bitrate);

    success = GetHttpHeaderField(buffer_, "icy-metaint", metaint);
    if (success) {
        header_.icy_metaint = stoul(metaint, NULL, 10);
    }
}


void
Stream::Listen()
{
    while (!quiting_) {
        bool recieved = PollRecv(socket_, buffer_, 5000);
        if (recieved) {
            ParseData();
        } else {
            quiting_ = true;
        }
    }
}


void
Stream::ParseData()
{
    if (state_ == PARSE_HEADER) {
        auto pos = buffer_.find("\r\n\r\n");
        if (pos != std::string::npos) {
            ExtractHeaderFields();

            // check server decided not to send meta data
            if (metadata_ && (header_.icy_metaint == 0))
                metadata_ = false;

            buffer_.erase(0, pos + 4);

            current_interval_ = header_.icy_metaint;
            state_ = PARSE_DATA;
        }
    } else if (state_ == PARSE_DATA) {
        // check if we should parse any meta data
        if (!metadata_) {
            WriteToStream(buffer_.length());
            return;
        }

        if (current_interval_ >= buffer_.length()) {
            current_interval_ -= buffer_.length();
            WriteToStream(buffer_.length());
            return;
        }

        // write until header
        WriteToStream(current_interval_);

        // read metada length
        metadata_length_ = abs((int)buffer_[0]) * 16;
        buffer_.erase(0, 1);

        if (metadata_length_ > 0) {
            state_ = PARSE_METADATA;
        } else {
            current_interval_ = header_.icy_metaint;
        }
    } else if (state_ == PARSE_METADATA) {
        // if there is not enough data in buffer we should read more...
        if (metadata_length_ <= buffer_.length()) {
            GetMetadataField(buffer_.substr(0, metadata_length_), "StreamTitle", title_);

            buffer_.erase(0, metadata_length_);
            metadata_length_ = 0;
            current_interval_ = header_.icy_metaint;

            state_ = PARSE_DATA;
        }
    } else {
        std::cerr << "Invalid parsing state" << std::endl;
    }
}
