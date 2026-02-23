// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// A very simple mmap-based malloc
// A real libc uses a complex arena allocator like dlmalloc.

struct chunk_header {
    size_t size;
    int is_free;
    chunk_header* next;
};

static chunk_header* g_head = nullptr;

extern "C" {

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
    (void)nptr;
    return 0; // stub
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

} // extern "C"
