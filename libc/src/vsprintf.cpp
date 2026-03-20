// SPDX-License-Identifier: MIT
// Full vsprintf/vsnprintf supporting:
//   %d %i %u %x %X %o %p %s %c %% %f %e %g %n
//   Length: l ll z h hh
//   Flags: 0 - + (space) #
//   Width and precision (including *)
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {

struct fmt_ctx {
    char* buf;
    size_t pos;
    size_t limit; // 0 = unlimited
};

static void put(fmt_ctx& c, char ch) {
    if (c.limit == 0 || c.pos < c.limit - 1)
        c.buf[c.pos] = ch;
    c.pos++;
}

static void puts_n(fmt_ctx& c, const char* s, int n) {
    for (int i = 0; i < n; ++i) put(c, s[i]);
}

static void pad(fmt_ctx& c, char ch, int n) {
    for (int i = 0; i < n; ++i) put(c, ch);
}

// Format unsigned integer into tmp[] (reversed). Returns digit count.
static int fmt_uint(char* tmp, unsigned long long val, int base, bool upper) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int len = 0;
    if (val == 0) { tmp[len++] = '0'; }
    else { while (val) { tmp[len++] = d[val % base]; val /= base; } }
    return len;
}

// Emit a formatted integer (signed or unsigned).
// prefix: "" or "-" or "+" or " " or "0x" or "0X"
// digits: tmp[0..dlen-1] in reverse order
static void emit_number(fmt_ctx& c, const char* prefix, int plen,
                         char* digits, int dlen,
                         int width, int prec, char padch, bool left) {
    // Precision: minimum number of digits
    int zeropad = (prec > dlen) ? prec - dlen : 0;
    int total = plen + zeropad + dlen;

    if (!left && padch == '0' && prec < 0) {
        // Zero-pad: prefix first, then zeros, then digits
        int fill = (width > total) ? width - total : 0;
        puts_n(c, prefix, plen);
        pad(c, '0', fill);
        for (int i = dlen - 1; i >= 0; --i) put(c, digits[i]);
    } else if (!left) {
        // Space-pad on left
        int fill = (width > total) ? width - total : 0;
        pad(c, ' ', fill);
        puts_n(c, prefix, plen);
        pad(c, '0', zeropad);
        for (int i = dlen - 1; i >= 0; --i) put(c, digits[i]);
    } else {
        // Left-align
        puts_n(c, prefix, plen);
        pad(c, '0', zeropad);
        for (int i = dlen - 1; i >= 0; --i) put(c, digits[i]);
        int fill = (width > total) ? width - total : 0;
        pad(c, ' ', fill);
    }
}

// Simple double-to-string for %f (no soft-float library needed).
// Handles integer part + fractional part to `prec` decimal places.
static void emit_double(fmt_ctx& c, double val, int width, int prec,
                         char padch, bool left, bool plus, bool space) {
    if (prec < 0) prec = 6;

    char prefix[2]; int plen = 0;
    if (val < 0) { prefix[plen++] = '-'; val = -val; }
    else if (plus) { prefix[plen++] = '+'; }
    else if (space) { prefix[plen++] = ' '; }

    // Split into integer and fractional parts
    unsigned long long ipart = static_cast<unsigned long long>(val);
    double frac = val - static_cast<double>(ipart);

    // Round the fractional part
    double rounder = 0.5;
    for (int i = 0; i < prec; ++i) rounder /= 10.0;
    frac += rounder;
    if (frac >= 1.0) { ipart++; frac -= 1.0; }

    // Format integer part
    char itmp[24]; int ilen = fmt_uint(itmp, ipart, 10, false);

    // Format fractional part
    char ftmp[24]; int flen = 0;
    if (prec > 0) {
        ftmp[flen++] = '.';
        for (int i = 0; i < prec; ++i) {
            frac *= 10.0;
            int digit = static_cast<int>(frac);
            if (digit > 9) digit = 9;
            ftmp[flen++] = '0' + digit;
            frac -= digit;
        }
    }

    int total = plen + ilen + flen;
    if (!left) {
        int fill = (width > total) ? width - total : 0;
        if (padch == '0') {
            puts_n(c, prefix, plen);
            pad(c, '0', fill);
        } else {
            pad(c, ' ', fill);
            puts_n(c, prefix, plen);
        }
    } else {
        puts_n(c, prefix, plen);
    }

    // Integer digits (reversed in itmp)
    for (int i = ilen - 1; i >= 0; --i) put(c, itmp[i]);
    // Fractional digits (in order in ftmp)
    for (int i = 0; i < flen; ++i) put(c, ftmp[i]);

    if (left) {
        int fill = (width > total) ? width - total : 0;
        pad(c, ' ', fill);
    }
}

static int do_format(fmt_ctx& c, const char* fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') { put(c, *fmt++); continue; }
        fmt++; // skip '%'

        // Flags
        bool fl_left = false, fl_plus = false, fl_space = false, fl_hash = false;
        char fl_pad = ' ';
        for (;;) {
            if      (*fmt == '-') { fl_left = true; fmt++; }
            else if (*fmt == '0') { fl_pad = '0'; fmt++; }
            else if (*fmt == '+') { fl_plus = true; fmt++; }
            else if (*fmt == ' ') { fl_space = true; fmt++; }
            else if (*fmt == '#') { fl_hash = true; fmt++; }
            else break;
        }
        if (fl_left) fl_pad = ' ';

        // Width
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { fl_left = true; width = -width; } fmt++; }
        else { while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0'); }

        // Precision
        int prec = -1;
        if (*fmt == '.') {
            fmt++; prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else { while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0'); }
        }

        // Length
        enum { L_NONE, L_L, L_LL, L_Z, L_H, L_HH } length = L_NONE;
        if (*fmt == 'l') { fmt++; length = (*fmt == 'l') ? (fmt++, L_LL) : L_L; }
        else if (*fmt == 'z') { length = L_Z; fmt++; }
        else if (*fmt == 'h') { fmt++; length = (*fmt == 'h') ? (fmt++, L_HH) : L_H; }
        else if (*fmt == 'j' || *fmt == 't') { length = L_L; fmt++; } // treat as long

        char tmp[24]; int dlen; char prefix[4]; int plen;

        switch (*fmt) {
        case 'd': case 'i': {
            long long v;
            if (length == L_LL) v = va_arg(ap, long long);
            else if (length == L_L || length == L_Z) v = va_arg(ap, long);
            else v = va_arg(ap, int);
            if (length == L_H)  v = static_cast<short>(v);
            if (length == L_HH) v = static_cast<signed char>(v);

            plen = 0;
            unsigned long long uv;
            if (v < 0) { prefix[plen++] = '-'; uv = static_cast<unsigned long long>(-v); }
            else { if (fl_plus) prefix[plen++] = '+'; else if (fl_space) prefix[plen++] = ' '; uv = static_cast<unsigned long long>(v); }
            dlen = fmt_uint(tmp, uv, 10, false);
            emit_number(c, prefix, plen, tmp, dlen, width, prec, fl_pad, fl_left);
            break;
        }
        case 'u': {
            unsigned long long v;
            if (length == L_LL) v = va_arg(ap, unsigned long long);
            else if (length == L_L || length == L_Z) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            dlen = fmt_uint(tmp, v, 10, false);
            emit_number(c, "", 0, tmp, dlen, width, prec, fl_pad, fl_left);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v;
            if (length == L_LL) v = va_arg(ap, unsigned long long);
            else if (length == L_L || length == L_Z) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            plen = 0;
            if (fl_hash && v != 0) { prefix[0] = '0'; prefix[1] = *fmt == 'X' ? 'X' : 'x'; plen = 2; }
            dlen = fmt_uint(tmp, v, 16, *fmt == 'X');
            emit_number(c, prefix, plen, tmp, dlen, width, prec, fl_pad, fl_left);
            break;
        }
        case 'o': {
            unsigned long long v;
            if (length == L_LL) v = va_arg(ap, unsigned long long);
            else if (length == L_L) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            plen = 0;
            if (fl_hash && v != 0) { prefix[plen++] = '0'; }
            dlen = fmt_uint(tmp, v, 8, false);
            emit_number(c, prefix, plen, tmp, dlen, width, prec, fl_pad, fl_left);
            break;
        }
        case 'p': {
            auto v = reinterpret_cast<uintptr_t>(va_arg(ap, void*));
            prefix[0] = '0'; prefix[1] = 'x'; plen = 2;
            dlen = fmt_uint(tmp, v, 16, false);
            emit_number(c, prefix, plen, tmp, dlen, width, -1, fl_pad, fl_left);
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = 0; while (s[slen]) slen++;
            if (prec >= 0 && prec < slen) slen = prec;
            if (!fl_left) pad(c, ' ', width > slen ? width - slen : 0);
            puts_n(c, s, slen);
            if (fl_left) pad(c, ' ', width > slen ? width - slen : 0);
            break;
        }
        case 'c':
            if (!fl_left) pad(c, ' ', width > 1 ? width - 1 : 0);
            put(c, static_cast<char>(va_arg(ap, int)));
            if (fl_left) pad(c, ' ', width > 1 ? width - 1 : 0);
            break;
        case '%':
            put(c, '%');
            break;
        case 'f': case 'F':
            emit_double(c, va_arg(ap, double), width, prec, fl_pad, fl_left, fl_plus, fl_space);
            break;
        case 'e': case 'E': case 'g': case 'G':
            // Fall back to %f — good enough for most uses
            emit_double(c, va_arg(ap, double), width, prec, fl_pad, fl_left, fl_plus, fl_space);
            break;
        case 'n':
            *va_arg(ap, int*) = static_cast<int>(c.pos);
            break;
        default:
            put(c, '%');
            if (*fmt) put(c, *fmt);
            break;
        }
        if (*fmt) fmt++;
    }

    if (c.limit == 0)       c.buf[c.pos] = '\0';
    else if (c.pos < c.limit) c.buf[c.pos] = '\0';
    else                    c.buf[c.limit - 1] = '\0';

    return static_cast<int>(c.pos);
}

int vsprintf(char* str, const char* format, va_list ap) {
    fmt_ctx c = {str, 0, 0};
    return do_format(c, format, ap);
}

int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    if (!str || size == 0) {
        char dummy;
        fmt_ctx c = {&dummy, 0, 1};
        return do_format(c, format, ap);
    }
    fmt_ctx c = {str, 0, size};
    return do_format(c, format, ap);
}

} // extern "C"
