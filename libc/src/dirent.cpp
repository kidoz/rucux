// SPDX-License-Identifier: MIT
#include <dirent.h>

extern "C" {

DIR* opendir(const char* name) {
    (void)name;
    return nullptr; // stub
}

struct dirent* readdir(DIR* dirp) {
    (void)dirp;
    return nullptr; // stub
}

int closedir(DIR* dirp) {
    (void)dirp;
    return -1; // stub
}
}