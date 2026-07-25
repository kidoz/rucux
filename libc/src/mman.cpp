extern "C" int shm_open(const char* name, int oflag, mode_t mode) {
    (void)name;
    (void)oflag;
    (void)mode;
    return -1;
}
extern "C" int shm_unlink(const char* name) {
    (void)name;
    return -1;
}
