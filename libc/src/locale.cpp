// SPDX-License-Identifier: MIT
#include <locale.h>
#include <stddef.h>

extern "C" {

char* setlocale(int category, const char* locale) {
    (void)category;
    (void)locale;
    return (char*)"C";
}

static struct lconv g_lconv = {
    (char*)".", // decimal_point
    (char*)"",  // thousands_sep
    (char*)"",  // grouping
    (char*)"",  // int_curr_symbol
    (char*)"",  // currency_symbol
    (char*)"",  // mon_decimal_point
    (char*)"",  // mon_thousands_sep
    (char*)"",  // mon_grouping
    (char*)"",  // positive_sign
    (char*)"",  // negative_sign
    127,        // int_frac_digits
    127,        // frac_digits
    127,        // p_cs_precedes
    127,        // p_sep_by_space
    127,        // n_cs_precedes
    127,        // n_sep_by_space
    127,        // p_sign_posn
    127,        // n_sign_posn
    127,        // int_p_cs_precedes
    127,        // int_p_sep_by_space
    127,        // int_n_cs_precedes
    127,        // int_n_sep_by_space
    127,        // int_p_sign_posn
    127         // int_n_sign_posn
};
struct lconv* localeconv(void) {
    return &g_lconv;
}
}
