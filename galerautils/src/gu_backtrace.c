// Copyright (C) 2012 Codership Oy <info@codership.com>

#include "gu_backtrace.h"
#include "gu_log.h"

#ifdef __GNUC__
#include <execinfo.h>
#include <stdlib.h>

char** gu_backtrace(int* size)
{
    char** strings;
    void** array = malloc(*size * sizeof(void*));
    if (!array)
    {
        gu_error("could not allocate memory for %d pointers\n", *size);
        return NULL;
    }
    *size = backtrace(array, *size);
    strings = backtrace_symbols(array, *size);

    free(array);
    return strings;
}
#else
char **gu_backtrace(int* size)
{
    return NULL;
}
#endif /* */

