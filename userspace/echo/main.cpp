// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

static pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;
static int shared_counter = 0;

void* thread_func(void* arg) {
    int id = (int)(long)arg;
    for (int i = 0; i < 5; i++) {
        pthread_mutex_lock(&my_mutex);
        shared_counter++;
        printf("Thread %d: counter=%d\n", id, shared_counter);
        pthread_mutex_unlock(&my_mutex);
        sched_yield(); // Let other thread run
    }
    return NULL;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("Hello from POSIX Echo Process! Testing printf: %d\n", 42);

    // Test mmap
    printf("Testing mmap...\n");
    void* ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        printf("mmap failed!\n");
    } else {
        printf("mmap successful! Address: %p\n", ptr);
        char* str = (char*)ptr;
        str[0] = 'M';
        str[1] = 'M';
        str[2] = 'A';
        str[3] = 'P';
        str[4] = ' ';
        str[5] = 'O';
        str[6] = 'K';
        str[7] = '!';
        str[8] = '\n';
        str[9] = '\0';
        printf("%s", str);

        if (munmap(ptr, 4096) == 0) {
            printf("munmap successful!\n");
        } else {
            printf("munmap failed!\n");
        }
    }

    // Test Persistent Storage
    printf("Testing Persistent Storage (FAT32/ATA)...\n");
    int fd = open("/fat32/kernel.elf", 0);
    if (fd >= 0) {
        printf("Successfully opened /fat32/kernel.elf! fd=%d\n", fd);
        char buf[5] = {0};
        if (read(fd, buf, 4) == 4) {
            printf("Read 4 bytes: %c%c%c\n", buf[1], buf[2], buf[3]); // Should be 'E', 'L', 'F'
        } else {
            printf("Failed to read from file!\n");
        }
        close(fd);
    } else {
        printf("Failed to open /fat32/kernel.elf!\n");
    }

    // Test Sockets
    printf("Testing Networking (BSD Sockets via IPC)...\n");
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock >= 0) {
        printf("Successfully created socket! fd=%d\n", sock);

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        addr.sin_addr.s_addr = htonl(0x08080808); // 8.8.8.8

        int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        printf("connect() result = %d\n", res);

        const char* msg = "GET / HTTP/1.1\r\n\r\n";
        ssize_t sent = send(sock, msg, 18, 0);
        printf("send() result = %d bytes\n", (int)sent);

    } else {
        printf("Failed to create socket!\n");
    }

    // Test Multi-Threading
    printf("Testing Multi-Threading (pthreads & futex)...\n");
    pthread_t t1, t2;
    pthread_create(&t1, NULL, thread_func, (void*)(long)1);
    pthread_create(&t2, NULL, thread_func, (void*)(long)2);

    // Simple busy-wait since real join isn't fully robust yet
    while (shared_counter < 10) {
        sched_yield();
    }
    printf("Final shared counter: %d (expected 10)\n", shared_counter);

    return 0;
}
