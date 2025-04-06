#ifndef TEXT_DECODE_UTILS_H
#define TEXT_DECODE_UTILS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void urldecode(const char *src, char *dst, size_t dst_len);

#ifdef __cplusplus
}
#endif

#endif