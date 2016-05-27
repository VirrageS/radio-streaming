#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "header.h"

static bool is_cr_present(char *str, int pos)
{
    if (str[pos-1] == '\r' && str[pos] == '\n')
        return true;
    else
        return false;
}

int extract_header_fields(header_t *header, char *buffer, bool meta_data)
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

int get_metadata_field(char *metadata, const char* field, char* value)
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
