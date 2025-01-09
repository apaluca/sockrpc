# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -I../include
LDFLAGS = -L../lib -lsockrpc -lcjson -pthread

# Math library for calculator
MATH_LIBS = -lm

# Executables
BASIC_EXECS = basic/basic_server basic/basic_client
STRING_EXECS = string_ops/string_server string_ops/string_client
CALC_EXECS = calculator/calc_server calculator/calc_client
DB_EXECS = database/db_server database/db_client

ALL_EXECS = $(BASIC_EXECS) $(STRING_EXECS) $(CALC_EXECS) $(DB_EXECS)

# Default target
all: $(ALL_EXECS)

# Create directories
create_dirs:
	@mkdir -p basic string_ops calculator database

# Basic example
basic/basic_server: basic/basic_server.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

basic/basic_client: basic/basic_client.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# String operations example
string_ops/string_server: string_ops/string_server.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

string_ops/string_client: string_ops/string_client.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Calculator example
calculator/calc_server: calculator/calc_server.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(MATH_LIBS)

calculator/calc_client: calculator/calc_client.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Database example
database/db_server: database/db_server.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

database/db_client: database/db_client.c | create_dirs
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Clean build files
clean:
	rm -f $(ALL_EXECS)

.PHONY: all clean create_dirs