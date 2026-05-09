#include <dlfcn.h>
#include <stddef.h>

extern "C" {

void *dlopen(const char *filename, int flag) {
    (void)filename;
    (void)flag;
    return NULL;
}

char *dlerror(void) {
    return (char*)"Dynamic loading not supported";
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle;
    (void)symbol;
    return NULL;
}

int dlclose(void *handle) {
    (void)handle;
    return -1;
}

}
