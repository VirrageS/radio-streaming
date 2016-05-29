#ifndef __PARSER_H__
#define __PARSER_H__

#include "stream.h"

int print_header(header_t *header);

int parse_header(stream_t *stream);
int parse_data(stream_t *stream);

#endif
