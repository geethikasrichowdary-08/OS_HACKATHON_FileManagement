CC ?= gcc
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
LDFLAGS ?=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/minish
SOURCES := src/main.c src/parser.c src/builtins.c src/jobs.c src/execute.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: src/%.c include/shell.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

test: $(TARGET)
	python3 -m unittest discover -s tests -v

clean:
	rm -rf $(BUILD_DIR)