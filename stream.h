#ifndef __STREAM_H__
#define __STREAM_H__

#include <string>
#include <ostream>
#include <algorithm>
#include <memory>


enum StateTypes {
    PARSE_HEADER,
    PARSE_DATA,
    PARSE_METADATA
};


struct Header
{
    std::string icy_name; // name of stream
    std::string icy_notice1;
    std::string icy_notice2;
    std::string icy_genre;
    std::string icy_pub;
    std::string icy_bitrate;
    unsigned long icy_metaint; // MP3 data bytes between metadata blocks

    std::string to_string() const;
};


class Stream
{
public:

    Stream();
    Stream(FILE* stream, bool metadata);
    Stream(const Stream& other);

    Stream& operator=(Stream other)
    {
        std::swap(stream_, other.stream_);
        std::swap(buffer_, other.buffer_);
        std::swap(socket_, other.socket_);
        std::swap(header_, other.header_);
        std::swap(current_interval_, other.current_interval_);
        std::swap(metadata_length_, other.metadata_length_);
        std::swap(title_, other.title_);
        std::swap(state_, other.state_);

        return *this;
    }

    virtual ~Stream() noexcept;

    bool paused() const { return paused_; }
    void paused(bool paused) { paused_ = paused; }
    bool quiting() const { return quiting_; }
    void quiting(bool quiting) { quiting_ = quiting; }

    std::string title() const { return title_; }

    bool SendRequest(const std::string& path);

    /**
        Set client socket for stream. Connects to ICY server on `host` and `port`.

        @param stream: Stream on which we want to make connection.
        @param host: Host on which ICY server is listening.
        @param port: Port on which ICY server is listening.
        @returns: 0 if connection was successful, -1 otherwise.
        **/
    int InitializeSocket(std::string host, std::string port);

    void Listen();


private:
    void ParseData();
    void WriteToStream(size_t bytes_count);
    bool ExtractHeaderFields();


private:
    FILE* stream_;
    std::string buffer_; // buffer in which we store all the data

    int socket_; // socket on which we listen to ICY-Data

    Header header_; // paresed ICY-Header

    unsigned int current_interval_; // stores length of data to next ICY-MetaData header
    unsigned int metadata_length_;
    std::string title_; // current ICY-Title

    StateTypes state_;

    bool metadata_;

    std::atomic_bool paused_; // check if stream is "playing" or "paused"
    std::atomic_bool quiting_; // check if stream is quiting
};

#endif
