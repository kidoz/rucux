// SPDX-License-Identifier: MIT
#include <locale.h>
#include <stddef.h>

extern "C" {

char *setlocale(int category, const char *locale) {
    (void)category; (void)locale;
    return (char*)"C";
}

static struct lconv g_lconv = {(char*)".", (char*)"", (char*)""};
struct lconv *localeconv(void) {
    return &g_lconv;
}

}
