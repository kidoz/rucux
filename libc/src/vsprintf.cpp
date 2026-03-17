// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdio.h>

extern "C" {

int vsprintf(char* str, const char* format, va_list ap) {
    // A very simple vsprintf that only supports %s, %d, %x, %p
    char* s = str;
    const char* f = format;

    while (*f) {
        if (*f == '%') {
            f++;
            if (*f == 's') {
                const char* val = va_arg(ap, const char*);
                while (*val) *s++ = *val++;
            } else if (*f == 'd') {
                int val = va_arg(ap, int);
                if (val < 0) {
                    *s++ = '-';
                    val = -val;
                }
                if (val == 0) {
                    *s++ = '0';
                } else {
                    char buf[10];
                    int i = 0;
                    while (val) {
                        buf[i++] = (val % 10) + '0';
                        val /= 10;
                    }
                    while (i--) *s++ = buf[i];
                }
            } else if (*f == 'x' || *f == 'p') {
                unsigned long val;
                if (*f == 'p') val = (unsigned long)va_arg(ap, void*);
                else val = va_arg(ap, unsigned int);

                if (val == 0) {
                    *s++ = '0';
                } else {
                    char buf[16];
                    int i = 0;
                    while (val) {
                        int nibble = val & 0xf;
                        buf[i++] = nibble < 10 ? nibble + '0' : nibble - 10 + 'a';
                        val >>= 4;
                    }
                    while (i--) *s++ = buf[i];
                }
            } else if (*f == '%') {
                *s++ = '%';
            }
        } else {
            *s++ = *f;
        }
        f++;
    }
    *s = '\0';
    return s - str;
}

}
