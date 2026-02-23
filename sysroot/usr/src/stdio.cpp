// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" {

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int fputs(const char* s, FILE* stream) {
    if (!s || !stream) return -1;
    // Real implementation would use the stream's fd, but for our stubs we just use write(2) if it's stderr, or 1 if
    // stdout. For now, since we only have basic stdout:
    return write(1, s, strlen(s));
}

int puts(const char* s) {
    size_t len = strlen(s);
    write(1, s, len);
    putchar('\n');
    return 0;
}

// A very simple printf
int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int count = 0;

    while (*format) {
        if (*format == '%') {
            format++;
            if (*format == 's') {
                const char* str = va_arg(args, const char*);
                size_t len = strlen(str);
                write(1, str, len);
                count += len;
            } else if (*format == 'c') {
                int c = va_arg(args, int);
                putchar(c);
                count++;
            } else if (*format == 'd') {
                int num = va_arg(args, int);
                char buf[32];
                int i = 0;
                if (num == 0) {
                    buf[i++] = '0';
                } else {
                    if (num < 0) {
                        putchar('-');
                        count++;
                        num = -num;
                    }
                    while (num > 0) {
                        buf[i++] = '0' + (num % 10);
                        num /= 10;
                    }
                }
                while (i > 0) {
                    putchar(buf[--i]);
                    count++;
                }
            } else if (*format == 'p') {
                void* ptr = va_arg(args, void*);
                unsigned long num = (unsigned long)ptr;
                char buf[32];
                int i = 0;
                if (num == 0) {
                    buf[i++] = '0';
                } else {
                    while (num > 0) {
                        int rem = num % 16;
                        if (rem < 10)
                            buf[i++] = '0' + rem;
                        else
                            buf[i++] = 'a' + (rem - 10);
                        num /= 16;
                    }
                }
                putchar('0');
                putchar('x');
                count += 2;
                while (i > 0) {
                    putchar(buf[--i]);
                    count++;
                }
            } else {
                putchar(*format);
                count++;
            }
        } else {
            putchar(*format);
            count++;
        }
        format++;
    }

    va_end(args);
    return count;
}

int sscanf(const char* str, const char* format, ...) {
    (void)str;
    (void)format;
    return 0; // stub
}

int vfprintf(FILE* stream, const char* format, void* ap) {
    (void)stream;
    (void)format;
    (void)ap;
    return 0; // stub
}

} // extern "C"
