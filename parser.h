#ifndef __PARSER_H__
#define __PARSER_H__

#include "stream.h"

// int extract_header_fields(header_t *header, char *buffer, bool meta_data);
int get_http_header_field(char *header, const char* field, char* value);
// int get_metadata_field(char *metadata, const char* field, char* value);
int print_header(header_t *header);

int parse_header(stream_t *stream);
int parse_data(stream_t *stream);

#endif
