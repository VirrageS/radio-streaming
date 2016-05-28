#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "parser.h"
#include "misc.h"

static bool is_cr_present(char *str, int pos)
{
    if (str[pos-1] == '\r' && str[pos] == '\n')
        return true;
    else
        return false;
}

static int extract_header_fields(header_t *header, char *buffer, bool meta_data)
{
    int err;
    char metaint[256], response_code[256];

    err = get_http_header_field(buffer, "ICY", response_code);
    if (err != 0)
        return 1;

    if (strcmp(response_code, "200 OK") != 0)
        return 1;

    get_http_header_field(buffer, "icy-name", header->icy_name);
    get_http_header_field(buffer, "icy-notice1", header->icy_notice1);
    get_http_header_field(buffer, "icy-notice2", header->icy_notice2);
    get_http_header_field(buffer, "icy-genre", header->icy_genre);
    get_http_header_field(buffer, "icy-pub", header->icy_pub);
    get_http_header_field(buffer, "icy-br", header->icy_br);

    header->metaint = -1;
    if (meta_data) {
        err = get_http_header_field(buffer, "icy-metaint", metaint);
        if (err != 0)
            return 1;

        header->metaint = atoi(metaint);
    }

    header->is_set = true;
    return 0;
}

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
    return 1;
}


int get_http_header_field(char *header, const char* field, char* value)
{
    char *occurrence = strstr(header, field);
    int content_pos = strlen(field) + 1;

    if (!occurrence) {
        value[0] = '\0';
        return 1;
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
    return 1;
}


int print_header(header_t *header)
{
    printf("##################################\n");
    printf("Name\t: %s\n", header->icy_name);
    printf("icy-notice1\t: %s\n", header->icy_notice1);
    printf("icy-notice2\t: %s\n", header->icy_notice2);
    printf("Genre\t: %s\n", header->icy_genre);
    //printf("Public\t: %s\n", (header->icy_pub?"yes":"no"));
    printf("Bitrate : %s kbit/s\n", header->icy_br);
    printf("metaint\t: %d\n", header->metaint);
    printf("##################################\n");
    return 0;
}


int parse_header(stream_t *stream)
{
    debug_print("%s\n", "parsing header...");
    while (true) {
        debug_print("socket: %d; buffer_size: %zu\n", stream->socket, sizeof(stream->buffer) - stream->in_buffer);
        ssize_t bytes_received = poll_recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer);
        if (bytes_received < 0) {
            syserr("poll_recv() failed");
        } else if (bytes_received == 0) {
            syserr("poll_recv() connection closed");
        } else {
            int parse_point = -1;
            stream->in_buffer += bytes_received;

            for (size_t i = 3; i < stream->in_buffer; ++i) {
                if (stream->buffer[i-3] == '\r' && stream->buffer[i-2] == '\n' && stream->buffer[i-1] == '\r' && stream->buffer[i] == '\n') {
                    parse_point = i + 1;
                }
            }

            debug_print("%s\n", "got some header...");

            if (parse_point >= 0) {
                debug_print("%s\n", "parsing headers...");
                int err = extract_header_fields(&stream->header, stream->buffer, stream->meta_data);
                if (err < 0) {
                    syserr("Could not parse header\n");
                }


                if (stream->header.metaint == 0) {
                    syserr("Could not find metaint information\n");
                }

                stream->in_buffer -= parse_point;
                memmove(&stream->buffer[0], &stream->buffer[parse_point], stream->in_buffer);
                break;
            }
        }
    }

    stream->current_interval = stream->header.metaint;
    return 0;
}


int check_metadata(stream_t *stream)
{
    // check if we should parse any meta data
    if (!stream->meta_data)
        return 0;

    if (stream->current_interval >= stream->in_buffer) {
        stream->current_interval -= stream->in_buffer;
        return 0;
    }

    // write until header
    write_to_file(stream, stream->current_interval);

    // read metada length
    unsigned int metadata_length = abs((int)stream->buffer[0]) * 16;

    // remove metadata length
    remove_from_buffer(stream, 1);

    // if metadata empty we move on
    if (metadata_length == 0) {
        stream->current_interval = stream->header.metaint - stream->in_buffer;
        return 0;
    }

    // if there is not enough data in buffer we should read more...
    while (metadata_length > stream->in_buffer) {
        ssize_t bytes_received = poll_recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer);
        if (bytes_received < 0) {
            syserr("poll_recv() failed");
        } else if (bytes_received == 0) {
            debug_print("%s\n", "not recieving anything (metadata)...");
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

    // update current interval between next header
    stream->current_interval = stream->header.metaint - stream->in_buffer;
    return 0;
}


int parse_data(stream_t *stream)
{
    ssize_t bytes_received = poll_recv(stream->socket, &stream->buffer[stream->in_buffer], sizeof(stream->buffer) - stream->in_buffer);
    if (bytes_received < 0) {
        syserr("poll_recv() failed");
    } else if (bytes_received == 0) {
        return -1;
    } else {
        stream->in_buffer += bytes_received;

        int err = check_metadata(stream);
        if (err < 0)
            return -1;

        write_to_file(stream, stream->in_buffer);
    }

    return 0;
}
