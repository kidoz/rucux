CXX ?= c++
CXXFLAGS = -std=c++23 -Wall -Wextra -g
CPPFLAGS = -I../src/include -idirafter ../lib/include -I..
LDLIBS = -lpthread
OBJDIR = ../builddir-host-tests
TESTS = test_atomic test_spinlock test_buddy test_libc test_elf test_user_access test_time test_tty test_network
LIBC_SOURCES = vsprintf snprintf strtol sscanf inet
LIBC_OBJECTS = $(addprefix $(OBJDIR)/libc_,$(addsuffix .o,$(LIBC_SOURCES)))
LIBC_SYMBOLS = vsprintf vsnprintf snprintf strtol strtoul strtoll strtoull strtod strtof strtold sscanf htons htonl ntohs ntohl inet_ntoa inet_pton inet_ntop
LIBC_RENAMES = $(foreach symbol,$(LIBC_SYMBOLS),-D$(symbol)=rucux_$(symbol))

.PHONY: all clean run
all: $(TESTS)

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

test_atomic: $(OBJDIR)/test_atomic.o
	$(CXX) -o $@ $^ $(LDLIBS)
test_spinlock: $(OBJDIR)/test_spinlock.o
	$(CXX) -o $@ $^ $(LDLIBS)
test_buddy: $(OBJDIR)/test_buddy.o $(OBJDIR)/buddy.o
	$(CXX) -o $@ $^ $(LDLIBS)
test_libc: $(OBJDIR)/test_libc.o $(LIBC_OBJECTS)
	$(CXX) -o $@ $^ $(LDLIBS)
test_elf: $(OBJDIR)/test_elf.o $(OBJDIR)/elf_validate.o
	$(CXX) -o $@ $^ $(LDLIBS)

$(OBJDIR)/user_access.o: ../src/kernel/memory/user_access.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/syscall.o: ../src/kernel/syscall.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
test_user_access: $(OBJDIR)/test_user_access.o $(OBJDIR)/user_access.o $(OBJDIR)/syscall.o
	$(CXX) -o $@ $^ $(LDLIBS)

run: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done

clean:
	rm -f $(TESTS) $(OBJDIR)/*.o $(OBJDIR)/*.d

-include $(wildcard $(OBJDIR)/*.d)

$(OBJDIR)/time.o: ../src/kernel/time.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
$(OBJDIR)/tty.o: ../src/kernel/vfs/tty.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
test_time: $(OBJDIR)/test_time.o $(OBJDIR)/time.o
	$(CXX) -o $@ $^ $(LDLIBS)
test_tty: $(OBJDIR)/test_tty.o $(OBJDIR)/tty.o
	$(CXX) -o $@ $^ $(LDLIBS)

NETWORK_SOURCES = netbuf netif ethernet arp ipv4 checksum udp tcp socket
NETWORK_OBJECTS = $(addprefix $(OBJDIR)/net_,$(addsuffix .o,$(NETWORK_SOURCES)))
$(OBJDIR)/net_%.o: ../src/kernel/net/%.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
test_network: $(OBJDIR)/test_network.o $(NETWORK_OBJECTS)
	$(CXX) -o $@ $^ $(LDLIBS)
