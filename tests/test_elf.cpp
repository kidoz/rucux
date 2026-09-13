// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <cstring>
#include <kernel/memory/user_access.hpp>
#include <kernel/process/elf.hpp>

using kernel::process::elf;
using kernel::process::elf64_ehdr;
using kernel::process::elf64_phdr;

struct fixture {
    uint8_t bytes[512]{};
    elf64_ehdr& h = *reinterpret_cast<elf64_ehdr*>(bytes);
    elf64_phdr& p = *reinterpret_cast<elf64_phdr*>(bytes + sizeof(h));
    fixture() {
        const uint8_t ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
        memcpy(h.e_ident, ident, sizeof(ident));
        h.e_type = 2;
        h.e_machine = 183;
        h.e_version = 1;
        h.e_ehsize = sizeof(h);
        h.e_phoff = sizeof(h);
        h.e_phentsize = sizeof(p);
        h.e_phnum = 1;
        h.e_entry = kernel::memory::USER_BEGIN;
        p.p_type = elf::PT_LOAD;
        p.p_flags = 5;
        p.p_offset = 256;
        p.p_filesz = 8;
        p.p_memsz = 16;
        p.p_vaddr = h.e_entry;
        p.p_align = 1;
    }
    bool valid() { return elf::validate(bytes, sizeof(bytes), 183); }
};

TEST(valid_executable) {
    fixture f;
    ASSERT(f.valid());
}
TEST(truncated_header) {
    fixture f;
    ASSERT(!elf::validate(f.bytes, 63, 183));
}
TEST(header_offset_overflow) {
    fixture f;
    f.h.e_phoff = UINT64_MAX;
    ASSERT(!f.valid());
}
TEST(header_count_bounds) {
    fixture f;
    f.h.e_phnum = 100;
    ASSERT(!f.valid());
}
TEST(segment_file_bounds) {
    fixture f;
    f.p.p_offset = 510;
    ASSERT(!f.valid());
}
TEST(filesz_exceeds_memsz) {
    fixture f;
    f.p.p_filesz = 17;
    ASSERT(!f.valid());
}
TEST(kernel_destination) {
    fixture f;
    f.p.p_vaddr = 0x40080000;
    ASSERT(!f.valid());
}
TEST(virtual_range_overflow) {
    fixture f;
    f.p.p_memsz = UINT64_MAX;
    ASSERT(!f.valid());
}
TEST(wrong_machine) {
    fixture f;
    ASSERT(!elf::validate(f.bytes, sizeof(f.bytes), 62));
}
TEST(writable_executable) {
    fixture f;
    f.p.p_flags = 7;
    ASSERT(!f.valid());
}
TEST(nonexecutable_entry) {
    fixture f;
    f.p.p_flags = 4;
    ASSERT(!f.valid());
}
TEST(entry_outside_image) {
    fixture f;
    ++f.h.e_entry;
    f.h.e_entry += 4096;
    ASSERT(!f.valid());
}
TEST(segment_overlap) {
    fixture f;
    f.h.e_phnum = 2;
    auto* next = &f.p + 1;
    *next = f.p;
    ASSERT(!f.valid());
}
TEST(empty_load_segment) {
    fixture f;
    f.h.e_phnum = 2;
    auto* next = &f.p + 1;
    *next = {};
    next->p_type = elf::PT_LOAD;
    ASSERT(f.valid());
}
TEST(alignment_mismatch) {
    fixture f;
    f.p.p_align = 4096;
    ASSERT(!f.valid());
}

int main() {
    return rucux_test::run_all_tests("ELF validation");
}
