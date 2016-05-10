#ifndef __PARSER_H__
#define __PARSER_H__

#include "stream.h"

int parse_header(stream_t *stream);
int check_metadata(stream_t *stream);
int parse_data(stream_t *stream);

#endif
