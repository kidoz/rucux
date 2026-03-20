CXX ?= c++
CXXFLAGS = -std=c++20 -Wall -Wextra -g -I../src/include
LDFLAGS = -lpthread

TESTS = test_atomic test_spinlock test_buddy test_libc

.PHONY: all clean run

all: $(TESTS)

test_atomic: test_atomic.cpp test_harness.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_spinlock: test_spinlock.cpp test_harness.hpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

test_buddy: test_buddy.cpp test_harness.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_libc: test_libc.cpp test_harness.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TESTS)
	@echo ""
	@for t in $(TESTS); do echo "--- Running $$t ---"; ./$$t; echo ""; done

clean:
	rm -f $(TESTS)
