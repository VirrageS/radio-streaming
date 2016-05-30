#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include "parser.h"
#include "misc.h"

/**
    Check if sequence '\r\n' is in string.

    @param str: String in which we search for sequence.
    @param pos: Position at which we expect this sequence to appear.
    @returns: True if sequence is presented, false otherwise.
    **/
static bool is_cr_present(char *str, int pos)
{
    if (str[pos-1] == '\r' && str[pos] == '\n')
        return true;
    else
        return false;
}

/**
    Get field from header.
    **/
static int get_http_header_field(char *header, const char* field, char* value)
{
    char *occurrence = strstr(header, field);
    int content_pos = strlen(field) + 1;

    if (!occurrence) {
        value[0] = '\0';
        return -1;
    }

    for (int i = content_pos; occurrence[i] != '\0'; i++) {
        if (is_cr_present(occurrence, i)) {
            // "<field>:" is deleted
            strncpy(value, occurrence + content_pos, i - content_pos);
            value[i - content_pos - 1] = '\0';

            return 0;
        }
    }

    // value has not been found
    value[0] = '\0';
    return -1;
}

/**
    Extract header from buffer.
    **/
static int extract_header_fields(header_t *header, char *buffer)
{
    int err;
    char metaint[256], response_code[256];

    err = get_http_header_field(buffer, "ICY", response_code);
    if (err < 0)
        return -1;

    if (strcmp(response_code, "200 OK") != 0)
        return -1;

    get_http_header_field(buffer, "icy-name", header->icy_name);
    get_http_header_field(buffer, "icy-notice1", header->icy_notice1);
    get_http_header_field(buffer, "icy-notice2", header->icy_notice2);
    get_http_header_field(buffer, "icy-genre", header->icy_genre);
    get_http_header_field(buffer, "icy-pub", header->icy_pub);
    get_http_header_field(buffer, "icy-br", header->icy_br);

    header->metaint = 0;
    err = get_http_header_field(buffer, "icy-metaint", metaint);
    if (err == 0) {
        header->metaint = strtoul(metaint, NULL, 10);

        if (errno == ERANGE)
            return -1;
    }

    return 0;
}

/**
    Get field from metadata
    **/
static int get_metadata_field(char *metadata, const char* field, char* value)
{
    char *split = strtok(metadata, ";");
    char *occurrence = NULL;

    while (split != NULL) {
        occurrence = strstr(split, field);

        if (occurrence != NULL) {
            unsigned int content_pos = strlen(field) + 2;
            unsigned int content_size = strlen(split) - content_pos - 1;

            strncpy(value, occurrence + content_pos, content_size);
            value[content_size] = '\0';

            return 0;
        }
        split = strtok(NULL, ";");
    }

    // Value hasn't been found
    value[0] = '\0';
    return -1;
}


/**
    Remove bytes from buffer
    **/
static int remove_from_buffer(stream_t *stream, size_t bytes_count)
{
    stream->in_buffer -= bytes_count;
    memmove(&stream->buffer[0], &stream->buffer[bytes_count], stream->in_buffer);
    return 0;
}


static int write_to_file(stream_t *stream, size_t bytes_count)
{
    if (stream->stream_on) {
        if (bytes_count == 0)
            return 0;

        debug_print("writing: %zu\n", bytes_count);
        size_t bytes_written = fwrite(&stream->buffer[0], sizeof(char), (size_t)bytes_count, stream->output_file);
        if (bytes_written != bytes_count) {
            syserr("Failed to write to file or stdout\n");
        }

        fflush(stream->output_file);
    }

    remove_from_buffer(stream, bytes_count);
    return 0;
}


static ssize_t check_metadata(stream_t *stream)
{
    // check if we should parse any meta data
    if (!stream->meta_data) {
        return stream->in_buffer;
    }

    if (stream->current_interval >= stream->in_buffer) {
        stream->current_interval -= stream->in_buffer;
        return stream->in_buffer;
    }

    // write until header
    write_to_file(stream, stream->current_interval);

    // read metada length
    unsigned int metadata_length = abs((int)stream->buffer[0]) * 16;

    // remove metadata length
    remove_from_buffer(stream, 1);

    // if metadata empty we move on
    if (metadata_length == 0) {
        goto end_checking;
    }

    // if there is not enough data in buffer we should read more...
    while (metadata_length > stream->in_buffer) {
        ssize_t bytes_received = poll_recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer);
        if (bytes_received < 0) {
            syserr("poll_recv() failed - parse_metadata");
        } else if (bytes_received == 0) {
            return -1;
        } else {
            stream->in_buffer += bytes_received;
        }
    }

    // extract title from metadata
    char metadata_content[MAX_METADATA_LENGTH];
    strncpy(metadata_content, stream->buffer, metadata_length);
    get_metadata_field(metadata_content, "StreamTitle", stream->title);

    debug_print("title: \"%s\"\n", stream->title);
    debug_print("metadata_length: %d\n", metadata_length);

    remove_from_buffer(stream, metadata_length);

end_checking:
    // update current interval between next header
    if (stream->in_buffer > stream->header.metaint) {
        stream->current_interval = 0;
        return stream->header.metaint;
    }

    stream->current_interval = stream->header.metaint - stream->in_buffer;
    return stream->in_buffer;
}


int print_header(header_t *header)
{
    debug_print("%s\n", "##################################");
    debug_print("Name\t: %s\n", header->icy_name);
    debug_print("icy-notice1\t: %s\n", header->icy_notice1);
    debug_print("icy-notice2\t: %s\n", header->icy_notice2);
    debug_print("Genre\t: %s\n", header->icy_genre);
    //debug_print("Public\t: %s\n", (header->icy_pub?"yes":"no"));
    debug_print("Bitrate : %s kbit/s\n", header->icy_br);
    debug_print("metaint\t: %lu\n", header->metaint);
    debug_print("%s\n", "##################################");
    return 0;
}


int parse_header(stream_t *stream)
{
    debug_print("%s\n", "parsing header...");

    while (true) {
        debug_print("socket: %d; buffer_size: %zu\n", stream->socket, sizeof(stream->buffer) - stream->in_buffer);
        ssize_t bytes_received = poll_recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer);
        if (bytes_received <= 0) {
            return -1;
        } else {
            ssize_t parse_point = -1;
            stream->in_buffer += bytes_received;

            for (size_t i = 3; i < stream->in_buffer; ++i) {
                if (stream->buffer[i-3] == '\r' && stream->buffer[i-2] == '\n' && stream->buffer[i-1] == '\r' && stream->buffer[i] == '\n') {
                    parse_point = i + 1;
                }
            }

            debug_print("%s\n", "got some header...");

            if (parse_point >= 0) {
                debug_print("%s\n", "parsing headers...");
                int err = extract_header_fields(&stream->header, stream->buffer);
                if (err < 0)
                    return -1;

                // check server decided not to send meta data
                if (stream->meta_data && (stream->header.metaint == 0L))
                    stream->meta_data = false;

                remove_from_buffer(stream, parse_point);
                break;
            }
        }
    }

    stream->current_interval = stream->header.metaint;
    return 0;
}


int parse_data(stream_t *stream)
{
    ssize_t bytes_received = poll_recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer);
    if (bytes_received > 0) {
        stream->in_buffer += bytes_received;

        while (stream->in_buffer > 0) {
            ssize_t bytes_to_write = check_metadata(stream);
            if (bytes_to_write < 0)
                return 0;

            write_to_file(stream, bytes_to_write);
        }
    }

    return bytes_received;
}
