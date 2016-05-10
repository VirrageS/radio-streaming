#ifndef __HEADER_H__
#define __HEADER_H__

#include <stdbool.h>

typedef struct
{
    char icy_name[500];
    char icy_notice1[500];
    char icy_notice2[500];
    char icy_genre[255];
    char icy_pub[10];
    char icy_br[10]; // bitrate

    char *ptr;            // Pointer used to parse header buffer
    char *buffer;         // Dynamic buffer with the whole http header.
    bool is_set;          // check if header is set or not
    unsigned int metaint; // MP3 data bytes between metadata blocks
} header_t;

int extract_header_fields(header_t *header, char *buffer);
int get_http_header_field(char *header, const char* field, char* value);
int get_metadata_field(char *metadata, const char* field, char* value);
bool is_cr_present(char *str, int pos);
int print_header(header_t *header);

#endif
