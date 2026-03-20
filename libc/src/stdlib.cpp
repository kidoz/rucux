// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// A very simple mmap-based malloc
// A real libc uses a complex arena allocator like dlmalloc.

struct chunk_header {
    size_t size;
    int is_free;
    chunk_header* next;
};

static chunk_header* g_head = nullptr;

extern "C" {

uintptr_t __stack_chk_guard = 0x595e9fbd94fda766;

void abort(void) {
    // We could print something here if we had stderr wired up simply
    _exit(1);
    while (1) {}
}

void __attribute__((noreturn)) __stack_chk_fail(void) {
    abort();
}

void* malloc(size_t size) {
    if (size == 0) return nullptr;

    // Simple 16-byte alignment
    size = (size + sizeof(chunk_header) + 15) & ~15;

    chunk_header* curr = g_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            curr->is_free = 0;
            return (void*)((char*)curr + sizeof(chunk_header));
        }
        curr = curr->next;
    }

    // No free chunk, map new pages
    size_t map_size = (size + 4095) & ~4095; // Page align
    void* ptr = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;

    chunk_header* new_chunk = (chunk_header*)ptr;
    new_chunk->size = map_size;
    new_chunk->is_free = 0;
    new_chunk->next = g_head;
    g_head = new_chunk;

    return (void*)((char*)new_chunk + sizeof(chunk_header));
}

void free(void* ptr) {
    if (!ptr) return;
    chunk_header* header = (chunk_header*)((char*)ptr - sizeof(chunk_header));
    header->is_free = 1;
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return nullptr;
    }

    chunk_header* header = (chunk_header*)((char*)ptr - sizeof(chunk_header));
    if (header->size - sizeof(chunk_header) >= size) {
        return ptr;
    }

    void* new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, header->size - sizeof(chunk_header));
        free(ptr);
    }
    return new_ptr;
}

const char* getprogname(void) {
    return "app"; // stub
}

int atoi(const char* nptr) {
    return (int)strtol(nptr, nullptr, 10);
}

long atol(const char* nptr) {
    return strtol(nptr, nullptr, 10);
}

double atof(const char* nptr) {
    return strtod(nptr, nullptr);
}

// strtod is now implemented in strtol.cpp

int abs(int j) {
    return j < 0 ? -j : j;
}

// ─── Environment variables ─────────────────────────────────────────────────
// Simple key=value store for a freestanding environment.

static constexpr int MAX_ENV = 32;
static char* g_environ[MAX_ENV + 1] = {nullptr}; // NULL-terminated

// Default environment — set before main()
static char env_term[] = "TERM=linux";
static char env_home[] = "HOME=/";
static char env_path[] = "PATH=/bin";

static bool g_env_initialized = false;

static void ensure_env_init() {
    if (g_env_initialized) return;
    g_env_initialized = true;
    g_environ[0] = env_term;
    g_environ[1] = env_home;
    g_environ[2] = env_path;
    g_environ[3] = nullptr;
}

static size_t env_strlen(const char* s) {
    const char* p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

char* getenv(const char* name) {
    ensure_env_init();
    if (!name) return nullptr;
    size_t len = env_strlen(name);
    for (int i = 0; g_environ[i]; ++i) {
        // Check if g_environ[i] starts with "name="
        bool match = true;
        for (size_t j = 0; j < len; ++j) {
            if (g_environ[i][j] != name[j]) { match = false; break; }
        }
        if (match && g_environ[i][len] == '=')
            return &g_environ[i][len + 1];
    }
    return nullptr;
}

int setenv(const char* name, const char* value, int overwrite) {
    ensure_env_init();
    if (!name || !value) return -1;
    size_t nlen = env_strlen(name);
    size_t vlen = env_strlen(value);

    // Check if already exists
    for (int i = 0; g_environ[i]; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            if (g_environ[i][j] != name[j]) { match = false; break; }
        }
        if (match && g_environ[i][nlen] == '=') {
            if (!overwrite) return 0;
            // Replace (leak old — acceptable for small env)
            char* buf = static_cast<char*>(malloc(nlen + vlen + 2));
            if (!buf) return -1;
            for (size_t j = 0; j < nlen; ++j) buf[j] = name[j];
            buf[nlen] = '=';
            for (size_t j = 0; j < vlen; ++j) buf[nlen + 1 + j] = value[j];
            buf[nlen + 1 + vlen] = '\0';
            g_environ[i] = buf;
            return 0;
        }
    }

    // Add new entry
    int count = 0;
    while (g_environ[count]) count++;
    if (count >= MAX_ENV) return -1;

    char* buf = static_cast<char*>(malloc(nlen + vlen + 2));
    if (!buf) return -1;
    for (size_t j = 0; j < nlen; ++j) buf[j] = name[j];
    buf[nlen] = '=';
    for (size_t j = 0; j < vlen; ++j) buf[nlen + 1 + j] = value[j];
    buf[nlen + 1 + vlen] = '\0';
    g_environ[count] = buf;
    g_environ[count + 1] = nullptr;
    return 0;
}

int putenv(char* string) {
    ensure_env_init();
    if (!string) return -1;
    // Find the '='
    const char* eq = string;
    while (*eq && *eq != '=') eq++;
    if (!*eq) return -1;
    size_t nlen = (size_t)(eq - string);

    // Check if exists
    for (int i = 0; g_environ[i]; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            if (g_environ[i][j] != string[j]) { match = false; break; }
        }
        if (match && g_environ[i][nlen] == '=') {
            g_environ[i] = string;
            return 0;
        }
    }

    int count = 0;
    while (g_environ[count]) count++;
    if (count >= MAX_ENV) return -1;
    g_environ[count] = string;
    g_environ[count + 1] = nullptr;
    return 0;
}

int unsetenv(const char* name) {
    ensure_env_init();
    if (!name) return -1;
    size_t len = env_strlen(name);
    for (int i = 0; g_environ[i]; ++i) {
        bool match = true;
        for (size_t j = 0; j < len; ++j) {
            if (g_environ[i][j] != name[j]) { match = false; break; }
        }
        if (match && g_environ[i][len] == '=') {
            // Shift remaining entries
            for (int j = i; g_environ[j]; ++j)
                g_environ[j] = g_environ[j + 1];
            return 0;
        }
    }
    return 0;
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    if (nmemb <= 1) return;
    char* arr = (char*)base;
    void* tmp = malloc(size);
    if (!tmp) return;
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - i - 1; j++) {
            if (compar(arr + j * size, arr + (j + 1) * size) > 0) {
                memcpy(tmp, arr + j * size, size);
                memcpy(arr + j * size, arr + (j + 1) * size, size);
                memcpy(arr + (j + 1) * size, tmp, size);
            }
        }
    }
    free(tmp);
}

uint32_t arc4random(void) {
    return 42; // stub: guaranteed to be random
}

void arc4random_buf(void* buf, size_t nbytes) {
    memset(buf, 42, nbytes);
}

uint32_t arc4random_uniform(uint32_t upper_bound) {
    return upper_bound ? arc4random() % upper_bound : 0;
}

static unsigned int g_seed = 1;
int rand(void) {
    g_seed = g_seed * 1103515245 + 12345;
    return (unsigned int)(g_seed / 65536) % 32768;
}

void srand(unsigned int seed) {
    g_seed = seed;
}

long random(void) {
    return rand();
}

void srandom(unsigned int seed) {
    srand(seed);
}

void srand48(long int seedval) {
    srand(seedval);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    (void)alignment;
    if (!memptr) return 22; // EINVAL
    void *ptr = malloc(size);
    if (!ptr) return 12; // ENOMEM
    *memptr = ptr;
    return 0;
}

void* aligned_alloc(size_t alignment, size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) == 0) {
        return ptr;
    }
    return nullptr;
}

char *realpath(const char *path, char *resolved_path) {
    if (!path) return nullptr;
    if (resolved_path) {
        strcpy(resolved_path, path);
        return resolved_path;
    }
    return strdup(path); // stub
}

} // extern "C"
