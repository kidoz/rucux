#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

size_t parse_octal(const char* str, size_t len) {
    size_t result = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            result = (result << 3) + (str[i] - '0');
        }
    }
    return result;
}

void create_dir_recursive(const char* path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

bool verify_checksum(const char* block) {
    size_t sum = 0;
    for (int i = 0; i < 512; i++) {
        if (i >= 148 && i < 156) {
            sum += 32; // space
        } else {
            sum += ((unsigned char*)block)[i];
        }
    }
    struct tar_header* hdr = (struct tar_header*)block;
    size_t chksum = parse_octal(hdr->chksum, 8);
    return sum == chksum;
}

int extract_tar(const char* archive_path, int manifest_fd) {
    int fd = open(archive_path, O_RDONLY);
    if (fd < 0) {
        printf("error: failed to open %s\n", archive_path);
        return 1;
    }

    struct tar_header header;
    char block[512];

    while (true) {
        ssize_t bytes_read = read(fd, block, 512);
        if (bytes_read <= 0) break;

        bool is_zero = true;
        for (int i = 0; i < 512; i++) {
            if (block[i] != 0) {
                is_zero = false;
                break;
            }
        }
        if (is_zero) break; // End of archive marker

        if (!verify_checksum(block)) {
            printf("error: invalid tar header checksum\n");
            return 1;
        }

        memcpy(&header, block, sizeof(struct tar_header));
        size_t file_size = parse_octal(header.size, 12);
        
        char full_name[256];
        if (header.prefix[0] != 0) {
            snprintf(full_name, sizeof(full_name), "%s/%s", header.prefix, header.name);
        } else {
            snprintf(full_name, sizeof(full_name), "%s", header.name);
        }

        bool is_dir = (header.typeflag == '5' || full_name[strlen(full_name) - 1] == '/');

        // Extracting into root (/) for now
        char dest_path[256];
        snprintf(dest_path, sizeof(dest_path), "/%s", full_name);

        if (is_dir) {
            printf("  creating dir  %s\n", dest_path);
            create_dir_recursive(dest_path);
        } else {
            printf("  extracting    %s (%zu bytes)\n", dest_path, file_size);
            
            if (manifest_fd >= 0 && strcmp(dest_path, "/port.yaml") != 0) {
                write(manifest_fd, dest_path, strlen(dest_path));
                write(manifest_fd, "\n", 1);
            }

            char parent_dir[256];
            snprintf(parent_dir, sizeof(parent_dir), "%s", dest_path);
            char* last_slash = strrchr(parent_dir, '/');
            if (last_slash && last_slash != parent_dir) {
                *last_slash = 0;
                create_dir_recursive(parent_dir);
            }

            int out_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out_fd >= 0) {
                size_t remaining = file_size;
                char buf[512];
                while (remaining > 0) {
                    size_t to_read = (remaining > 512) ? 512 : remaining;
                    read(fd, buf, 512);
                    write(out_fd, buf, to_read);
                    remaining -= to_read;
                }
                close(out_fd);
            } else {
                printf("  error: failed to create %s\n", dest_path);
                size_t blocks = (file_size + 511) / 512;
                for (size_t i = 0; i < blocks; i++) {
                    read(fd, block, 512);
                }
            }
        }
    }

    close(fd);
    return 0;
}

void print_usage() {
    printf("usage: pkg <command> [<args>]\n");
    printf("commands:\n");
    printf("  install <file.rpkg>   Install a local package archive\n");
    printf("  remove <name>         Remove an installed package\n");
    printf("  list                  List installed packages\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            printf("error: install requires a package file\n");
            return 1;
        }
        
#ifndef RUCUX_ARCH
#define RUCUX_ARCH "any"
#endif

        char* base = strrchr(argv[2], '/');
        base = base ? base + 1 : argv[2];
        
        char expected_suffix_arch[64];
        char expected_suffix_any[64];
        snprintf(expected_suffix_arch, sizeof(expected_suffix_arch), "-%s.rpkg", RUCUX_ARCH);
        snprintf(expected_suffix_any, sizeof(expected_suffix_any), "-any.rpkg");

        if (strstr(base, expected_suffix_arch) == NULL && strstr(base, expected_suffix_any) == NULL) {
            printf("error: package architecture does not match system architecture (%s)\n", RUCUX_ARCH);
            return 1;
        }

        printf("Installing %s...\n", argv[2]);
        char reg_file[256];
        snprintf(reg_file, sizeof(reg_file), "/var/db/pkg/%s.txt", base);
        
        create_dir_recursive("/var/db/pkg");
        int manifest_fd = open(reg_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (extract_tar(argv[2], manifest_fd) == 0) {
            if (manifest_fd >= 0) close(manifest_fd);
            printf("Done.\n");
        } else {
            if (manifest_fd >= 0) close(manifest_fd);
            unlink(reg_file); // cleanup
            printf("Failed.\n");
            return 1;
        }
    } else if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            printf("error: remove requires a package name\n");
            return 1;
        }
        printf("Removing %s...\n", argv[2]);
        char reg_file[256];
        snprintf(reg_file, sizeof(reg_file), "/var/db/pkg/%s.txt", argv[2]);
        
        int fd = open(reg_file, O_RDONLY);
        if (fd >= 0) {
            char ch;
            char line[256];
            size_t line_idx = 0;
            while (read(fd, &ch, 1) == 1) {
                if (ch == '\n') {
                    line[line_idx] = '\0';
                    if (line_idx > 0 && strcmp(line, "/") != 0 && strcmp(line, "/port.yaml") != 0) {
                        printf("  deleting %s\n", line);
                        unlink(line);
                    }
                    line_idx = 0;
                } else if (line_idx < sizeof(line) - 1) {
                    line[line_idx++] = ch;
                }
            }
            if (line_idx > 0) {
                line[line_idx] = '\0';
                if (strcmp(line, "/") != 0 && strcmp(line, "/port.yaml") != 0) {
                    printf("  deleting %s\n", line);
                    unlink(line);
                }
            }
            close(fd);
            unlink(reg_file);
            printf("Package removed.\n");
        } else {
            printf("error: package %s is not installed.\n", argv[2]);
            return 1;
        }
    } else if (strcmp(argv[1], "list") == 0) {
        printf("Installed packages:\n");
        DIR* dir = opendir("/var/db/pkg");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                // Strip .txt extension for display
                char* ext = strstr(entry->d_name, ".txt");
                if (ext) *ext = '\0';
                printf("  - %s\n", entry->d_name);
            }
            closedir(dir);
        } else {
            printf("  (none)\n");
        }
    } else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }

    return 0;
}