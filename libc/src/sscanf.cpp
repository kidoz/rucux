// SPDX-License-Identifier: MIT
// Basic sscanf supporting: %d %i %u %x %o %s %c %ld %lu %lx %lld %llu %llx %n %%
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int sscanf(const char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int matched = 0;
    const char* s = str;

    while (*format && *s) {
        // Skip whitespace in format
        if (is_space(*format)) {
            format++;
            while (is_space(*s)) s++;
            continue;
        }

        // Literal match
        if (*format != '%') {
            if (*s != *format) break;
            s++;
            format++;
            continue;
        }

        format++; // skip '%'

        if (*format == '%') {
            if (*s != '%') break;
            s++; format++;
            continue;
        }

        // Length modifier
        enum { LEN_NONE, LEN_L, LEN_LL } length = LEN_NONE;
        if (*format == 'l') {
            format++;
            if (*format == 'l') { length = LEN_LL; format++; }
            else length = LEN_L;
        } else if (*format == 'h') {
            format++;
            if (*format == 'h') format++;
        }

        switch (*format) {
        case 'd':
        case 'i': {
            char* end;
            long long val = strtoll(s, &end, (*format == 'i') ? 0 : 10);
            if (end == s) goto done;
            s = end;
            if (length == LEN_LL)     *va_arg(ap, long long*) = val;
            else if (length == LEN_L) *va_arg(ap, long*) = static_cast<long>(val);
            else                      *va_arg(ap, int*) = static_cast<int>(val);
            matched++;
            break;
        }
        case 'u': {
            char* end;
            unsigned long long val = strtoull(s, &end, 10);
            if (end == s) goto done;
            s = end;
            if (length == LEN_LL)     *va_arg(ap, unsigned long long*) = val;
            else if (length == LEN_L) *va_arg(ap, unsigned long*) = static_cast<unsigned long>(val);
            else                      *va_arg(ap, unsigned int*) = static_cast<unsigned int>(val);
            matched++;
            break;
        }
        case 'x':
        case 'X': {
            char* end;
            unsigned long long val = strtoull(s, &end, 16);
            if (end == s) goto done;
            s = end;
            if (length == LEN_LL)     *va_arg(ap, unsigned long long*) = val;
            else if (length == LEN_L) *va_arg(ap, unsigned long*) = static_cast<unsigned long>(val);
            else                      *va_arg(ap, unsigned int*) = static_cast<unsigned int>(val);
            matched++;
            break;
        }
        case 'o': {
            char* end;
            unsigned long long val = strtoull(s, &end, 8);
            if (end == s) goto done;
            s = end;
            if (length == LEN_LL)     *va_arg(ap, unsigned long long*) = val;
            else if (length == LEN_L) *va_arg(ap, unsigned long*) = static_cast<unsigned long>(val);
            else                      *va_arg(ap, unsigned int*) = static_cast<unsigned int>(val);
            matched++;
            break;
        }
        case 's': {
            char* dst = va_arg(ap, char*);
            while (*s && !is_space(*s)) *dst++ = *s++;
            *dst = '\0';
            matched++;
            break;
        }
        case 'c': {
            *va_arg(ap, char*) = *s++;
            matched++;
            break;
        }
        case 'n': {
            *va_arg(ap, int*) = static_cast<int>(s - str);
            break; // %n doesn't count as a match
        }
        case 'f':
        case 'e':
        case 'g': {
            char* end;
            double val = strtod(s, &end);
            if (end == s) goto done;
            s = end;
            if (length == LEN_L) *va_arg(ap, double*) = val;
            else                 *va_arg(ap, float*) = static_cast<float>(val);
            matched++;
            break;
        }
        default:
            goto done;
        }
        format++;
    }

done:
    va_end(ap);
    return matched;
}

} // extern "C"
