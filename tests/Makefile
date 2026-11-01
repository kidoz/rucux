CXX ?= c++
CXXFLAGS = -std=c++23 -Wall -Wextra -g
CPPFLAGS = -I../src/include -idirafter ../lib/include -I..
LDLIBS = -lpthread
OBJDIR ?= ../build/host-tests
TEST_NAMES = test_tlb test_dispatch test_ipc test_cancellation test_atomic test_spinlock test_buddy test_libc test_elf test_user_access test_time test_tty test_network test_credentials test_rcu test_fat32 test_ata test_vfs test_file_sync test_file_metadata test_file_write test_mmap test_vma test_file_mmap
TESTS = $(addprefix $(OBJDIR)/,$(TEST_NAMES))
LIBC_SOURCES = vsprintf snprintf strtol sscanf inet
LIBC_OBJECTS = $(addprefix $(OBJDIR)/libc_,$(addsuffix .o,$(LIBC_SOURCES)))
LIBC_SYMBOLS = vsprintf vsnprintf snprintf strtol strtoul strtoll strtoull strtod strtof strtold sscanf htons htonl ntohs ntohl inet_ntoa inet_pton inet_ntop
LIBC_RENAMES = $(foreach symbol,$(LIBC_SYMBOLS),-D$(symbol)=rucux_$(symbol))

.PHONY: all clean run $(TEST_NAMES)
all: $(TESTS)

$(TEST_NAMES): %: $(OBJDIR)/%

$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/test_%.o: test_%.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR)/buddy.o: ../src/kernel/memory/buddy.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR)/elf_validate.o: ../src/kernel/process/elf_validate.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR)/libc_%.o: ../libc/src/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I../libc/include -I.. $(LIBC_RENAMES) -fno-builtin -MMD -MP -c -o $@ $<

$(OBJDIR)/test_atomic: $(OBJDIR)/test_atomic.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_spinlock: $(OBJDIR)/test_spinlock.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/rcu.o: ../src/kernel/sync/rcu.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_rcu: $(OBJDIR)/test_rcu.o $(OBJDIR)/rcu.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/fat32.o: ../src/kernel/vfs/fat32.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_fat32: $(OBJDIR)/test_fat32.o $(OBJDIR)/fat32.o $(OBJDIR)/vfs.o $(OBJDIR)/signal.o $(OBJDIR)/ipc.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/ata.o: ../src/arch/amd64/ata.cpp | $(OBJDIR)
	$(CXX) -Isupport $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_ata: $(OBJDIR)/test_ata.o $(OBJDIR)/ata.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/vfs.o: ../src/kernel/vfs/vfs.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/ramfs.o: ../src/kernel/vfs/ramfs.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/mmap.o: ../src/kernel/memory/mmap.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/vma.o: ../src/kernel/memory/vma.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/rbtree.o: ../src/kernel/ds/rbtree.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_mmap: $(OBJDIR)/test_mmap.o $(OBJDIR)/mmap.o $(OBJDIR)/vma.o $(OBJDIR)/rbtree.o $(OBJDIR)/vfs.o $(OBJDIR)/signal.o $(OBJDIR)/ipc.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_vma: $(OBJDIR)/test_vma.o $(OBJDIR)/vma.o $(OBJDIR)/rbtree.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/libc_mmap.o: ../libc/src/mmap.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I../libc/include -I.. -Dmmap=rucux_mmap -Dsyscall=rucux_test_syscall -D__errno_location=rucux_test_errno_location -MMD -MP -c -o $@ $<
$(OBJDIR)/test_file_mmap: $(OBJDIR)/test_file_mmap.o $(OBJDIR)/libc_mmap.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_vfs: $(OBJDIR)/test_vfs.o $(OBJDIR)/vfs.o $(OBJDIR)/ramfs.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/libc_file_sync.o: ../libc/src/file_sync.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I../libc/include -I.. -Dfsync=rucux_fsync -Dftruncate=rucux_ftruncate -Dsyscall=rucux_test_syscall -D__errno_location=rucux_test_errno_location -MMD -MP -c -o $@ $<
$(OBJDIR)/test_file_sync: $(OBJDIR)/test_file_sync.o $(OBJDIR)/libc_file_sync.o
	$(CXX) -o $@ $^ $(LDLIBS)
METADATA_RENAMES = -Dopen=rucux_open -Dstat=rucux_stat -Dfstat=rucux_fstat -Dlseek=rucux_lseek -Dsyscall=rucux_test_syscall -D__errno_location=rucux_test_errno_location
$(OBJDIR)/metadata_%.o: ../libc/src/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I../libc/include -I.. $(METADATA_RENAMES) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_file_metadata: $(OBJDIR)/test_file_metadata.o $(OBJDIR)/metadata_file_metadata.o $(OBJDIR)/metadata_lseek.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/libc_file_write.o: ../libc/src/file_write.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I../libc/include -I.. -Dread=rucux_read -Dwrite=rucux_write -Dfcntl=rucux_fcntl -Dsyscall=rucux_test_syscall -D__errno_location=rucux_test_errno_location -MMD -MP -c -o $@ $<
$(OBJDIR)/test_file_write: $(OBJDIR)/test_file_write.o $(OBJDIR)/libc_file_write.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_buddy: $(OBJDIR)/test_buddy.o $(OBJDIR)/buddy.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_libc: $(OBJDIR)/test_libc.o $(LIBC_OBJECTS)
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_elf: $(OBJDIR)/test_elf.o $(OBJDIR)/elf_validate.o
	$(CXX) -o $@ $^ $(LDLIBS)

$(OBJDIR)/user_access.o: ../src/kernel/memory/user_access.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/syscall.o: ../src/kernel/syscall.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_user_access: $(OBJDIR)/test_user_access.o $(OBJDIR)/user_access.o $(OBJDIR)/syscall.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_credentials: $(OBJDIR)/test_credentials.o
	$(CXX) -o $@ $^ $(LDLIBS)

run: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done

clean:
	rm -rf $(OBJDIR)

-include $(wildcard $(OBJDIR)/*.d)

$(OBJDIR)/time.o: ../src/kernel/time.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/tty.o: ../src/kernel/vfs/tty.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_time: $(OBJDIR)/test_time.o $(OBJDIR)/time.o
	$(CXX) -o $@ $^ $(LDLIBS)
$(OBJDIR)/test_tty: $(OBJDIR)/test_tty.o $(OBJDIR)/tty.o
	$(CXX) -o $@ $^ $(LDLIBS)

NETWORK_SOURCES = netbuf netif ethernet arp ipv4 checksum udp tcp socket
NETWORK_OBJECTS = $(addprefix $(OBJDIR)/net_,$(addsuffix .o,$(NETWORK_SOURCES)))
$(OBJDIR)/net_%.o: ../src/kernel/net/%.cpp network_test_alloc.hpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -include network_test_alloc.hpp -MMD -MP -c -o $@ $<
$(OBJDIR)/test_network: $(OBJDIR)/test_network.o $(NETWORK_OBJECTS)
	$(CXX) -o $@ $^ $(LDLIBS)

# Optional Clang/libFuzzer campaign. Separate objects keep sanitizers out of the
# normal hosted build. The in-process fixtures never open host network sockets.
.PHONY: fuzz-network
fuzz-network: $(OBJDIR)/fuzz_network
FUZZ_FLAGS = -DRUCUX_NETWORK_FUZZ -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
FUZZ_OBJECTS = $(OBJDIR)/network-fuzz/test_network.o $(addprefix $(OBJDIR)/network-fuzz/net_,$(addsuffix .o,$(NETWORK_SOURCES)))
$(OBJDIR)/network-fuzz:
	mkdir -p $@
$(OBJDIR)/network-fuzz/test_network.o: test_network.cpp | $(OBJDIR)/network-fuzz
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FUZZ_FLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/network-fuzz/net_%.o: ../src/kernel/net/%.cpp network_test_alloc.hpp | $(OBJDIR)/network-fuzz
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FUZZ_FLAGS) -include network_test_alloc.hpp -MMD -MP -c -o $@ $<
$(OBJDIR)/fuzz_network: $(FUZZ_OBJECTS)
	$(CXX) $(FUZZ_FLAGS) -o $@ $^ $(LDLIBS)
-include $(wildcard $(OBJDIR)/network-fuzz/*.d)

$(OBJDIR)/signal.o: ../src/kernel/process/signal.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_cancellation: $(OBJDIR)/test_cancellation.o $(OBJDIR)/signal.o $(OBJDIR)/syscall.o $(OBJDIR)/user_access.o $(OBJDIR)/futex.o $(OBJDIR)/ipc.o
	$(CXX) -o $@ $^ $(LDLIBS)

$(OBJDIR)/futex.o: ../src/kernel/sync/futex.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR)/ipc.o: ../src/kernel/ipc/ipc.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_ipc: $(OBJDIR)/test_ipc.o $(OBJDIR)/ipc.o $(OBJDIR)/signal.o $(OBJDIR)/syscall.o $(OBJDIR)/credentials.o
	$(CXX) -o $@ $^ $(LDLIBS)

$(OBJDIR)/credentials.o: ../src/kernel/process/credentials.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR)/dispatch.o: ../src/kernel/scheduler/dispatch.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/test_dispatch: $(OBJDIR)/test_dispatch.o $(OBJDIR)/dispatch.o
	$(CXX) -o $@ $^ $(LDLIBS)

$(OBJDIR)/test_tlb: $(OBJDIR)/test_tlb.o
	$(CXX) -o $@ $^ $(LDLIBS)
