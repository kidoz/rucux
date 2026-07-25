// SPDX-License-Identifier: MIT
#include <fenv.h>

extern "C" {

int feclearexcept(int excepts) {
    (void)excepts;
    return 0;
}
int fegetexceptflag(fexcept_t* flagp, int excepts) {
    (void)flagp;
    (void)excepts;
    return 0;
}
int feraiseexcept(int excepts) {
    (void)excepts;
    return 0;
}
int fesetexceptflag(const fexcept_t* flagp, int excepts) {
    (void)flagp;
    (void)excepts;
    return 0;
}
int fetestexcept(int excepts) {
    (void)excepts;
    return 0;
}
int fegetround(void) {
    return 0;
}
int fesetround(int round) {
    (void)round;
    return 0;
}
int fegetenv(fenv_t* envp) {
    (void)envp;
    return 0;
}
int feholdexcept(fenv_t* envp) {
    (void)envp;
    return 0;
}
int fesetenv(const fenv_t* envp) {
    (void)envp;
    return 0;
}
int feupdateenv(const fenv_t* envp) {
    (void)envp;
    return 0;
}
}