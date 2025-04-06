#include "text_decode_utils.h"
#include <ctype.h> // isxdigit() function

void urldecode(const char *src, char *dst, size_t dst_len)
{
    unsigned int i = 0, j = 0; // Use unsigned int (or size_t) for indexing
    while (src[i] != '\0' && j < dst_len - 1)
    {
        if (src[i] == '%')
        {
            // Cast to unsigned char when calling isxdigit to avoid warnings
            if (isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2]))
            {
                char a = src[i + 1];
                char b = src[i + 2];
                a = (a >= 'a' ? a - 'a' + 10 : (a >= 'A' ? a - 'A' + 10 : a - '0'));
                b = (b >= 'a' ? b - 'a' + 10 : (b >= 'A' ? b - 'A' + 10 : b - '0'));
                dst[j++] = (char)(16 * a + b);
                i += 3;
            }
            else
            {
                dst[j++] = src[i++];
            }
        }
        else if (src[i] == '+')
        {
            dst[j++] = ' ';
            i++;
        }
        else
        {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}
