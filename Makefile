# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -fPIC -I./include
LDFLAGS = -shared -lcjson -pthread

# Directories
SRC_DIR = src
BUILD_DIR = build
LIB_DIR = lib
DOC_DIR = docs

# Source and object files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Library name
LIB = $(LIB_DIR)/libsockrpc.so

# Valgrind settings
VALGRIND = valgrind
VALGRIND_FLAGS = --leak-check=full \
                 --show-leak-kinds=all \
                 --track-origins=yes \
                 --verbose

# Default target
all: dirs $(LIB)

# Create necessary directories
dirs:
	@mkdir -p $(BUILD_DIR) $(LIB_DIR)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Create shared library
$(LIB): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# Build examples
examples: $(LIB)
	$(MAKE) -C examples

# Build and run tests
test: $(LIB)
	$(MAKE) -C tests
	@echo "Running unit tests with valgrind..."
	LD_LIBRARY_PATH=$(LIB_DIR) $(VALGRIND) $(VALGRIND_FLAGS) \
		--log-file=tests/valgrind-unit.txt tests/test_suite
	@echo "Valgrind unit test output saved to tests/valgrind-unit.txt"
	@echo "Running stress test with valgrind..."
	LD_LIBRARY_PATH=$(LIB_DIR) $(VALGRIND) $(VALGRIND_FLAGS) \
		--log-file=tests/valgrind-stress.txt tests/stress_test
	@echo "Valgrind stress test output saved to tests/valgrind-stress.txt"

# Run tests without valgrind (faster)
test-fast: $(LIB)
	$(MAKE) -C tests
	@echo "Running unit tests..."
	LD_LIBRARY_PATH=$(LIB_DIR) tests/test_suite
	@echo "\nRunning stress test..."
	LD_LIBRARY_PATH=$(LIB_DIR) tests/stress_test

# Create documentation with Doxygen
docs:
	doxygen Doxyfile

# Clean build files
clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)
	$(MAKE) -C examples clean
	$(MAKE) -C tests clean
	rm -f tests/valgrind-*.txt
	rm -rf $(DOC_DIR)

.PHONY: all dirs clean examples test test-fast docs