# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -I../include
LDFLAGS = -L../lib -lsockrpc -lcjson -pthread
MATH_LIBS = -lm

# Test executables
TEST_SUITE = test_suite
STRESS_TEST = stress_test

# Default target
all: $(TEST_SUITE) $(STRESS_TEST)

# Compile unit test suite
$(TEST_SUITE): test_suite.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Compile stress test
$(STRESS_TEST): stress_test.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(MATH_LIBS)

# Clean build files
clean:
	rm -f $(TEST_SUITE) $(STRESS_TEST) valgrind-*.txt

.PHONY: all clean