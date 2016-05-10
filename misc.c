#include "misc.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "err.h"

int strtob(bool* b, const char* str)
{
    if (strcmp(str, "yes") == 0) {
        *b = true;
        return 0;
    } else if (strcmp(str, "no") == 0) {
        *b = false;
        return 0;
    }

    return 1;
}
