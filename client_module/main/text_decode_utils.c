#include "text_decode_utils.h"

/**
 * @brief Remove diacritics from a UTF-8 encoded string.
 *
 * This function decodes the UTF-8 input string and converts specific Czech
 * characters with diacritics into their plain ASCII counterparts.
 *
 * For example:
 *     ("Břicháček") becomes "Brichacek"
 *
 * The returned string is dynamically allocated and must be freed by the caller.
 *
 * @param src The source UTF-8 string.
 * @return A new string without diacritics, or NULL on allocation failure.
 */
char *remove_diacritics_utf8(const char *src)
{
    if (src == NULL)
    {
        return NULL;
    }
    size_t len = strlen(src);
    // Allocate worst-case same length as source.
    char *dest = (char *)malloc(len + 1);
    if (!dest)
    {
        return NULL;
    }
    const unsigned char *s = (const unsigned char *)src;
    char *d = dest;

    while (*s)
    {
        uint32_t codepoint = 0;
        int char_len = 1;

        // If ASCII, simply copy.
        if (*s < 0x80)
        {
            codepoint = *s;
            char_len = 1;
        }
        // 2-byte sequence.
        else if ((*s & 0xE0) == 0xC0)
        {
            codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            char_len = 2;
        }
        // 3-byte sequence.
        else if ((*s & 0xF0) == 0xE0)
        {
            codepoint = ((s[0] & 0x0F) << 12) |
                        ((s[1] & 0x3F) << 6) |
                        (s[2] & 0x3F);
            char_len = 3;
        }
        // For simplicity, if longer than 3 bytes, copy as is.
        else
        {
            codepoint = *s;
            char_len = 1;
        }

        // Map known Czech diacritic codepoints to their plain ASCII equivalent.
        char mapped = 0;
        switch (codepoint)
        {
        // Lowercase letters
        case 0x00E1: // á
            mapped = 'a';
            break;
        case 0x010D: // č
            mapped = 'c';
            break;
        case 0x0159: // ř
            mapped = 'r';
            break;
        case 0x00E9: // é
            mapped = 'e';
            break;
        case 0x00ED: // í
            mapped = 'i';
            break;
        case 0x011B: // ě
            mapped = 'e';
            break;
        case 0x0161: // š
            mapped = 's';
            break;
        case 0x0165: // ť
            mapped = 't';
            break;
        case 0x017E: // ž
            mapped = 'z';
            break;
        case 0x010F: // ď
            mapped = 'd';
            break;
        case 0x00F3: // ó
            mapped = 'o';
            break;
        case 0x016F: // ů
            mapped = 'u';
            break;
        case 0x00FD: // ý
            mapped = 'y';
            break;
        // Uppercase letters
        case 0x00C1: // Á
            mapped = 'A';
            break;
        case 0x010C: // Č
            mapped = 'C';
            break;
        case 0x0158: // Ř
            mapped = 'R';
            break;
        case 0x00C9: // É
            mapped = 'E';
            break;
        case 0x00CD: // Í
            mapped = 'I';
            break;
        case 0x011A: // Ě
            mapped = 'E';
            break;
        case 0x0160: // Š
            mapped = 'S';
            break;
        case 0x0164: // Ť
            mapped = 'T';
            break;
        case 0x017D: // Ž
            mapped = 'Z';
            break;
        case 0x010E: // Ď
            mapped = 'D';
            break;
        case 0x0147: // Ň
            mapped = 'N';
            break;
        case 0x00D3: // Ó
            mapped = 'O';
            break;
        case 0x00DA: // Ú
            mapped = 'U';
            break;
        case 0x016E: // Ů
            mapped = 'U';
            break;
        case 0x00DD: // Ý
            mapped = 'Y';
            break;
        default:
            mapped = 0; // No mapping
            break;
        }

        if (mapped)
        {
            *d++ = mapped;
        }
        else
        {
            // If no mapping available, copy the original UTF-8 bytes.
            for (int i = 0; i < char_len; i++)
            {
                *d++ = s[i];
            }
        }
        s += char_len;
    }

    *d = '\0';
    return dest;
}