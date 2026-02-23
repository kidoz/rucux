// SPDX-License-Identifier: MIT
#include <ctype.h>

extern "C" {

int isalpha(int c) {
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f');
}

int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

int isxdigit(int c) {
    return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

int isascii(int c) {
    return (c >= 0 && c <= 127);
}

int toupper(int c) {
    if (islower(c)) return c - 'a' + 'A';
    return c;
}

int tolower(int c) {
    if (isupper(c)) return c - 'A' + 'a';
    return c;
}

} // extern "C"
