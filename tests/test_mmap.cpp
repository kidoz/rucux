// SPDX-License-Identifier: MIT
// Production mmap/VMA code with observable physical frames and page-table edges.
#include "test_harness.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <kernel/memory/mmap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vma.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <map>
#include <new>
#include <set>
#include <thread>

namespace {
using namespace kernel::memory;
using namespace kernel::vfs;
constexpr int READ = 1, WRITE = 2, PRIVATE = 2, SHARED = 1, FIXED = 16, ANONYMOUS = 32;
constexpr size_t PAGE = pmm::PAGE_SIZE;
constexpr uintptr_t START = USER_BEGIN + 0x1000000;
struct Leaf {
    uintptr_t phys;
    page_flags flags;
};
std::map<uintptr_t, Leaf> leaves;
std::set<void*> frames;
size_t alloc_calls, fail_alloc = SIZE_MAX, map_calls, fail_map = SIZE_MAX;
bool bad_free, has_thread;
kernel::scheduler::thread current{};
kernel::scheduler::thread::file_descriptor descriptors[2]{};
address_space mm{};
vfs_node file{};
vfs_ops ops{};
std::array<uint8_t, PAGE * 3> contents;
unsigned finalized;
size_t read_calls, fail_read = SIZE_MAX, read_limit = SIZE_MAX;
long read_error = -5;
void (*on_read)();
std::function<void()> on_free;
std::atomic<bool> read_entered{false}, read_release{false};

struct Fixture {
    Fixture() {
        mm.vmas.init();
        mm.next_mmap = START;
        current.mm = &mm;
        current.fd_table = descriptors;
        current.fd_count = 2;
        has_thread = true;
        alloc_calls = map_calls = read_calls = 0;
        finalized = 0;
        fail_alloc = fail_map = fail_read = read_limit = SIZE_MAX;
        read_error = -5;
        bad_free = false;
        on_read = nullptr;
        on_free = nullptr;
        new (&file) vfs_node{};
        file.type = file_type::REGULAR;
        file.length = contents.size();
        file.ops = &ops;
        ops = {};
        for (size_t i = 0; i < contents.size(); ++i)
            contents[i] = static_cast<uint8_t>(i * 19 + 73);
        descriptors[0] = {&file, 99, 2};
        descriptors[1] = {};
        ops.read = [](vfs_node*, size_t offset, size_t size, void* output) -> long {
            if (on_read) {
                auto callback = on_read;
                on_read = nullptr;
                callback();
            }
            if (read_calls++ == fail_read) return read_error;
            if (offset >= contents.size()) return 0;
            if (size > contents.size() - offset) size = contents.size() - offset;
            if (size > read_limit) size = read_limit;
            std::memcpy(output, contents.data() + offset, size);
            return static_cast<long>(size);
        };
    }
    ~Fixture() {
        for (auto& descriptor : descriptors) {
            auto* target = static_cast<vfs_node*>(descriptor.node);
            descriptor = {};
            if (target && target->destroy) target->close_reference();
        }
        leaves.clear();
        mm.vmas.destroy_all();
        for (void* frame : frames)
            std::free(frame);
        frames.clear();
    }
};
intptr_t map_file(size_t length = PAGE * 2, long offset = 0, int flags = PRIVATE, void* address = nullptr) {
    return reinterpret_cast<intptr_t>(mmap_manager::sys_mmap(address, length, READ, flags, 0, offset));
}
vfs_node* managed_file(bool borrowed_frames = false) {
    auto* target = new vfs_node();
    target->type = borrowed_frames ? file_type::CHAR_DEVICE : file_type::REGULAR;
    target->length = contents.size();
    target->ops = &ops;
    target->destroy = [](vfs_node* node) {
        ++finalized;
        if (node->ptr) pmm::free_page(node->ptr);
        delete node;
    };
    if (borrowed_frames) {
        target->ptr = reinterpret_cast<vfs_node*>(pmm::alloc_page());
        ASSERT(target->ptr);
        ops.mmap = [](vfs_node* node, size_t) { return reinterpret_cast<uintptr_t>(node->ptr); };
    }
    descriptors[0].node = target;
    return target;
}
void close_file() {
    auto* target = static_cast<vfs_node*>(descriptors[0].node);
    descriptors[0] = {};
    if (target) target->close_reference();
}
void assert_clean() {
    ASSERT(frames.empty());
    ASSERT(leaves.empty());
    ASSERT(mm.vmas.find(START) == nullptr);
    ASSERT(mm.vmas.find(START + PAGE) == nullptr);
    ASSERT_EQ(mm.next_mmap, START);
    ASSERT(!bad_free);
}
} // namespace

namespace kernel::scheduler {
thread* scheduler::current_thread() noexcept {
    return has_thread ? &current : nullptr;
}
} // namespace kernel::scheduler

namespace kernel::memory {
void* pmm::alloc_page() noexcept {
    if (alloc_calls++ == fail_alloc) return nullptr;
    void* frame = std::aligned_alloc(PAGE, PAGE);
    if (frame) frames.insert(frame);
    return frame;
}
void pmm::free_page(void* frame) noexcept {
    bool mapped = false;
    for (const auto& entry : leaves)
        if (entry.second.phys == reinterpret_cast<uintptr_t>(frame)) mapped = true;
    if (mapped || frames.erase(frame) != 1) {
        bad_free = true;
        return;
    }
    std::free(frame);
    if (on_free) {
        auto callback = std::move(on_free);
        on_free = nullptr;
        callback();
    }
}
void vmm::map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    if (map_calls++ != fail_map) leaves[virt] = {phys, flags};
}
uintptr_t vmm::get_phys(uintptr_t virt) noexcept {
    const auto found = leaves.find(virt);
    return found == leaves.end() ? 0 : found->second.phys;
}
void vmm::unmap(uintptr_t virt) noexcept {
    leaves.erase(virt);
}
bool vmm::protect(uintptr_t virt, page_flags flags) noexcept {
    auto found = leaves.find(virt);
    if (found == leaves.end()) return false;
    found->second.flags = flags;
    return true;
}
} // namespace kernel::memory

TEST(mmap_eager_population_pins_node_across_reentrant_descriptor_close) {
    Fixture fixture;
    managed_file();
    on_read = [] {
        close_file();
        ASSERT_EQ(finalized, 0U);
    };
    ASSERT_EQ(map_file(), static_cast<intptr_t>(START));
    ASSERT_EQ(finalized, 1U); // Private copied frames no longer depend on the file.
    ASSERT(mm.vmas.find(START)->backing.get() == nullptr);
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE * 2), 0);
    ASSERT(frames.empty());
    ASSERT(leaves.empty());
    ASSERT(mm.vmas.find(START) == nullptr);
    ASSERT(!bad_free);
}

TEST(mmap_failed_population_releases_pin_after_rollback) {
    for (size_t failing : {0UL, 1UL}) {
        Fixture fixture;
        managed_file();
        fail_read = failing;
        on_read = [] { close_file(); };
        ASSERT_EQ(map_file(), -5);
        ASSERT_EQ(finalized, 1U);
        assert_clean();
    }
}

TEST(mmap_borrowed_owner_survives_close_splits_and_partial_unmap) {
    Fixture fixture;
    managed_file(true);
    ASSERT_EQ(map_file(PAGE * 3), static_cast<intptr_t>(START));
    close_file();
    ASSERT_EQ(finalized, 0U);
    ASSERT(!mm.vmas.owns_frame(START));
    ASSERT_EQ(mmap_manager::sys_mprotect(reinterpret_cast<void*>(START + PAGE), PAGE, READ | WRITE), 0);
    ASSERT(mm.vmas.find(START)->backing.get());
    ASSERT(mm.vmas.find(START + PAGE)->backing.get());
    ASSERT(mm.vmas.find(START + PAGE * 2)->backing.get());
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START + PAGE), PAGE), 0);
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE), 0);
    ASSERT_EQ(finalized, 0U);
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START + PAGE * 2), PAGE), 0);
    ASSERT_EQ(finalized, 1U);
    ASSERT(frames.empty());
    ASSERT(!bad_free);
}

TEST(mmap_borrowed_owner_is_released_after_inactive_translations_are_dropped) {
    Fixture fixture;
    managed_file(true);
    ASSERT_EQ(map_file(), static_cast<intptr_t>(START));
    close_file();
    ASSERT_EQ(finalized, 0U);
    ASSERT(!mm.vmas.owns_frame(START));
    ASSERT(mm.vmas.owns_frame(START + PAGE * 8)); // ELF/stack frames outside VMAs.
    // Model inactive page-table reclamation: skip borrowed frames, remove the
    // translations, then destroy VMAs and their last backing references.
    leaves.clear();
    mm.vmas.destroy_all();
    ASSERT_EQ(finalized, 1U);
    ASSERT(frames.empty());
    ASSERT(!bad_free);
}

TEST(mmap_borrowed_population_failure_drops_vma_and_temporary_pins) {
    Fixture fixture;
    auto* target = managed_file(true);
    fail_map = 1;
    ASSERT_EQ(map_file(), -12);
    ASSERT_EQ(finalized, 0U);
    ASSERT_EQ(target->references.load(), 1U); // Only the descriptor remains.
    ASSERT(leaves.empty());
    ASSERT(mm.vmas.find(START) == nullptr);
    close_file();
    ASSERT_EQ(finalized, 1U);
    assert_clean();
}

TEST(mmap_first_backing_read_failure_is_reported_and_releases_everything) {
    Fixture fixture;
    fail_read = 0;
    ASSERT_EQ(map_file(), -5);
    assert_clean();
}

TEST(mmap_later_read_errors_roll_back_installed_pages_and_allow_retry) {
    for (long error : {-5L, -16L}) {
        Fixture fixture;
        fail_read = 1;
        read_error = error;
        ASSERT_EQ(map_file(), error);
        ASSERT_EQ(alloc_calls, 2UL);
        ASSERT_EQ(map_calls, 1UL);
        assert_clean();
        fail_read = SIZE_MAX;
        ASSERT_EQ(map_file(), static_cast<intptr_t>(START));
        ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE * 2), 0);
        ASSERT(frames.empty() && leaves.empty());
        ASSERT(mm.vmas.find(START) == nullptr);
        ASSERT(!bad_free);
    }
}

TEST(mmap_failure_after_a_short_prefix_releases_the_uninstalled_frame) {
    Fixture fixture;
    read_limit = 17;
    fail_read = 2;
    ASSERT_EQ(map_file(), -5);
    ASSERT_EQ(alloc_calls, 1UL);
    ASSERT_EQ(map_calls, 0UL);
    assert_clean();
}

TEST(mmap_rejects_unexpected_eof_and_impossible_callback_results) {
    for (long result : {0L, static_cast<long>(PAGE + 1), -4096L}) {
        Fixture fixture;
        fail_read = 0;
        read_error = result;
        ASSERT_EQ(map_file(), -5);
        assert_clean();
    }
}

TEST(mmap_positive_short_reads_are_retried_until_the_page_is_filled) {
    Fixture fixture;
    read_limit = 37;
    ASSERT_EQ(map_file(), static_cast<intptr_t>(START));
    for (size_t i = 0; i < 2; ++i) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(leaves.at(START + i * PAGE).phys);
        ASSERT(std::memcmp(bytes, contents.data() + i * PAGE, PAGE) == 0);
    }
    ASSERT_EQ(descriptors[0].offset, 99UL);
    ASSERT((mm.vmas.find(START)->flags & VMA_POPULATING) == 0);
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE * 2), 0);
    ASSERT(frames.empty() && leaves.empty());
    ASSERT(!bad_free);
}

TEST(mmap_snapshot_eof_zero_fills_the_tail_and_pages_beyond_eof) {
    Fixture fixture;
    ops.refresh_metadata = [](vfs_node* node) {
        node->length = PAGE + 37;
        return 0;
    };
    ASSERT_EQ(map_file(PAGE * 3 - 11), static_cast<intptr_t>(START));
    ASSERT_EQ(read_calls, 2UL);
    for (size_t page = 0; page < 3; ++page) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(leaves.at(START + page * PAGE).phys);
        for (size_t i = 0; i < PAGE; ++i) {
            const size_t offset = page * PAGE + i;
            ASSERT_EQ(bytes[i], offset < PAGE + 37 ? contents[offset] : 0);
        }
    }
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE * 3), 0);
    ASSERT(frames.empty() && leaves.empty());
}

TEST(mmap_metadata_errors_happen_before_frame_or_vma_allocation) {
    Fixture fixture;
    ops.refresh_metadata = [](vfs_node*) { return -16; };
    ASSERT_EQ(map_file(), -16);
    ASSERT_EQ(alloc_calls, 0UL);
    ASSERT_EQ(read_calls, 0UL);
    assert_clean();
}

TEST(mmap_page_allocation_and_install_failures_release_all_owned_frames) {
    for (size_t failure = 0; failure < 2; ++failure) {
        {
            Fixture fixture;
            fail_alloc = failure;
            ASSERT_EQ(map_file(), -12);
            assert_clean();
        }
        {
            Fixture fixture;
            fail_map = failure;
            ASSERT_EQ(map_file(), -12);
            assert_clean();
        }
    }
}

TEST(mmap_pending_population_excludes_mutators_and_anonymous_fault_fill) {
    Fixture fixture;
    static int unmap_result, protect_result;
    static intptr_t replace_result;
    static bool fault_result;
    on_read = [] {
        unmap_result = mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE);
        protect_result = mmap_manager::sys_mprotect(reinterpret_cast<void*>(START), PAGE, READ | WRITE);
        replace_result = map_file(PAGE, 0, PRIVATE | FIXED, reinterpret_cast<void*>(START));
        fault_result = mmap_manager::handle_fault(START);
    };
    fail_read = 1;
    ASSERT_EQ(map_file(), -5);
    ASSERT_EQ(unmap_result, -16);
    ASSERT_EQ(protect_result, -16);
    ASSERT_EQ(replace_result, -16);
    ASSERT(!fault_result);
    ASSERT_EQ(alloc_calls, 2UL);
    assert_clean();
}

TEST(mmap_pending_population_excludes_mutation_from_a_sibling_thread) {
    Fixture fixture;
    read_entered.store(false);
    read_release.store(false);
    on_read = [] {
        read_entered.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!read_release.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    };
    intptr_t result = 0;
    std::thread worker([&] { result = map_file(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!read_entered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const int unmap_result = mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE);
    const int protect_result = mmap_manager::sys_mprotect(reinterpret_cast<void*>(START), PAGE, READ);
    read_release.store(true);
    worker.join();
    ASSERT(read_entered.load());
    ASSERT_EQ(unmap_result, -16);
    ASSERT_EQ(protect_result, -16);
    ASSERT_EQ(result, static_cast<intptr_t>(START));
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE * 2), 0);
    ASSERT(frames.empty() && leaves.empty());
    ASSERT(!bad_free);
}

TEST(mmap_unmap_rechecks_a_replaced_range_before_touching_pending_pages) {
    for (size_t old_pages : {1UL, 2UL}) {
        Fixture fixture;
        ASSERT_EQ(reinterpret_cast<uintptr_t>(
                      mmap_manager::sys_mmap(nullptr, old_pages * PAGE, READ | WRITE, PRIVATE | ANONYMOUS, -1, 0)),
                  START);
        read_entered.store(false);
        read_release.store(false);
        intptr_t result = 0;
        int nested_unmap = -1;
        std::thread worker;
        on_free = [&] {
            nested_unmap = mmap_manager::sys_munmap(reinterpret_cast<void*>(START), old_pages * PAGE);
            on_read = [] {
                read_entered.store(true);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!read_release.load() && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::yield();
            };
            worker =
                std::thread([&] { result = map_file(2 * PAGE, 0, PRIVATE | FIXED, reinterpret_cast<void*>(START)); });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!read_entered.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
        };
        const int outer_unmap = mmap_manager::sys_munmap(reinterpret_cast<void*>(START), old_pages * PAGE);
        read_release.store(true);
        if (worker.joinable()) worker.join();
        ASSERT(read_entered.load());
        ASSERT_EQ(nested_unmap, 0);
        ASSERT_EQ(outer_unmap, -16);
        ASSERT_EQ(result, static_cast<intptr_t>(START));
        ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), 2 * PAGE), 0);
        ASSERT(frames.empty() && leaves.empty());
        ASSERT(!bad_free);
    }
}

TEST(mmap_rollback_does_not_touch_an_unrelated_mapping) {
    Fixture fixture;
    const auto neighbor = START + 2 * PAGE;
    ASSERT_EQ(reinterpret_cast<uintptr_t>(mmap_manager::sys_mmap(reinterpret_cast<void*>(neighbor), PAGE, READ | WRITE,
                                                                 PRIVATE | ANONYMOUS | FIXED, -1, 0)),
              neighbor);
    ASSERT(leaves.find(neighbor) != leaves.end());
    const auto frame = leaves.at(neighbor).phys;
    fail_read = 1;
    ASSERT_EQ(map_file(), -5);
    ASSERT_EQ(frames.size(), 1UL);
    ASSERT_EQ(leaves.at(neighbor).phys, frame);
    ASSERT(mm.vmas.find(neighbor));
    ASSERT(mm.vmas.find(START) == nullptr);
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(neighbor), PAGE), 0);
    assert_clean();
}

alignas(PAGE) std::array<uint8_t, PAGE * 2> borrowed;

TEST(mmap_borrowed_device_frames_are_unmapped_without_being_freed) {
    for (bool fail : {false, true}) {
        Fixture fixture;
        file.type = file_type::CHAR_DEVICE;
        ops.read = nullptr;
        ops.mmap = [](vfs_node*, size_t offset) { return reinterpret_cast<uintptr_t>(borrowed.data() + offset); };
        if (fail)
            ops.mmap = [](vfs_node*, size_t offset) -> uintptr_t {
                return offset ? 0 : reinterpret_cast<uintptr_t>(borrowed.data());
            };
        ASSERT_EQ(map_file(PAGE * 2, 0, SHARED), fail ? -5 : static_cast<intptr_t>(START));
        if (!fail) ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE * 2), 0);
        ASSERT(frames.empty() && leaves.empty());
        ASSERT(!bad_free);
    }
}

TEST(mmap_an_unexpected_existing_leaf_is_not_owned_by_failure_cleanup) {
    Fixture fixture;
    on_read = [] { vmm::map(START, reinterpret_cast<uintptr_t>(borrowed.data()), page_flags::PRESENT); };
    ASSERT_EQ(map_file(), -17);
    ASSERT(frames.empty());
    ASSERT_EQ(leaves.at(START).phys, reinterpret_cast<uintptr_t>(borrowed.data()));
    ASSERT(mm.vmas.find(START) == nullptr);
    ASSERT(!bad_free);
    vmm::unmap(START);
    assert_clean();
}

TEST(mmap_file_pages_never_fall_back_to_anonymous_zero_faults) {
    Fixture fixture;
    ASSERT_EQ(map_file(PAGE), static_cast<intptr_t>(START));
    void* frame = reinterpret_cast<void*>(leaves.at(START).phys);
    vmm::unmap(START);
    pmm::free_page(frame);
    const auto attempts = alloc_calls;
    ASSERT(!mmap_manager::handle_fault(START));
    ASSERT_EQ(alloc_calls, attempts);
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE), 0);
    ASSERT(frames.empty() && leaves.empty());
}

TEST(mmap_anonymous_mappings_remain_eager_and_zero_filled_for_clone_stacks) {
    Fixture fixture;
    ASSERT_EQ(
        reinterpret_cast<uintptr_t>(mmap_manager::sys_mmap(nullptr, PAGE, READ | WRITE, PRIVATE | ANONYMOUS, -1, 0)),
        START);
    ASSERT_EQ(alloc_calls, 1UL);
    ASSERT(!mmap_manager::handle_fault(START));
    const auto* bytes = reinterpret_cast<const uint8_t*>(leaves.at(START).phys);
    for (size_t i = 0; i < PAGE; ++i)
        ASSERT_EQ(bytes[i], 0);
    void* original = reinterpret_cast<void*>(leaves.at(START).phys);
    vmm::unmap(START);
    pmm::free_page(original);
    ASSERT(mmap_manager::handle_fault(START));
    ASSERT_EQ(mmap_manager::sys_munmap(reinterpret_cast<void*>(START), PAGE), 0);
    ASSERT(frames.empty() && leaves.empty());
    ASSERT_EQ(read_calls, 0UL);
}

TEST(mmap_anonymous_allocation_failure_rolls_back_eager_stack_pages) {
    Fixture fixture;
    fail_alloc = 1;
    ASSERT_EQ(
        reinterpret_cast<intptr_t>(mmap_manager::sys_mmap(nullptr, PAGE * 2, READ | WRITE, PRIVATE | ANONYMOUS, -1, 0)),
        -12);
    assert_clean();
}

TEST(mmap_rejects_bad_descriptors_modes_offsets_and_internal_flags) {
    Fixture fixture;
    ASSERT_EQ(reinterpret_cast<intptr_t>(mmap_manager::sys_mmap(nullptr, PAGE, READ, PRIVATE, 1, 0)), -9);
    descriptors[0].flags = 1;
    ASSERT_EQ(map_file(), -13);
    descriptors[0].flags = 0;
    ASSERT_EQ(reinterpret_cast<intptr_t>(mmap_manager::sys_mmap(nullptr, PAGE, READ | WRITE, SHARED, 0, 0)), -13);
    descriptors[0].flags = 2;
    ASSERT_EQ(map_file(PAGE, -1), -22);
    ASSERT_EQ(map_file(PAGE, 1), -22);
    const long large = __LONG_MAX__ & ~static_cast<long>(PAGE - 1);
    ASSERT_EQ(map_file(PAGE, large), -75);
    ASSERT_EQ(map_file(PAGE, 0, 0), -22);
    ASSERT_EQ(map_file(PAGE, 0, SHARED | PRIVATE), -22);
    ASSERT_EQ(map_file(PAGE, 0, PRIVATE | static_cast<int>(VMA_OWNS_FRAMES)), -22);
    ASSERT_EQ(map_file(PAGE, 0, PRIVATE | static_cast<int>(VMA_POPULATING)), -22);
    ASSERT_EQ(map_file(PAGE, 0, PRIVATE | FIXED), -22);
    ops.read = nullptr;
    ASSERT_EQ(map_file(), -19);
    ASSERT_EQ(alloc_calls, 0UL);
    assert_clean();
}

int main() {
    return RUN_ALL_TESTS("File-backed mmap population and rollback");
}
