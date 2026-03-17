// SPDX-License-Identifier: MIT
#include <syslog.h>

extern "C" {

void openlog(const char* ident, int option, int facility) {
    (void)ident;
    (void)option;
    (void)facility;
}

void syslog(int priority, const char* format, ...) {
    (void)priority;
    (void)format;
}

void vsyslog(int priority, const char* format, void* ap) {
    (void)priority;
    (void)format;
    (void)ap;
}

void closelog(void) {}
}
