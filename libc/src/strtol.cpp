// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <limits.h>

extern "C" {

// Helper: skip whitespace
static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v')
        s++;
    return s;
}

// Helper: detect base from prefix (0x, 0, or decimal)
static int detect_base(const char** s, int base) {
    if (base == 0) {
        if (**s == '0') {
            (*s)++;
            if (**s == 'x' || **s == 'X') { (*s)++; return 16; }
            return 8;
        }
        return 10;
    }
    if (base == 16 && **s == '0' && ((*s)[1] == 'x' || (*s)[1] == 'X'))
        *s += 2;
    return base;
}

// Helper: digit value (-1 if invalid)
static int digit_val(char c, int base) {
    int v;
    if (c >= '0' && c <= '9')      v = c - '0';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
    else return -1;
    return (v < base) ? v : -1;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    const char* s = skip_ws(nptr);
    bool neg = false;
    if (*s == '+') s++;
    else if (*s == '-') { neg = true; s++; }

    base = detect_base(&s, base);

    unsigned long long result = 0;
    bool any = false;

    while (true) {
        int d = digit_val(*s, base);
        if (d < 0) break;
        any = true;
        // Overflow check
        if (result > (ULLONG_MAX - d) / static_cast<unsigned long long>(base))
            result = ULLONG_MAX;
        else
            result = result * base + d;
        s++;
    }

    if (endptr) *endptr = const_cast<char*>(any ? s : nptr);
    return neg ? static_cast<unsigned long long>(-static_cast<long long>(result)) : result;
}

long long strtoll(const char* nptr, char** endptr, int base) {
    const char* s = skip_ws(nptr);
    bool neg = false;
    if (*s == '+') s++;
    else if (*s == '-') { neg = true; s++; }

    base = detect_base(&s, base);

    unsigned long long result = 0;
    bool any = false;

    while (true) {
        int d = digit_val(*s, base);
        if (d < 0) break;
        any = true;
        result = result * base + d;
        s++;
    }

    if (endptr) *endptr = const_cast<char*>(any ? s : nptr);

    if (neg) {
        if (result > static_cast<unsigned long long>(LLONG_MAX) + 1) return LLONG_MIN;
        return -static_cast<long long>(result);
    }
    if (result > static_cast<unsigned long long>(LLONG_MAX)) return LLONG_MAX;
    return static_cast<long long>(result);
}

long strtol(const char* nptr, char** endptr, int base) {
    return static_cast<long>(strtoll(nptr, endptr, base));
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    return static_cast<unsigned long>(strtoull(nptr, endptr, base));
}

double strtod(const char* nptr, char** endptr) {
    const char* s = skip_ws(nptr);
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') s++;

    double result = 0.0;
    bool any = false;

    // Integer part
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (*s - '0');
        s++;
        any = true;
    }

    // Fractional part
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            result += (*s - '0') * frac;
            frac *= 0.1;
            s++;
            any = true;
        }
    }

    // Exponent
    if (*s == 'e' || *s == 'E') {
        s++;
        bool eneg = false;
        if (*s == '-') { eneg = true; s++; }
        else if (*s == '+') s++;
        int exp = 0;
        while (*s >= '0' && *s <= '9') {
            exp = exp * 10 + (*s - '0');
            s++;
        }
        double mult = 1.0;
        for (int i = 0; i < exp; ++i) mult *= 10.0;
        if (eneg) result /= mult;
        else      result *= mult;
    }

    if (endptr) *endptr = const_cast<char*>(any ? s : nptr);
    return neg ? -result : result;
}

float strtof(const char* nptr, char** endptr) {
    return static_cast<float>(strtod(nptr, endptr));
}

long double strtold(const char* nptr, char** endptr) {
    return static_cast<long double>(strtod(nptr, endptr));
}

} // extern "C"
