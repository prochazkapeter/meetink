#ifndef TEXT_DECODE_UTILS_H
#define TEXT_DECODE_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    char *remove_diacritics_utf8(const char *src);

#ifdef __cplusplus
}
#endif

#endif
